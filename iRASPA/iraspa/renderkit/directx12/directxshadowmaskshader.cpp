/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxshadowmaskshader.h"

#include "directxdevicehelpers.h"
#include "directxdxccompiler.h"
#include "directxpathtracerstringliterals.h"

namespace
{
  /// The root parameters, in the order the root signature declares them. Everything is a root view:
  /// the pass touches no descriptor heap, so it cannot be disturbed by the heaps the raster passes
  /// swap between themselves, and there is no heap to size or to run out of.
  enum RootParameter : UINT
  {
    frameConstants = 0,
    tracerConstants,
    lightConstants,
    accelerationStructure,
    structureUniforms,
    instances,
    spheres,
    cylinders,
    ribbonVertices,
    ribbonIndices,
    shadowMask,
    rootParameterCount
  };

  ComPtr<ID3D12RootSignature> createRootSignature(ID3D12Device *device, std::string &error)
  {
    D3D12_ROOT_PARAMETER params[rootParameterCount] = {};

    auto setConstantBuffer = [&](RootParameter slot, UINT shaderRegister) {
      params[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      params[slot].Descriptor.ShaderRegister = shaderRegister;
      params[slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    };

    auto setShaderResource = [&](RootParameter slot, UINT shaderRegister) {
      params[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
      params[slot].Descriptor.ShaderRegister = shaderRegister;
      params[slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    };

    setConstantBuffer(frameConstants, 0);
    setConstantBuffer(tracerConstants, 2);
    setConstantBuffer(lightConstants, 3);

    setShaderResource(accelerationStructure, 0);
    setShaderResource(structureUniforms, 1);
    setShaderResource(instances, 2);
    setShaderResource(spheres, 3);
    setShaderResource(cylinders, 4);
    setShaderResource(ribbonVertices, 5);
    setShaderResource(ribbonIndices, 6);

    params[shadowMask].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[shadowMask].Descriptor.ShaderRegister = 0;
    params[shadowMask].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = rootParameterCount;
    rootDesc.pParameters = params;
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
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(),
                                           signature->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature))))
    {
      error = "the root signature could not be created";
      return nullptr;
    }
    return rootSignature;
  }

