/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxpathtracershader.h"

#include "directxdevicehelpers.h"
#include "directxdxccompiler.h"
#include "directxpathtracerstringliterals.h"

#include <algorithm>
#include <cstdio>

namespace
{
  const UINT kThreadGroupSize = 8; // matches [numthreads(8, 8, 1)] in both kernels

  /// The root parameters of the accumulate kernel. Everything is a root view: the pass touches no
  /// texture, so it needs no descriptor heap and cannot be disturbed by the heaps the raster passes
  /// swap between themselves.
  enum AccumulateRootParameter : UINT
  {
    accumulateFrameConstants = 0,
    accumulateTracerConstants,
    accumulateLightConstants,
    accumulateAccelerationStructure,
    accumulateStructureUniforms,
    accumulateInstances,
    accumulateSpheres,
    accumulateCylinders,
    accumulateRibbonVertices,
    accumulateRibbonIndices,
    accumulateAccumulation,
    accumulateIndirect,
    accumulateSurfaceInfo,
    accumulateRootParameterCount
  };

  /// The root parameters of the resolve kernel. The two scene textures it reads and the image it
  /// writes have to come from a heap, a texture not being expressible as a root view.
  enum ResolveRootParameter : UINT
  {
    resolveTracerConstants = 0,
    resolveStructureUniforms,
    resolveAccumulation,
    resolveIndirect,
    resolveSurfaceInfo,
    resolveCompositeDepth,
    resolveCompositeCueMask,
    resolveSceneTextures,
    resolveCompositeTexture,
    resolveRootParameterCount
  };

  void setRootConstantBuffer(D3D12_ROOT_PARAMETER &parameter, UINT shaderRegister)
  {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  void setRootShaderResource(D3D12_ROOT_PARAMETER &parameter, UINT shaderRegister)
  {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  void setRootUnorderedAccess(D3D12_ROOT_PARAMETER &parameter, UINT shaderRegister)
  {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  ComPtr<ID3D12RootSignature> serialize(ID3D12Device *device, const D3D12_ROOT_PARAMETER *parameters,
                                        UINT count, std::string &error)
  {
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = count;
    rootDesc.pParameters = parameters;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errorBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature,
                                           &errorBlob)))
    {
      error = errorBlob ? static_cast<const char *>(errorBlob->GetBufferPointer())
                        : "the root signature could not be serialized";
      return nullptr;
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature))))
    {
      error = "the root signature could not be created";
      return nullptr;
    }
    return rootSignature;
  }

  ComPtr<ID3D12Resource> createUnorderedAccessBuffer(ID3D12Device *device, UINT64 bytes)
  {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> buffer;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&buffer))))
    {
      return nullptr;
    }
    return buffer;
  }

  void transition(ID3D12GraphicsCommandList4 *commandList, ID3D12Resource *resource,
                  D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
  {
    if (before == after) return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commandList->ResourceBarrier(1, &barrier);
  }
}

DirectXPathTracerShader::~DirectXPathTracerShader()
{
  release();
}

bool DirectXPathTracerShader::fail(const std::string &reason)
{
  m_status = reason;
  return false;
}

void DirectXPathTracerShader::release()
{
  m_accumulateRootSignature.Reset();
  m_accumulatePipeline.Reset();
  m_resolveRootSignature.Reset();
  m_resolvePipeline.Reset();
  m_commandAllocator.Reset();
  m_commandList.Reset();
  m_fence.reset();

  m_accumulation.Reset();
  m_indirect.Reset();
  m_surfaceInfo.Reset();
  m_compositeDepth.Reset();
  m_compositeCueMask.Reset();
  m_composite.Reset();
  m_uniformBuffer.Reset();
  for (ComPtr<ID3D12Resource> &buffer : m_interactiveUniformBuffers)
    buffer.Reset();
  m_interactiveUniformIndex = 0;
  m_interactiveFrameCounter = 0;
  m_heap.release();

  m_pixelCapacity = 0;
  m_compositeWidth = 0;
  m_compositeHeight = 0;
  m_compositeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  m_ready = false;
  m_status = "the path tracer has not been initialized";
}

bool DirectXPathTracerShader::createPipelines(ID3D12Device *device)
{
  {
    D3D12_ROOT_PARAMETER params[accumulateRootParameterCount] = {};
    setRootConstantBuffer(params[accumulateFrameConstants], 0);
    setRootConstantBuffer(params[accumulateTracerConstants], 2);
    setRootConstantBuffer(params[accumulateLightConstants], 3);
    setRootShaderResource(params[accumulateAccelerationStructure], 0);
    setRootShaderResource(params[accumulateStructureUniforms], 1);
    setRootShaderResource(params[accumulateInstances], 2);
    setRootShaderResource(params[accumulateSpheres], 3);
    setRootShaderResource(params[accumulateCylinders], 4);
    setRootShaderResource(params[accumulateRibbonVertices], 5);
    setRootShaderResource(params[accumulateRibbonIndices], 6);
    setRootUnorderedAccess(params[accumulateAccumulation], 0);
    setRootUnorderedAccess(params[accumulateIndirect], 1);
    setRootUnorderedAccess(params[accumulateSurfaceInfo], 2);

    std::string error;
    m_accumulateRootSignature = serialize(device, params, accumulateRootParameterCount, error);
    if (!m_accumulateRootSignature)
      return fail("the accumulate root signature failed: " + error);
  }

  // The ranges have to outlive the serialize call, the descriptor tables pointing at them.
  D3D12_DESCRIPTOR_RANGE sceneRange = {};
  sceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  sceneRange.NumDescriptors = 2; // the scene colour and the scene depth
  sceneRange.BaseShaderRegister = 4;

  D3D12_DESCRIPTOR_RANGE compositeRange = {};
  compositeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  compositeRange.NumDescriptors = 1;
  compositeRange.BaseShaderRegister = 1;

  {
    D3D12_ROOT_PARAMETER params[resolveRootParameterCount] = {};
    setRootConstantBuffer(params[resolveTracerConstants], 2);
    setRootShaderResource(params[resolveStructureUniforms], 0);
    setRootShaderResource(params[resolveAccumulation], 1);
    setRootShaderResource(params[resolveIndirect], 2);
    setRootShaderResource(params[resolveSurfaceInfo], 3);
    setRootUnorderedAccess(params[resolveCompositeDepth], 0);
    setRootUnorderedAccess(params[resolveCompositeCueMask], 2);

    params[resolveSceneTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[resolveSceneTextures].DescriptorTable.NumDescriptorRanges = 1;
    params[resolveSceneTextures].DescriptorTable.pDescriptorRanges = &sceneRange;
    params[resolveSceneTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[resolveCompositeTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[resolveCompositeTexture].DescriptorTable.NumDescriptorRanges = 1;
    params[resolveCompositeTexture].DescriptorTable.pDescriptorRanges = &compositeRange;
    params[resolveCompositeTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    std::string error;
    m_resolveRootSignature = serialize(device, params, resolveRootParameterCount, error);
    if (!m_resolveRootSignature)
      return fail("the resolve root signature failed: " + error);
  }

  struct KernelBuild
  {
    const char *name;
    const wchar_t *entryPoint;
    std::string source;
    ID3D12RootSignature *rootSignature;
    ComPtr<ID3D12PipelineState> *pipeline;
  };

  KernelBuild kernels[] = {
      {"accumulateKernel", L"accumulateKernel",
       DirectXPathTracerStringLiterals::accumulateKernelSource(),
       m_accumulateRootSignature.Get(), &m_accumulatePipeline},
      {"resolveKernel", L"resolveKernel", DirectXPathTracerStringLiterals::resolveKernelSource(),
       m_resolveRootSignature.Get(), &m_resolvePipeline}};

  for (const KernelBuild &kernel : kernels)
  {
    DirectXDxcCompiler::Result compiled =
        DirectXDxcCompiler::compile(kernel.source, kernel.entryPoint, L"cs_6_5");
    if (!compiled.succeeded())
    {
      DirectXDxcCompiler::logDiagnostics(kernel.name, compiled.diagnostics);
      return fail(std::string("the ") + kernel.name + " failed to compile");
    }
    if (!compiled.diagnostics.empty())
      DirectXDxcCompiler::logDiagnostics(kernel.name, compiled.diagnostics);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = kernel.rootSignature;
    psoDesc.CS = compiled.bytecode();
    if (FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(kernel.pipeline->GetAddressOf()))))
      return fail(std::string("the ") + kernel.name + " pipeline state could not be created");
  }

  return true;
}

void DirectXPathTracerShader::initialize(Dx12DeviceContext &context)
{
  release();

  ID3D12Device *device = context.device();
  if (!device)
  {
    fail("there is no device to trace with");
    return;
  }

  if (!context.supportsInlineRaytracing())
  {
    fail("the device does not support inline ray tracing (DXR 1.1)");
    return;
  }

  if (!DirectXDxcCompiler::canCompileInlineRaytracing())
  {
    fail("the shader compiler cannot compile inline ray tracing: " +
         DirectXDxcCompiler::unavailableReason());
    return;
  }

  if (!createPipelines(device)) return;

  // Three descriptors, and the same three every time: the scene colour, the scene depth and the
  // image the resolve writes.
  if (!m_heap.create(device, 3))
  {
    fail("the tracer descriptor heap could not be created");
    return;
  }

  m_uniformBuffer = DirectXDeviceHelpers::createUploadBuffer(device, sizeof(RKPathTracerUniforms));
  if (!m_uniformBuffer)
  {
    fail("the tracer constant buffer could not be created");
    return;
  }

  m_fence.reset(context.createFence());
  if (!m_fence)
  {
    fail("the tracer fence could not be created");
    return;
  }

  m_ready = true;
  m_status = "ready to trace";
}

bool DirectXPathTracerShader::ensureBuffers(ID3D12Device *device, UINT width, UINT height)
{
  const UINT pixels = width * height;

  if (pixels > m_pixelCapacity)
  {
    m_accumulation = createUnorderedAccessBuffer(device, UINT64(pixels) * 16);
    m_indirect = createUnorderedAccessBuffer(device, UINT64(pixels) * 16);
    m_surfaceInfo = createUnorderedAccessBuffer(device, UINT64(pixels) * 16);
    m_compositeDepth = createUnorderedAccessBuffer(device, UINT64(pixels) * 4);
    m_compositeCueMask = createUnorderedAccessBuffer(device, UINT64(pixels) * 4);
    if (!m_accumulation || !m_indirect || !m_surfaceInfo || !m_compositeDepth || !m_compositeCueMask)
    {
      m_pixelCapacity = 0;
      return false;
    }
    m_pixelCapacity = pixels;
  }

  if (m_composite && m_compositeWidth == width && m_compositeHeight == height) return true;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  // R8G8B8A8_UNORM rather than the swap chain's format: an unordered-access view has to be able to
  // store to it, and this is the 8-bit order every device is required to support storing to.
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  m_composite.Reset();
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                             IID_PPV_ARGS(&m_composite))))
  {
    m_compositeWidth = 0;
    m_compositeHeight = 0;
    return false;
  }

  m_compositeWidth = width;
  m_compositeHeight = height;
  m_compositeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  return true;
}