  ComPtr<ID3D12Resource> createUnorderedAccessBuffer(ID3D12Device *device, UINT64 bytes,
                                                     D3D12_RESOURCE_STATES state)
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
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
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

DirectXShadowMaskShader::~DirectXShadowMaskShader()
{
  release();
}

bool DirectXShadowMaskShader::fail(const std::string &reason)
{
  m_status = reason;
  return false;
}

void DirectXShadowMaskShader::release()
{
  m_rootSignature.Reset();
  m_pipelineState.Reset();
  m_maskBuffer.Reset();
  m_allLitBuffer.Reset();
  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
    m_uniformBuffers[i].Reset();

  m_maskCapacity = 0;
  m_maskWidth = 0;
  m_maskHeight = 0;
  m_maskReadable = false;
  m_ready = false;
  m_status = "the shadow mask has not been initialized";
}

bool DirectXShadowMaskShader::createFallback(ID3D12Device *device)
{
  if (m_allLitBuffer) return true;
  if (!device) return false;

  // Written once from the CPU and read for the rest of the run, so an upload-heap buffer is the
  // whole of it: there is nothing to copy to a default heap for.
  const uint32_t allLit = 0xFFu;
  m_allLitBuffer = DirectXDeviceHelpers::createUploadBuffer(device, sizeof(allLit));
  if (!m_allLitBuffer) return false;

  DirectXDeviceHelpers::writeUploadBuffer(m_allLitBuffer.Get(), &allLit, sizeof(allLit));
  return true;
}

void DirectXShadowMaskShader::initialize(Dx12DeviceContext &context)
{
  release();

  ID3D12Device *device = context.device();
  if (!device)
  {
    fail("there is no device to trace with");
    return;
  }

  // The all-lit buffer is what keeps the raster root signature satisfied when nothing is traced, so
  // it is created whether or not this device can trace at all.
  if (!createFallback(device))
  {
    fail("the all-lit fallback buffer could not be created");
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

  std::string error;
  m_rootSignature = createRootSignature(device, error);
  if (!m_rootSignature)
  {
    fail("the shadow-mask root signature failed: " + error);
    return;
  }

  const std::string source = DirectXPathTracerStringLiterals::shadowMaskKernelSource();
  DirectXDxcCompiler::Result compiled =
      DirectXDxcCompiler::compile(source, L"shadowMaskKernel", L"cs_6_5");
  if (!compiled.succeeded())
  {
    DirectXDxcCompiler::logDiagnostics("shadowMaskKernel", compiled.diagnostics);
    fail("the shadow-mask kernel failed to compile");
    return;
  }
  if (!compiled.diagnostics.empty())
    DirectXDxcCompiler::logDiagnostics("shadowMaskKernel", compiled.diagnostics);

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = m_rootSignature.Get();
  psoDesc.CS = compiled.bytecode();
  if (FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
  {
    fail("the shadow-mask pipeline state could not be created");
    return;
  }

  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
  {
    m_uniformBuffers[i] = DirectXDeviceHelpers::createUploadBuffer(device, sizeof(RKPathTracerUniforms));
    if (!m_uniformBuffers[i])
    {
      fail("the shadow-mask constant buffers could not be created");
      return;
    }
  }

  m_ready = true;
  m_status = "ready to trace shadows";
}

bool DirectXShadowMaskShader::ensureMaskBuffer(ID3D12Device *device, UINT width, UINT height)
{
  const UINT needed = width * height;
  if (m_maskBuffer && needed <= m_maskCapacity) return true;

  // Grown to the high-water mark, so that dragging a window larger and back does not reallocate on
  // every size along the way. The buffer is only ever indexed by the size it was traced at.
  const UINT capacity = (std::max)(needed, m_maskCapacity);

  // Created in the state the raster passes read it in, so that a frame which fails before the
  // dispatch still leaves the resource in the state the next transition expects.
  m_maskBuffer = createUnorderedAccessBuffer(device, UINT64(capacity) * sizeof(uint32_t),
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!m_maskBuffer)
  {
    m_maskCapacity = 0;
    return false;
  }

  m_maskCapacity = capacity;
  m_maskReadable = false;
  return true;
}

bool DirectXShadowMaskShader::encode(ID3D12GraphicsCommandList4 *commandList,
                                     Dx12DeviceContext &context,
                                     const DirectXPathTracerGeometry &geometry,
                                     D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
                                     D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress, UINT width,
                                     UINT height)
{
  // Nothing traced this frame: the caller reports a mask size of zero and the raster passes light
  // every surface, which is what they did before any of this existed.
  m_maskWidth = 0;
  m_maskHeight = 0;

  if (!m_ready || !commandList) return fail("the shadow mask is not ready to trace");
  if (width == 0 || height == 0) return fail("the viewport is empty");
  if (!geometry.isValid()) return fail("there is no traceable geometry: " + geometry.status());
  if (!geometry.structureUniformBuffer()) return fail("the structure uniforms were not packed");

  ID3D12Device *device = context.device();
  if (!device) return fail("there is no device to trace with");
  if (!ensureMaskBuffer(device, width, height)) return fail("the shadow mask buffer could not be created");

  ID3D12Resource *uniformBuffer = m_uniformBuffers[context.frameIndex()].Get();
  if (!uniformBuffer) return fail("there is no constant buffer for this frame");

  RKPathTracerUniforms uniforms;
  uniforms.width = width;
  uniforms.height = height;
  // A shadow ray leaving a surface has to clear the surface it left, and how far that is depends on
  // the size of the scene: the same offset that is invisible across a protein would lift a shadow
  // clean off a single unit cell.
  uniforms.rayEpsilon = 1.0e-4f * geometry.sceneRadius();
  DirectXDeviceHelpers::writeUploadBuffer(uniformBuffer, &uniforms, sizeof(uniforms));

  transition(commandList, m_maskBuffer.Get(),
             m_maskReadable ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  commandList->SetComputeRootSignature(m_rootSignature.Get());
  commandList->SetPipelineState(m_pipelineState.Get());

  commandList->SetComputeRootConstantBufferView(frameConstants, frameConstantsAddress);
  commandList->SetComputeRootConstantBufferView(tracerConstants,
                                                uniformBuffer->GetGPUVirtualAddress());
  commandList->SetComputeRootConstantBufferView(lightConstants, lightConstantsAddress);

  commandList->SetComputeRootShaderResourceView(
      accelerationStructure, geometry.topLevelAccelerationStructure()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      structureUniforms, geometry.structureUniformBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      instances, geometry.instanceDataBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(spheres,
                                                geometry.sphereBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(cylinders,
                                                geometry.cylinderBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      ribbonVertices, geometry.ribbonVertexBuffer()->GetGPUVirtualAddress());
  commandList->SetComputeRootShaderResourceView(
      ribbonIndices, geometry.ribbonIndexBuffer()->GetGPUVirtualAddress());

  commandList->SetComputeRootUnorderedAccessView(shadowMask,
                                                 m_maskBuffer->GetGPUVirtualAddress());

  const UINT groupSize = 8; // matches [numthreads(8, 8, 1)]
  commandList->Dispatch((width + groupSize - 1) / groupSize, (height + groupSize - 1) / groupSize, 1);

  transition(commandList, m_maskBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_maskReadable = true;

  m_maskWidth = width;
  m_maskHeight = height;
  m_status = "traced the shadow mask";
  return true;
}