bool DirectXPathTracerShader::ensureCommandList(ID3D12Device *device)
{
  if (m_commandList) return true;

  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&m_commandAllocator))))
    return false;

  ComPtr<ID3D12GraphicsCommandList> list;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(),
                                       nullptr, IID_PPV_ARGS(&list))))
    return false;
  if (FAILED(list->QueryInterface(IID_PPV_ARGS(&m_commandList))))
    return false;

  m_commandList->Close();
  return true;
}

bool DirectXPathTracerShader::executeAndWait(Dx12DeviceContext &context)
{
  if (FAILED(m_commandList->Close())) return false;

  ID3D12CommandList *lists[] = {m_commandList.Get()};
  context.commandQueue()->ExecuteCommandLists(1, lists);
  context.waitForGPU(m_fence.get());
  return true;
}

void DirectXPathTracerShader::encodeAccumulate(ID3D12GraphicsCommandList4 *commandList,
                                               const DirectXPathTracerGeometry &geometry,
                                               D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
                                               D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress,
                                               ID3D12Resource *uniforms, UINT groupsX, UINT groupsY)
{
  commandList->SetComputeRootSignature(m_accumulateRootSignature.Get());
  commandList->SetPipelineState(m_accumulatePipeline.Get());

  commandList->SetComputeRootConstantBufferView(accumulateFrameConstants, frameConstantsAddress);
  commandList->SetComputeRootConstantBufferView(accumulateTracerConstants,
                                                uniforms->GetGPUVirtualAddress());
  commandList->SetComputeRootConstantBufferView(accumulateLightConstants, lightConstantsAddress);

  commandList->SetComputeRootShaderResourceView(
      accumulateAccelerationStructure,
      geometry.topLevelAccelerationStructure()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateStructureUniforms, geometry.structureUniformBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateInstances, geometry.instanceDataBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateSpheres, geometry.sphereBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateCylinders, geometry.cylinderBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateRibbonVertices, geometry.ribbonVertexBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      accumulateRibbonIndices, geometry.ribbonIndexBuffer()->GetGPUVirtualAddress());

  commandList->SetComputeRootUnorderedAccessView(accumulateAccumulation,
                                                 m_accumulation->GetGPUVirtualAddress());
  commandList->SetComputeRootUnorderedAccessView(accumulateIndirect,
                                                 m_indirect->GetGPUVirtualAddress());
  commandList->SetComputeRootUnorderedAccessView(accumulateSurfaceInfo,
                                                 m_surfaceInfo->GetGPUVirtualAddress());

  commandList->Dispatch(groupsX, groupsY, 1);
}

bool DirectXPathTracerShader::encodeResolve(ID3D12GraphicsCommandList4 *commandList,
                                            Dx12DeviceContext &context,
                                            const DirectXPathTracerGeometry &geometry,
                                            ID3D12Resource *uniforms, ID3D12Resource *sceneColor,
                                            ID3D12Resource *sceneDepth, UINT groupsX, UINT groupsY,
                                            D3D12_RESOURCE_STATES compositeEndState)
{
  ID3D12Device *device = context.device();

  // The resolve reads what the accumulate summed, so the writes have to be made visible to it.
  transition(commandList, m_accumulation.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(commandList, m_indirect.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  transition(commandList, m_surfaceInfo.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  transition(commandList, m_composite.Get(), m_compositeState,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  m_heap.reset();
  D3D12_CPU_DESCRIPTOR_HANDLE sceneCpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE sceneGpu = {};
  D3D12_CPU_DESCRIPTOR_HANDLE compositeCpu = {};
  D3D12_GPU_DESCRIPTOR_HANDLE compositeGpu = {};
  if (!m_heap.allocateRange(2, sceneCpu, sceneGpu) ||
      !m_heap.allocateRange(1, compositeCpu, compositeGpu))
    return fail("the tracer descriptor heap ran out of room");

  D3D12_SHADER_RESOURCE_VIEW_DESC colorView = {};
  colorView.Format = context.backBufferFormat();
  colorView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  colorView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  colorView.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(sceneColor, &colorView, sceneCpu);

  // The scene depth is a typeless depth/stencil resource, so the view has to name which half of it
  // is being read.
  D3D12_SHADER_RESOURCE_VIEW_DESC depthView = {};
  depthView.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  depthView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  depthView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depthView.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(sceneDepth, &depthView, m_heap.offsetHandle(sceneCpu, 1));

  D3D12_UNORDERED_ACCESS_VIEW_DESC compositeView = {};
  compositeView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  compositeView.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(m_composite.Get(), nullptr, &compositeView, compositeCpu);

  ID3D12DescriptorHeap *heaps[] = {m_heap.heap()};
  commandList->SetDescriptorHeaps(1, heaps);

  commandList->SetComputeRootSignature(m_resolveRootSignature.Get());
  commandList->SetPipelineState(m_resolvePipeline.Get());

  commandList->SetComputeRootConstantBufferView(resolveTracerConstants,
                                               uniforms->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      resolveStructureUniforms, geometry.structureUniformBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(resolveAccumulation,
                                                m_accumulation->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(resolveIndirect,
                                                m_indirect->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(resolveSurfaceInfo,
                                                m_surfaceInfo->GetGPUVirtualAddress());
  commandList->SetComputeRootUnorderedAccessView(resolveCompositeDepth,
                                                 m_compositeDepth->GetGPUVirtualAddress());
  commandList->SetComputeRootUnorderedAccessView(resolveCompositeCueMask,
                                                 m_compositeCueMask->GetGPUVirtualAddress());
  commandList->SetComputeRootDescriptorTable(resolveSceneTextures, sceneGpu);
  commandList->SetComputeRootDescriptorTable(resolveCompositeTexture, compositeGpu);

  commandList->Dispatch(groupsX, groupsY, 1);

  // Left where the caller wants it, and the sums put back so the next trace starts from the state
  // they were created in.
  transition(commandList, m_composite.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             compositeEndState);
  m_compositeState = compositeEndState;
  transition(commandList, m_accumulation.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(commandList, m_indirect.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  transition(commandList, m_surfaceInfo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  return true;
}

bool DirectXPathTracerShader::encodeInteractive(
    ID3D12GraphicsCommandList4 *commandList, Dx12DeviceContext &context,
    const DirectXPathTracerGeometry &geometry, D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
    D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress, ID3D12Resource *sceneColor,
    ID3D12Resource *sceneDepth, UINT width, UINT height, const Settings &settings,
    UINT samplesThisFrame)
{
  if (!m_ready) return fail("the path tracer is not ready to trace");
  if (!commandList) return fail("there is no command list to trace into");
  if (width == 0 || height == 0) return fail("the frame is empty");
  if (!sceneColor || !sceneDepth) return fail("there is no rasterized scene to composite over");
  if (!geometry.isValid()) return fail("there is no traceable geometry: " + geometry.status());
  if (!geometry.structureUniformBuffer()) return fail("the structure uniforms were not packed");

  ID3D12Device *device = context.device();
  if (!device) return fail("there is no device to trace with");
  if (!ensureBuffers(device, width, height))
    return fail("the per-pixel buffers could not be created");

  ComPtr<ID3D12Resource> &slot = m_interactiveUniformBuffers[m_interactiveUniformIndex];
  if (!slot)
  {
    slot = DirectXDeviceHelpers::createUploadBuffer(device, sizeof(RKPathTracerUniforms));
    if (!slot)
      return fail("the frame's tracer uniforms could not be created");
  }
  ID3D12Resource *uniforms = slot.Get();
  m_interactiveUniformIndex =
      (m_interactiveUniformIndex + 1) % Dx12DeviceContext::kInflightFrameCount;

  const UINT samples = (std::max)(samplesThisFrame, 1u);

  RKPathTracerUniforms values;
  values.width = width;
  values.height = height;
  values.maximumBounces = settings.effectiveMaximumBounces();
  // A ray leaving a surface has to clear the surface it left, and how far that is depends on the
  // size of the scene.
  values.rayEpsilon = 1.0e-4f * geometry.sceneRadius();
  values.ambientOcclusionStrength = settings.ambientOcclusionStrength;
  values.sceneColorSwapsRedAndBlue =
      (context.backBufferFormat() == DXGI_FORMAT_B8G8R8A8_UNORM) ? 1u : 0u;
  // Zero is what tells the accumulate kernel to start the sums rather than add to them, which is
  // what makes each frame stand on its own.
  values.sampleOffset = 0;
  values.samplesPerDispatch = samples;
  values.accumulatedSamples = static_cast<float>(samples);
  values.seed = m_interactiveFrameCounter * 9781u + 1u;
  ++m_interactiveFrameCounter;
  DirectXDeviceHelpers::writeUploadBuffer(uniforms, &values, sizeof(values));

  const UINT groupsX = (width + kThreadGroupSize - 1) / kThreadGroupSize;
  const UINT groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;

  // One dispatch rather than the batches a still image is traced in: a frame has to be short enough
  // to keep the view responsive anyway, so it is already well inside what the watchdog allows.
  encodeAccumulate(commandList, geometry, frameConstantsAddress, lightConstantsAddress, uniforms,
                   groupsX, groupsY);

  if (!encodeResolve(commandList, context, geometry, uniforms, sceneColor, sceneDepth, groupsX,
                     groupsY, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
    return false;

  m_status = "traced a frame at " + std::to_string(samples) + " samples per pixel";
  return true;
}

bool DirectXPathTracerShader::render(Dx12DeviceContext &context,
                                     const DirectXPathTracerGeometry &geometry,
                                     D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
                                     D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress,
                                     ID3D12Resource *sceneColor, ID3D12Resource *sceneDepth,
                                     UINT width, UINT height, const Settings &settings)
{
  if (!m_ready) return fail("the path tracer is not ready to trace");
  if (width == 0 || height == 0) return fail("the image is empty");
  if (!sceneColor || !sceneDepth) return fail("there is no rasterized scene to composite over");
  if (!geometry.isValid()) return fail("there is no traceable geometry: " + geometry.status());
  if (!geometry.structureUniformBuffer()) return fail("the structure uniforms were not packed");

  ID3D12Device *device = context.device();
  if (!device) return fail("there is no device to trace with");
  if (!ensureBuffers(device, width, height)) return fail("the per-pixel buffers could not be created");
  if (!ensureCommandList(device)) return fail("the tracer command list could not be created");

  const UINT sampleCount = (std::max)(settings.sampleCount, 1u);
  const UINT batchSize = (std::max)(settings.samplesPerDispatch, 1u);
  const UINT groupsX = (width + kThreadGroupSize - 1) / kThreadGroupSize;
  const UINT groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;

  RKPathTracerUniforms uniforms;
  uniforms.width = width;
  uniforms.height = height;
  uniforms.maximumBounces = settings.effectiveMaximumBounces();
  // A ray leaving a surface has to clear the surface it left, and how far that is depends on the
  // size of the scene: an offset invisible across a protein would lift a shadow clean off a single
  // unit cell.
  uniforms.rayEpsilon = 1.0e-4f * geometry.sceneRadius();
  uniforms.ambientOcclusionStrength = settings.ambientOcclusionStrength;
  uniforms.accumulatedSamples = static_cast<float>(sampleCount);
  uniforms.sceneColorSwapsRedAndBlue =
      (context.backBufferFormat() == DXGI_FORMAT_B8G8R8A8_UNORM) ? 1u : 0u;

  // Traced in batches, one command list each, so that no single dispatch runs long enough for the
  // display driver to decide the device has stopped responding and reset it.
  for (UINT sampleOffset = 0; sampleOffset < sampleCount; sampleOffset += batchSize)
  {
    uniforms.sampleOffset = sampleOffset;
    uniforms.samplesPerDispatch = (std::min)(batchSize, sampleCount - sampleOffset);
    // Decorrelates the batches: without it every batch would trace the same paths and no amount of
    // them would converge.
    uniforms.seed = sampleOffset * 9781u + 1u;
    DirectXDeviceHelpers::writeUploadBuffer(m_uniformBuffer.Get(), &uniforms, sizeof(uniforms));

    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);

    encodeAccumulate(m_commandList.Get(), geometry, frameConstantsAddress, lightConstantsAddress,
                     m_uniformBuffer.Get(), groupsX, groupsY);

    if (!executeAndWait(context)) return fail("a batch of samples could not be submitted");
  }

  // The resolve reads what the batches summed. A wait on the queue has already happened, but the
  // barriers encodeResolve places are what make the reads-after-writes ordered for the driver.
  m_commandAllocator->Reset();
  m_commandList->Reset(m_commandAllocator.Get(), nullptr);

  // Left in a state the caller can copy the image away from.
  if (!encodeResolve(m_commandList.Get(), context, geometry, m_uniformBuffer.Get(), sceneColor,
                     sceneDepth, groupsX, groupsY, D3D12_RESOURCE_STATE_COPY_SOURCE))
    return false;

  if (!executeAndWait(context)) return fail("the resolve could not be submitted");

  m_status = "traced " + std::to_string(sampleCount) + " samples, "
             + describePrimaryRays(context, width * height);
  return true;
}

std::string DirectXPathTracerShader::describePrimaryRays(Dx12DeviceContext &context, UINT pixels)
{
  // What the primary rays of a still image found, which is the one thing a traced image that comes
  // out looking like the rasterized one cannot be asked about afterwards: whether the geometry was
  // missed, or found and then composited away. Read once per exported image.
  ID3D12Device *device = context.device();
  if (!device || !m_surfaceInfo || pixels == 0) return "nothing to report on";

  const UINT64 bytes = UINT64(pixels) * 16;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_READBACK;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&readback))))
    return "the surface buffer could not be read back";

  m_commandAllocator->Reset();
  m_commandList->Reset(m_commandAllocator.Get(), nullptr);
  transition(m_commandList.Get(), m_surfaceInfo.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_commandList->CopyBufferRegion(readback.Get(), 0, m_surfaceInfo.Get(), 0, bytes);
  transition(m_commandList.Get(), m_surfaceInfo.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!executeAndWait(context)) return "the surface buffer could not be copied";

  void *mapped = nullptr;
  if (FAILED(readback->Map(0, nullptr, &mapped)) || !mapped)
    return "the surface buffer could not be mapped";

  UINT hits = 0;
  float nearest = 1.0f;
  float farthest = 0.0f;
  const float *values = static_cast<const float *>(mapped);
  for (UINT pixel = 0; pixel < pixels; ++pixel)
  {
    // The depth a missed pixel is left at, the far plane, is the one depth a hit cannot report.
    const float depth = values[pixel * 4];
    if (depth >= 1.0f) continue;
    ++hits;
    nearest = (std::min)(nearest, depth);
    farthest = (std::max)(farthest, depth);
  }
  readback->Unmap(0, nullptr);

  if (hits == 0)
    return "no primary ray of " + std::to_string(pixels) + " pixels hit anything";

  char range[64] = {};
  std::snprintf(range, sizeof(range), "%.4f..%.4f", double(nearest), double(farthest));
  return std::to_string(hits) + " of " + std::to_string(pixels) + " pixels hit, at depth "
         + range;
}
