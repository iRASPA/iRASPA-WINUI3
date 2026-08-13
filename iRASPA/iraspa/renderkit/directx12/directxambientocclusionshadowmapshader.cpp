/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxambientocclusionshadowmapshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "ribbonaolayout.h"
#include "rkcamera.h"
#include <mathkit.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
constexpr UINT kShadowMapSize = 2048;
constexpr UINT kWhiteSrvIndex = 0;
// Half-float encoding of 1.0
constexpr uint16_t kHalfFloatOne = 0x3C00;

void transition(ID3D12GraphicsCommandList *cmd, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
  if (!resource || before == after)
    return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmd->ResourceBarrier(1, &barrier);
}

ComPtr<ID3D12Resource> createTexture2D(ID3D12Device *device, UINT width, UINT height,
                                       DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                       D3D12_RESOURCE_STATES initialState,
                                       const D3D12_CLEAR_VALUE *clearValue = nullptr)
{
  ComPtr<ID3D12Resource> texture;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = flags;

  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             initialState, clearValue, IID_PPV_ARGS(&texture))))
  {
    return nullptr;
  }
  return texture;
}
} // namespace

DirectXAmbientOcclusionShadowMapShader::DirectXAmbientOcclusionShadowMapShader(
    DirectXAtomSphereShader &atomSphere,
    DirectXAtomOrthographicImposterShader &orthoImposter)
  : _atomSphereShader(atomSphere),
    _atomOrthographicImposterShader(orthoImposter)
{
  _cache.setMaxCost(128);
}

DirectXAmbientOcclusionShadowMapShader::~DirectXAmbientOcclusionShadowMapShader()
{
  _cache.clear();
  if (_fenceEvent)
  {
    CloseHandle(_fenceEvent);
    _fenceEvent = nullptr;
  }
}

void DirectXAmbientOcclusionShadowMapShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAmbientOcclusionShadowMapShader::initialize(ID3D12Device *device, ID3D12CommandQueue *queue)
{
  if (!device || !queue)
    return;

  _srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  _rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator))))
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create command allocator";
    return;
  }
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(),
                                        nullptr, IID_PPV_ARGS(&_commandList))))
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create command list";
    return;
  }
  _commandList->Close();

  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create fence";
    return;
  }
  _fenceValue = 0;
  _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  createGenerationPipelines(device);
  ensureTransientShadowResources(device);

  // 1x1 white R16_FLOAT fallback (value 1.0)
  {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_srvHeap))))
    {
      std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create initial SRV heap";
      return;
    }

    _whiteTexture = createTexture2D(device, 1, 1, DXGI_FORMAT_R16_FLOAT,
                                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!_whiteTexture)
      return;

    const uint16_t white = kHalfFloatOne;
    const UINT64 uploadSize = DirectXDeviceHelpers::alignedCBSize(sizeof(white) + 256);
    ComPtr<ID3D12Resource> upload = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 total = 0;
    D3D12_RESOURCE_DESC texDesc = _whiteTexture->GetDesc();
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

    uint8_t *mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    upload->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
    std::memcpy(mapped + footprint.Offset, &white, sizeof(white));
    upload->Unmap(0, nullptr);

    resetCommandList();
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = _whiteTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    _commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    transition(_commandList.Get(), _whiteTexture.Get(),
               D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    executeAndWait(queue);

    _whiteSrvCpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
    _whiteSrvGpu = _srvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(_whiteTexture.Get(), &srv, _whiteSrvCpu);
  }

  _initialized = true;
}

void DirectXAmbientOcclusionShadowMapShader::createGenerationPipelines(ID3D12Device *device)
{
  // Root signature: b1 structure, b2 shadow, b0 root constant weight, t0 shadow SRV, s0 comparison sampler
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;
  srvRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_DESCRIPTOR_RANGE samplerRange = {};
  samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  samplerRange.NumDescriptors = 1;
  samplerRange.BaseShaderRegister = 0;
  samplerRange.RegisterSpace = 0;
  samplerRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER params[4] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 1;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[1].Descriptor.ShaderRegister = 1;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[2].Descriptor.ShaderRegister = 2;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].DescriptorTable.NumDescriptorRanges = 1;
  params[3].DescriptorTable.pDescriptorRanges = &srvRange;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_PARAMETER samplerParam = {};
  samplerParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  samplerParam.DescriptorTable.NumDescriptorRanges = 1;
  samplerParam.DescriptorTable.pDescriptorRanges = &samplerRange;
  samplerParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_PARAMETER allParams[5] = { params[0], params[1], params[2], params[3], samplerParam };

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 5;
  rootDesc.pParameters = allParams;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
  {
    if (error)
      std::cerr << "AO gen root sig:" << static_cast<const char *>(error->GetBufferPointer());
    return;
  }
  if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                         IID_PPV_ARGS(&_genRootSignature))))
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create gen root signature";
    return;
  }

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  // Shadow map PSO (depth-only)
  {
    ComPtr<ID3DBlob> vs = compileShader(_vertexShadowMapShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_pixelShadowMapShaderSource, "PSMain", "ps_5_0");
    if (!vs || !ps)
      return;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _genRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_shadowPso))))
    {
      std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create shadow PSO";
      return;
    }
  }

  // AO accumulate PSO
  {
    ComPtr<ID3DBlob> vs = compileShader(_vertexAmbientOcclusionShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_pixelAmbientOcclusionShaderSource, "PSMain", "ps_5_0");
    if (!vs || !ps)
      return;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _genRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_aoAccumulatePso))))
    {
      std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to create AO accumulate PSO";
      return;
    }
  }

  // Generation SRV heap (shadow map) + comparison sampler heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_genSrvHeap));

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {};
    samplerDesc.NumDescriptors = 1;
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&_genSamplerHeap));

    D3D12_SAMPLER_DESC samp = {};
    samp.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    device->CreateSampler(&samp, _genSamplerHeap->GetCPUDescriptorHandleForHeapStart());
  }

  {
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&_dsvHeap));
    _shadowDsvCpu = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
  }
}

void DirectXAmbientOcclusionShadowMapShader::ensureTransientShadowResources(ID3D12Device *device)
{
  if (_shadowDepthTexture)
    return;

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_D32_FLOAT;
  clearValue.DepthStencil.Depth = 1.0f;

  _shadowDepthTexture = createTexture2D(device, kShadowMapSize, kShadowMapSize,
                                        DXGI_FORMAT_R32_TYPELESS,
                                        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                                        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue);
  if (!_shadowDepthTexture)
    return;

  D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
  dsv.Format = DXGI_FORMAT_D32_FLOAT;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  device->CreateDepthStencilView(_shadowDepthTexture.Get(), &dsv, _shadowDsvCpu);

  if (_genSrvHeap)
  {
    _shadowSrvCpu = _genSrvHeap->GetCPUDescriptorHandleForHeapStart();
    _shadowSrvGpu = _genSrvHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(_shadowDepthTexture.Get(), &srv, _shadowSrvCpu);
  }
}

void DirectXAmbientOcclusionShadowMapShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXAmbientOcclusionShadowMapShader::deleteBuffers()
{
  _aoResources.clear();
  _totalStructureSlots = 0;
}

void DirectXAmbientOcclusionShadowMapShader::generateBuffers()
{
  _aoResources.resize(_renderStructures.size());
  _totalStructureSlots = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    _aoResources[i].resize(_renderStructures[i].size());
    _totalStructureSlots += static_cast<UINT>(_renderStructures[i].size());
  }
}

UINT DirectXAmbientOcclusionShadowMapShader::srvIndex(size_t i, size_t j) const
{
  UINT index = 1; // 0 is white fallback
  for (size_t ii = 0; ii < i; ++ii)
    index += static_cast<UINT>(_renderStructures[ii].size());
  index += static_cast<UINT>(j);
  return index;
}

void DirectXAmbientOcclusionShadowMapShader::invalidateCachedAmbientOcclusionTexture(
    std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  for (const std::shared_ptr<RKRenderObject> &structure : structures)
    _cache.remove(structure.get());
}

bool DirectXAmbientOcclusionShadowMapShader::anyStructureNeedsAmbientOcclusion() const
{
  for (const auto &scene : _renderStructures)
  {
    for (const auto &object : scene)
    {
      if (!object || !object->isVisible())
        continue;
      if (auto *atoms = dynamic_cast<RKRenderAtomSource *>(object.get()))
      {
        if (atoms->atomAmbientOcclusion())
          return true;
      }
      if (auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(object.get()))
      {
        if (ribbon->drawRibbon() && ribbon->ribbonAmbientOcclusion()
            && ribbon->ribbonNumberOfVertices() > 0)
          return true;
      }
    }
  }
  return false;
}

void DirectXAmbientOcclusionShadowMapShader::reloadData(ID3D12Device *device, ID3D12CommandQueue *queue,
                                                        std::shared_ptr<RKRenderDataSource> dataSource,
                                                        RKRenderQuality quality)
{
  if (!_initialized || !device || !queue)
    return;
  // Shaders already sample the white fallback when a structure has no bake; there is nothing to
  // allocate or accumulate when nobody asked for occlusion.
  if (!anyStructureNeedsAmbientOcclusion())
    return;

  try
  {
    adjustAmbientOcclusionTextureSize(device);
    if (_ribbonAo)
    {
      _ribbonAo->ensureGenerationPipelines(device, _genRootSignature.Get());
      _ribbonAo->prepareAtlases(device);
      _ribbonAo->uploadCachedAtlases(device, queue);
    }
    updateAmbientOcclusionTextures(device, queue, dataSource, quality);
  }
  catch (const std::exception &ex)
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader::reloadData failed:" << ex.what();
  }
  catch (...)
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader::reloadData failed (unknown)";
  }
}

void DirectXAmbientOcclusionShadowMapShader::adjustAmbientOcclusionTextureSize(ID3D12Device *device)
{
  if (!device)
    return;

  const int maxSize = 16384;
  const UINT neededDescriptors = std::max(1u, 1u + _totalStructureSlots);

  D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
  srvDesc.NumDescriptors = neededDescriptors;
  srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  _srvHeap.Reset();
  if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_srvHeap))))
  {
    std::cerr << "DirectXAmbientOcclusionShadowMapShader: failed to recreate SRV heap";
    return;
  }

  // Rebind white fallback at slot 0
  _whiteSrvCpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
  _whiteSrvGpu = _srvHeap->GetGPUDescriptorHandleForHeapStart();
  if (_whiteTexture)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(_whiteTexture.Get(), &srv, _whiteSrvCpu);
  }

  D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
  rtvDesc.NumDescriptors = std::max(1u, _totalStructureSlots);
  rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  _rtvHeap.Reset();
  device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&_rtvHeap));

  UINT flat = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j, ++flat)
    {
      AoStructureResources &res = _aoResources[i][j];
      res = AoStructureResources{};

      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!source || !source->atomAmbientOcclusion() || !_renderStructures[i][j]->isVisible())
        continue;

      const size_t numberOfAtoms = source->renderAtoms().size();
      source->setAtomAmbientOcclusionTextureSize(
          RKAmbientOcclusionSizing::maxTextureSize(int(numberOfAtoms), maxSize));

      source->setAtomAmbientOcclusionPatchNumber(int(std::sqrt(double(numberOfAtoms))) + 1);
      source->setAtomAmbientOcclusionPatchSize(source->atomAmbientOcclusionTextureSize() /
                                               source->atomAmbientOcclusionPatchNumber());

      const int texSize = source->atomAmbientOcclusionTextureSize();
      D3D12_CLEAR_VALUE clearValue = {};
      clearValue.Format = DXGI_FORMAT_R16_FLOAT;
      clearValue.Color[0] = 0.0f;

      res.texture = createTexture2D(device, static_cast<UINT>(texSize), static_cast<UINT>(texSize),
                                    DXGI_FORMAT_R16_FLOAT,
                                    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue);
      res.textureSize = texSize;
      if (!res.texture)
        continue;

      D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
      rtvCpu.ptr += SIZE_T(flat) * _rtvDescriptorSize;
      D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
      rtv.Format = DXGI_FORMAT_R16_FLOAT;
      rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      device->CreateRenderTargetView(res.texture.Get(), &rtv, rtvCpu);

      D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
      srvCpu.ptr += SIZE_T(srvIndex(i, j)) * _srvDescriptorSize;
      D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
      srv.Format = DXGI_FORMAT_R16_FLOAT;
      srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(res.texture.Get(), &srv, srvCpu);
      res.valid = true;
    }
  }
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXAmbientOcclusionShadowMapShader::aoSrv(size_t i, size_t j) const
{
  if (!_srvHeap)
    return _whiteSrvGpu;
  if (!hasAo(i, j))
    return _whiteSrvGpu;

  D3D12_GPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(srvIndex(i, j)) * _srvDescriptorSize;
  return handle;
}

bool DirectXAmbientOcclusionShadowMapShader::hasAo(size_t i, size_t j) const
{
  if (i >= _aoResources.size() || j >= _aoResources[i].size())
    return false;
  if (!_aoResources[i][j].valid || !_aoResources[i][j].texture)
    return false;

  auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
  if (!source || !source->atomAmbientOcclusion() || !_renderStructures[i][j]->isVisible())
    return false;
  return true;
}

void DirectXAmbientOcclusionShadowMapShader::waitGpu(ID3D12CommandQueue *queue)
{
  const UINT64 fenceToWait = ++_fenceValue;
  queue->Signal(_fence.Get(), fenceToWait);
  if (_fence->GetCompletedValue() < fenceToWait)
  {
    _fence->SetEventOnCompletion(fenceToWait, _fenceEvent);
    WaitForSingleObject(_fenceEvent, INFINITE);
  }
}

void DirectXAmbientOcclusionShadowMapShader::resetCommandList()
{
  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);
}

void DirectXAmbientOcclusionShadowMapShader::executeAndWait(ID3D12CommandQueue *queue)
{
  _commandList->Close();
  ID3D12CommandList *lists[] = { _commandList.Get() };
  queue->ExecuteCommandLists(1, lists);
  waitGpu(queue);
}

void DirectXAmbientOcclusionShadowMapShader::uploadAoTextureData(
    ID3D12Device *device, ID3D12CommandQueue *queue,
    size_t i, size_t j, const std::vector<uint16_t> &data, int textureSize)
{
  AoStructureResources &res = _aoResources[i][j];
  if (!res.texture || data.empty())
    return;

  D3D12_RESOURCE_DESC texDesc = res.texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSize = 0;
  UINT64 total = 0;
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

  ComPtr<ID3D12Resource> upload = DirectXDeviceHelpers::createUploadBuffer(device, total);
  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  upload->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
  for (UINT row = 0; row < numRows; ++row)
  {
    const size_t srcOffset = size_t(row) * size_t(textureSize) * sizeof(uint16_t);
    std::memcpy(mapped + footprint.Offset + row * footprint.Footprint.RowPitch,
                data.data() + srcOffset / sizeof(uint16_t),
                size_t(textureSize) * sizeof(uint16_t));
  }
  upload->Unmap(0, nullptr);

  resetCommandList();
  transition(_commandList.Get(), res.texture.Get(),
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = res.texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;
  _commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  transition(_commandList.Get(), res.texture.Get(),
             D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  executeAndWait(queue);
}

void DirectXAmbientOcclusionShadowMapShader::updateAmbientOcclusionTextures(
    ID3D12Device *device, ID3D12CommandQueue *queue,
    std::shared_ptr<RKRenderDataSource> dataSource, RKRenderQuality quality)
{
  if (!dataSource || !_initialized || !_shadowPso || !_aoAccumulatePso)
    return;
  if (!anyStructureNeedsAmbientOcclusion())
    return;

  ensureTransientShadowResources(device);
  if (!_shadowDepthTexture)
    return;

  const UINT structureStride = DirectXDeviceHelpers::alignedCBSize(sizeof(RKStructureUniforms));
  const UINT shadowStride = DirectXDeviceHelpers::alignedCBSize(sizeof(RKShadowUniforms));

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    // The ribbons of the scene are occluders for every bake in it, whichever structure is the target.
    const std::vector<DirectXRibbonAmbientOcclusionShader::Occluder> ribbonOccluders =
        _ribbonAo ? _ribbonAo->occluders(i)
                  : std::vector<DirectXRibbonAmbientOcclusionShader::Occluder>{};
    ID3D12PipelineState *ribbonShadowPso = _ribbonAo ? _ribbonAo->shadowPipelineState() : nullptr;
    const bool ribbonsOcclude = ribbonShadowPso && !ribbonOccluders.empty();

    // A ribbon that has to be rebaked has changed shape, and the atom textures of the scene were
    // baked against the shape it had, so they go with it.
    if (_ribbonAo)
    {
      bool ribbonRebakes = false;
      for (size_t j = 0; j < _renderStructures[i].size() && !ribbonRebakes; ++j)
        ribbonRebakes = _ribbonAo->needsBake(i, j);
      if (ribbonRebakes)
      {
        for (size_t j = 0; j < _renderStructures[i].size(); ++j)
          _cache.remove(_renderStructures[i][j].get());
      }
    }

    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      RKRenderObject *renderStructure = dynamic_cast<RKRenderObject *>(_renderStructures[i][j].get());
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!renderStructure || !source)
        continue;
      if (!renderStructure->cell())
        continue;
      if (!_genSrvHeap || !_genSamplerHeap || !_genRootSignature)
        continue;

      bool bakeAtoms = source->atomAmbientOcclusion() && _renderStructures[i][j]->isVisible() &&
                       _aoResources[i][j].valid && _aoResources[i][j].texture;
      if (bakeAtoms && _cache.contains(_renderStructures[i][j].get()))
      {
        std::vector<uint16_t> *textureData = _cache.object(_renderStructures[i][j].get());
        if (textureData)
          uploadAoTextureData(device, queue, i, j, *textureData, source->atomAmbientOcclusionTextureSize());
        bakeAtoms = false;
      }

      bool bakeRibbon = _ribbonAo && _ribbonAo->needsBake(i, j);
      if (!bakeAtoms && !bakeRibbon)
        continue;

      double4x4 modelMatrix =
          double4x4::AffinityMatrixToTransformationAroundArbitraryPointWithTranslation(
              double4x4(renderStructure->orientation()),
              renderStructure->cell()->boundingBox().center(),
              renderStructure->origin());

      std::vector<RKStructureUniforms> structureUniforms;
      structureUniforms.reserve(_renderStructures[i].size());
      for (size_t k = 0; k < _renderStructures[i].size(); ++k)
      {
        structureUniforms.push_back(
            RKStructureUniforms(int(i), int(k), _renderStructures[i][k], double4x4::inverse(modelMatrix)));
      }

      ComPtr<ID3D12Resource> structureCB =
          DirectXDeviceHelpers::createUploadBuffer(
              device, std::max<UINT64>(1, UINT64(structureUniforms.size()) * structureStride));
      {
        uint8_t *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        structureCB->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
        for (size_t k = 0; k < structureUniforms.size(); ++k)
          std::memcpy(mapped + k * structureStride, &structureUniforms[k], sizeof(RKStructureUniforms));
        structureCB->Unmap(0, nullptr);
      }

      // A structure that shows no atoms is baked in the scene's own frame, the frame its ribbon mesh
      // is already expressed in; one that shows atoms follows them into the target's frame so that
      // the two kinds of geometry meet in the same depth map. This is the choice Cocoa makes.
      const bool ribbonInSceneFrame = _renderStructures[i][j]->isPresentedAsRibbonOnly();
      ComPtr<ID3D12Resource> ribbonStructureCB = structureCB;
      if (bakeRibbon && ribbonInSceneFrame)
      {
        std::vector<RKStructureUniforms> renderUniforms;
        renderUniforms.reserve(_renderStructures[i].size());
        for (size_t k = 0; k < _renderStructures[i].size(); ++k)
          renderUniforms.push_back(RKStructureUniforms(i, k, _renderStructures[i][k]));

        ribbonStructureCB = DirectXDeviceHelpers::createUploadBuffer(
            device, std::max<UINT64>(1, UINT64(renderUniforms.size()) * structureStride));
        uint8_t *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        ribbonStructureCB->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
        for (size_t k = 0; k < renderUniforms.size(); ++k)
          std::memcpy(mapped + k * structureStride, &renderUniforms[k], sizeof(RKStructureUniforms));
        ribbonStructureCB->Unmap(0, nullptr);
      }

      SKBoundingBox boundingBox = dataSource->renderBoundingBox();
      double largestRadius = boundingBox.boundingSphereRadius();
      if (!(largestRadius > 0.0) || !std::isfinite(largestRadius))
        largestRadius = 1.0;
      double3 centerOfScene = boundingBox.minimum() + (boundingBox.maximum() - boundingBox.minimum()) * 0.5;
      double3 eye = double3(centerOfScene.x, centerOfScene.y, centerOfScene.z + largestRadius);

      const double extentX = std::fabs(boundingBox.maximum().x - boundingBox.minimum().x);
      const double extentY = std::fabs(boundingBox.maximum().y - boundingBox.minimum().y);
      double boundingBoxAspectRatio = (extentY > 1e-8) ? (extentX / extentY) : 1.0;
      if (!(boundingBoxAspectRatio > 0.0) || !std::isfinite(boundingBoxAspectRatio))
        boundingBoxAspectRatio = 1.0;

      double left, right, top, bottom;
      if (boundingBoxAspectRatio < 1.0)
      {
        left = -largestRadius / boundingBoxAspectRatio;
        right = largestRadius / boundingBoxAspectRatio;
        top = largestRadius / boundingBoxAspectRatio;
        bottom = -largestRadius / boundingBoxAspectRatio;
      }
      else
      {
        left = -largestRadius;
        right = largestRadius;
        top = largestRadius;
        bottom = -largestRadius;
      }

      const double near1 = 0.0;
      const double far1 = 2.0 * largestRadius;

      // Cocoa: picture uses Data1992; the live view always uses Data300+Data60 (360).
      const int maxk = (quality == RKRenderQuality::picture) ? 1992 : 360;

      std::vector<RKShadowUniforms> shadowMapFrameUniforms;
      shadowMapFrameUniforms.reserve(size_t(maxk));
      for (int k = 0; k < maxk; ++k)
      {
        simd_quatd smallChangeQ = simd_quatd::smallRandomQuaternion(0.5 * 10.0 * M_PI / 180.0);
        simd_quatd q = smallChangeQ * simd_quatd::ambientOcclusionDirection(k, maxk);

        double4x4 currentModelMatrix =
            double4x4::AffinityMatrixToTransformationAroundArbitraryPoint(double4x4(q), centerOfScene);
        double4x4 viewMatrix = RKCamera::GluLookAt(eye, centerOfScene, double3(0, 1, 0));
        // Keep OpenGL orthographic projection (no Y flip). RKShadowUniforms already applies
        // the OpenGL NDC→[0,1] depth bias (0.5*z+0.5); flipping Y here breaks that pairing.
        double4x4 projectionMatrix =
            RKCamera::glFrustumfOrthographic(left, right, bottom, top, near1, far1);
        shadowMapFrameUniforms.push_back(RKShadowUniforms(projectionMatrix, viewMatrix, currentModelMatrix));
      }

      ComPtr<ID3D12Resource> shadowCB =
          DirectXDeviceHelpers::createUploadBuffer(device, UINT64(maxk) * shadowStride);
      {
        uint8_t *mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        shadowCB->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
        for (int k = 0; k < maxk; ++k)
          std::memcpy(mapped + size_t(k) * shadowStride, &shadowMapFrameUniforms[size_t(k)],
                      sizeof(RKShadowUniforms));
        shadowCB->Unmap(0, nullptr);
      }

      const int textureSize = source->atomAmbientOcclusionTextureSize();
      AoStructureResources &aoRes = _aoResources[i][j];
      UINT flat = 0;
      for (size_t ii = 0; ii < i; ++ii)
        flat += static_cast<UINT>(_renderStructures[ii].size());
      flat += static_cast<UINT>(j);

      D3D12_CPU_DESCRIPTOR_HANDLE aoRtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
      aoRtv.ptr += SIZE_T(flat) * _rtvDescriptorSize;

      if (bakeRibbon && !_ribbonAo->beginBake(device, queue, i, j))
        bakeRibbon = false;
      if (!bakeAtoms && !bakeRibbon)
        continue;

      // When the target shows atoms as well as a ribbon, atom imposters go into the depth map —
      // including those of a ribbon-only neighbour, which is the allowance Cocoa makes. Ribbon-only
      // targets leave them out, or the ribbon would be shaded by the atoms it replaces.
      const bool includeAtomOccluders =
          bakeRibbon && !_renderStructures[i][j]->isPresentedAsRibbonOnly();

      // Cocoa records every direction into one command buffer and waits once, which Metal is happy to
      // run for as long as it takes. Windows is not: the display driver kills any single submission
      // that outlives the watchdog (2 s by default) and takes the device down with it, and a protein
      // the size of 1aon passes that within a handful of its 360 directions. The directions are
      // therefore submitted in batches. They still run back-to-back on one queue, in order, blending
      // into the same accumulation targets with the same per-direction weights, so the bake that comes
      // out is the same one Cocoa produces.
      const int directionsPerSubmit = [&]() -> int
      {
        uint64_t primitivesPerDirection = 0;
        for (size_t l = 0; l < _renderStructures[i].size(); ++l)
        {
          if (!_renderStructures[i][l]->isVisible() || !_atomSphereShader.isInstanceReady(i, l))
            continue;
          auto *occluderSource = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][l].get());
          if (!occluderSource || !occluderSource->drawAtoms())
            continue;
          if (_renderStructures[i][l]->isPresentedAsRibbonOnly() && !includeAtomOccluders)
            continue;
          primitivesPerDirection += 2ull * _atomSphereShader.instanceCount(i, l);
        }
        if (ribbonsOcclude)
        {
          for (const DirectXRibbonAmbientOcclusionShader::Occluder &occluder : ribbonOccluders)
            for (const RKRibbonChainDrawRange &range : occluder.ranges)
              primitivesPerDirection += uint64_t(std::max(range.indexCount, 0)) / 3ull;
        }
        if (bakeAtoms)
          primitivesPerDirection += 2ull * _atomSphereShader.instanceCount(i, j);
        if (bakeRibbon)
        {
          if (auto *target = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get()))
            primitivesPerDirection += uint64_t(std::max(target->ribbonNumberOfIndices(), 0)) / 3ull;
        }
        if (primitivesPerDirection == 0)
          return maxk;
        // Every submission costs a fence wait, so the batch is made as large as the watchdog safely
        // allows rather than as small as possible. The budget below is about a quarter second on the
        // hardware this was measured on, which leaves room for a machine several times slower to still
        // come in under the two second limit. Small scenes divide out to the full 360 and stay on a
        // single submission, exactly as before.
        const uint64_t primitiveBudgetPerSubmit = 256ull * 1000ull * 1000ull;
        const uint64_t perSubmit = primitiveBudgetPerSubmit / primitivesPerDirection;
        return int(std::clamp<uint64_t>(perSubmit, 1, uint64_t(maxk)));
      }();

      resetCommandList();
      if (bakeAtoms)
      {
        transition(_commandList.Get(), aoRes.texture.Get(),
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        _commandList->ClearRenderTargetView(aoRtv, clearColor, 0, nullptr);
      }

      for (int kBatch = 0; kBatch < maxk; kBatch += directionsPerSubmit)
      {
        // The first batch keeps the command list the clear above was recorded into.
        if (kBatch > 0)
          resetCommandList();

        _commandList->SetGraphicsRootSignature(_genRootSignature.Get());

        ID3D12DescriptorHeap *heaps[] = { _genSrvHeap.Get(), _genSamplerHeap.Get() };
        _commandList->SetDescriptorHeaps(2, heaps);
        _commandList->SetGraphicsRootDescriptorTable(3, _shadowSrvGpu);
        _commandList->SetGraphicsRootDescriptorTable(4, _genSamplerHeap->GetGPUDescriptorHandleForHeapStart());

        D3D12_RESOURCE_STATES shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        const int kEnd = std::min(kBatch + directionsPerSubmit, maxk);
        for (int k = kBatch; k < kEnd; ++k)
        {
          // Shadow pass
          if (shadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
          {
            transition(_commandList.Get(), _shadowDepthTexture.Get(), shadowState,
                       D3D12_RESOURCE_STATE_DEPTH_WRITE);
            shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
          }

          _commandList->OMSetRenderTargets(0, nullptr, FALSE, &_shadowDsvCpu);
          _commandList->ClearDepthStencilView(_shadowDsvCpu, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

          D3D12_VIEWPORT shadowVp = {};
          shadowVp.Width = float(kShadowMapSize);
          shadowVp.Height = float(kShadowMapSize);
          shadowVp.MinDepth = 0.0f;
          shadowVp.MaxDepth = 1.0f;
          _commandList->RSSetViewports(1, &shadowVp);
          D3D12_RECT shadowSc = {0, 0, LONG(kShadowMapSize), LONG(kShadowMapSize)};
          _commandList->RSSetScissorRects(1, &shadowSc);

          _commandList->SetPipelineState(_shadowPso.Get());
          _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
          _commandList->SetGraphicsRootConstantBufferView(
              2, shadowCB->GetGPUVirtualAddress() + D3D12_GPU_VIRTUAL_ADDRESS(k) * shadowStride);

          for (size_t l = 0; l < _renderStructures[i].size(); ++l)
          {
            if (!_renderStructures[i][l]->isVisible() || !_atomSphereShader.isInstanceReady(i, l))
              continue;
            if (!_atomOrthographicImposterShader.isQuadReady())
              continue;
            // Atom AO on the occluder only decides whether that structure receives a bake of its
            // own; the spheres still shade everyone else's bake, and a ribbon's, whenever atoms are
            // drawn. Ribbon-only structures keep their (hidden) atoms out unless the target itself
            // stands on atoms, which is the Cocoa allowance behind includeAtomOccluders.
            auto *occluderSource = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][l].get());
            if (!occluderSource || !occluderSource->drawAtoms())
              continue;
            if (_renderStructures[i][l]->isPresentedAsRibbonOnly() && !includeAtomOccluders)
              continue;

            _commandList->SetGraphicsRootConstantBufferView(
                1, structureCB->GetGPUVirtualAddress() + D3D12_GPU_VIRTUAL_ADDRESS(l) * structureStride);

            D3D12_VERTEX_BUFFER_VIEW views[2] = {
              _atomOrthographicImposterShader.quadVbv(),
              _atomSphereShader.instanceVbv(i, l)
            };
            D3D12_INDEX_BUFFER_VIEW ibv = _atomOrthographicImposterShader.quadIbv();
            _commandList->IASetVertexBuffers(0, 2, views);
            _commandList->IASetIndexBuffer(&ibv);
            _commandList->DrawIndexedInstanced(_atomOrthographicImposterShader.quadIndexCount(),
                                               _atomSphereShader.instanceCount(i, l), 0, 0, 0);
          }

          // The ribbons of the scene into the same depth map, so a ribbon shades the atoms around it
          // and is shaded by them in turn.
          if (ribbonsOcclude)
          {
            _commandList->SetPipelineState(ribbonShadowPso);
            _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            for (const DirectXRibbonAmbientOcclusionShader::Occluder &occluder : ribbonOccluders)
            {
              _commandList->SetGraphicsRootConstantBufferView(
                  1, ribbonStructureCB->GetGPUVirtualAddress() +
                         D3D12_GPU_VIRTUAL_ADDRESS(occluder.structureIndex) * structureStride);
              _commandList->IASetVertexBuffers(0, 1, &occluder.vbv);
              _commandList->IASetIndexBuffer(&occluder.ibv);
              for (const RKRibbonChainDrawRange &range : occluder.ranges)
              {
                if (range.indexCount > 0)
                  _commandList->DrawIndexedInstanced(UINT(range.indexCount), 1, UINT(range.indexStart), 0, 0);
              }
            }
          }

          // AO accumulate
          transition(_commandList.Get(), _shadowDepthTexture.Get(),
                     D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
          shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

          const float weight = simd_quatd::ambientOcclusionBlendWeight(k, maxk);
          const D3D12_GPU_VIRTUAL_ADDRESS shadowCBV =
              shadowCB->GetGPUVirtualAddress() + D3D12_GPU_VIRTUAL_ADDRESS(k) * shadowStride;

          if (bakeAtoms)
          {
            _commandList->OMSetRenderTargets(1, &aoRtv, FALSE, nullptr);
            D3D12_VIEWPORT aoVp = {};
            aoVp.Width = float(textureSize);
            aoVp.Height = float(textureSize);
            aoVp.MinDepth = 0.0f;
            aoVp.MaxDepth = 1.0f;
            _commandList->RSSetViewports(1, &aoVp);
            D3D12_RECT aoSc = {0, 0, LONG(textureSize), LONG(textureSize)};
            _commandList->RSSetScissorRects(1, &aoSc);

            _commandList->SetPipelineState(_aoAccumulatePso.Get());
            _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            _commandList->SetGraphicsRoot32BitConstants(0, 1, &weight, 0);
            _commandList->SetGraphicsRootConstantBufferView(2, shadowCBV);
            _commandList->SetGraphicsRootConstantBufferView(
                1, structureCB->GetGPUVirtualAddress() + D3D12_GPU_VIRTUAL_ADDRESS(j) * structureStride);

            if (_renderStructures[i][j]->isVisible() && _atomSphereShader.isInstanceReady(i, j)
                && _atomOrthographicImposterShader.isQuadReady())
            {
              D3D12_VERTEX_BUFFER_VIEW views[2] = {
                _atomOrthographicImposterShader.quadVbv(),
                _atomSphereShader.instanceVbv(i, j)
              };
              D3D12_INDEX_BUFFER_VIEW ibv = _atomOrthographicImposterShader.quadIbv();
              _commandList->IASetVertexBuffers(0, 2, views);
              _commandList->IASetIndexBuffer(&ibv);
              _commandList->DrawIndexedInstanced(_atomOrthographicImposterShader.quadIndexCount(),
                                                 _atomSphereShader.instanceCount(i, j), 0, 0, 0);
            }
          }

          if (bakeRibbon)
          {
            _ribbonAo->recordAccumulate(
                _commandList.Get(), weight, shadowCBV,
                ribbonStructureCB->GetGPUVirtualAddress() +
                    D3D12_GPU_VIRTUAL_ADDRESS(j) * structureStride);
          }
        }

        if (shadowState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
        {
          transition(_commandList.Get(), _shadowDepthTexture.Get(), shadowState,
                     D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        executeAndWait(queue);
      }

      if (bakeRibbon)
        _ribbonAo->finishBake(device, queue, i, j);

      if (!bakeAtoms)
        continue;

      // Readback AO texture into cache
      D3D12_RESOURCE_DESC texDesc = aoRes.texture->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
      UINT numRows = 0;
      UINT64 rowSize = 0;
      UINT64 total = 0;
      device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

      ComPtr<ID3D12Resource> readback;
      {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = total;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&readback));
      }

      resetCommandList();
      transition(_commandList.Get(), aoRes.texture.Get(),
                 D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
      D3D12_TEXTURE_COPY_LOCATION src = {};
      src.pResource = aoRes.texture.Get();
      src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      src.SubresourceIndex = 0;
      D3D12_TEXTURE_COPY_LOCATION dst = {};
      dst.pResource = readback.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      dst.PlacedFootprint = footprint;
      _commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
      transition(_commandList.Get(), aoRes.texture.Get(),
                 D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      executeAndWait(queue);

      auto *textureData = new std::vector<uint16_t>(size_t(textureSize) * size_t(textureSize));
      {
        uint8_t *mapped = nullptr;
        D3D12_RANGE readRange = {0, total};
        readback->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
        for (UINT row = 0; row < numRows; ++row)
        {
          std::memcpy(textureData->data() + size_t(row) * size_t(textureSize),
                      mapped + footprint.Offset + row * footprint.Footprint.RowPitch,
                      size_t(textureSize) * sizeof(uint16_t));
        }
        D3D12_RANGE written = {0, 0};
        readback->Unmap(0, &written);
      }
      _cache.insert(_renderStructures[i][j].get(), textureData);
    }
  }
}

const std::string DirectXAmbientOcclusionShadowMapShader::_vertexAmbientOcclusionShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
  uint instanceId : SV_InstanceID;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 atomCenterPosition : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 sphere_radius : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.atomCenterPosition = mul(structureUniforms.modelMatrix, input.instancePosition);
  output.sphere_radius = structureUniforms.atomScaleFactor * input.instanceScale;

  int patchNumber = max(structureUniforms.ambientOcclusionPatchNumber, 1);
  float patchSize = structureUniforms.ambientOcclusionPatchSize;
  float k1 = (float)(input.instanceId % (uint)patchNumber);
  float k2 = (float)(input.instanceId / (uint)patchNumber);

  float2 offset = float2(patchSize, patchSize) * float2(k1, k2) * structureUniforms.ambientOcclusionInverseTextureSize;
  float2 origin = offset * 2.0 - 1.0;
  float tmp = 2.0 * patchSize * structureUniforms.ambientOcclusionInverseTextureSize;

  output.texcoords = input.vertexPosition.xy;
  float4 pos = float4(origin + tmp * (input.vertexPosition.xy * 0.5 + float2(0.5, 0.5)), 0.0, 1.0);
  // D3D maps NDC +Y to the top of the RT (V=0), while OpenGL FBO/UV treat V=0 as bottom.
  // Flip clip Y so atlas patch layout matches the OpenGL-style UV math used when sampling AO.
  pos.y = -pos.y;
  output.position = pos;
  return output;
}
)foo");

const std::string DirectXAmbientOcclusionShadowMapShader::_pixelAmbientOcclusionShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
Texture2D<float> shadowMapTexture : register(t0);
SamplerComparisonState shadowMapSampler : register(s0);

cbuffer WeightBlock : register(b0)
{
  float weight;
};

struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 atomCenterPosition : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 sphere_radius : TEXCOORD2;
};

float3 coordinateFromTexturePosition(float2 texturePosition)
{
  float2 absoluteTexturePosition = abs(texturePosition);
  float h = 1.0 - absoluteTexturePosition.x - absoluteTexturePosition.y;

  if (h >= 0.0)
    return float3(texturePosition.x, texturePosition.y, h);
  else
    return float3(sign(texturePosition.x) * (1.0 - absoluteTexturePosition.y),
                  sign(texturePosition.y) * (1.0 - absoluteTexturePosition.x), h);
}

float PSMain(PSInput input) : SV_TARGET
{
  float patchSize = structureUniforms.ambientOcclusionPatchSize;
  // Do NOT flip FragCoord Y. D3D SV_Position Y increases downward (top-left origin), so
  // high V within the patch is the bottom of the patch on screen — and localY→octahedral
  // then matches OpenGL's high-FragCoord→+Y and the ModelN sampling UV math.
  // Flipping Y here wrote +Y occlusion into low-V texels that sampling reads for -Y (and
  // vice versa), making exposed surfaces look dark.
  int2 impostorSpaceCoordinate = int2(int(input.position.x), int(input.position.y)) % int(patchSize);
  float2 newImpostorSpaceCoordinate = (2.0 * float2(impostorSpaceCoordinate) / (patchSize - 1.0) - 1.0);

  float3 imposterXYZ = normalize(coordinateFromTexturePosition(newImpostorSpaceCoordinate));
  float3 currentSphereSurfaceCoordinate = input.sphere_radius.xyz * imposterXYZ;
  float3 pos = currentSphereSurfaceCoordinate + input.atomCenterPosition.xyz;

  float4 shadowCoordinate = mul(shadowUniforms.shadowMatrix, float4(pos, 1.0));
  float4 normal = mul(shadowUniforms.normalMatrix, float4(imposterXYZ, 1.0));

  if (normal.z < 0.0)
    discard;

  // shadowMatrix uses OpenGL depth bias (0.5*ndc+0.5), matching SV_Depth.
  // Flip V: D3D texture (0,0) is top-left; OpenGL shadow UVs assume bottom-left.
  float3 shadow = shadowCoordinate.xyz / shadowCoordinate.w;
  shadow.y = 1.0 - shadow.y;
  float shadowSample = shadowMapTexture.SampleCmpLevelZero(shadowMapSampler, shadow.xy, shadow.z);
  return weight * normal.z * shadowSample;
}
)foo");

const std::string DirectXAmbientOcclusionShadowMapShader::_vertexShadowMapShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float sphere_radius : TEXCOORD0;
  float2 texcoord : TEXCOORD1;
  nointerpolation float4 eye_position : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float sphere_radius = structureUniforms.atomScaleFactor * input.instanceScale.x;
  output.eye_position = mul(shadowUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.sphere_radius = sphere_radius;
  output.texcoord = input.vertexPosition.xy;

  float4 pos = mul(shadowUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  pos.xy += sphere_radius * input.vertexPosition.xy;
  float4 clip = mul(shadowUniforms.projectionMatrix, pos);
  // OpenGL NDC Z [-1,1] -> D3D clip Z [0,1] for rasterization only (PS overrides depth).
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXAmbientOcclusionShadowMapShader::_pixelShadowMapShaderSource =
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float sphere_radius : TEXCOORD0;
  float2 texcoord : TEXCOORD1;
  nointerpolation float4 eye_position : TEXCOORD2;
};

struct PSOutput
{
  float depth : SV_Depth;
};

PSOutput PSMain(PSInput input)
{
  PSOutput output;
  float x = input.texcoord.x;
  float y = input.texcoord.y;
  float zz = 1.0 - x * x - y * y;
  if (zz <= 0.0)
    discard;

  // Same as OpenGL gl_FragDepth: OpenGL NDC Z -> [0,1]. Must match RKShadowUniforms
  // ViewToOpenGLDepthTextureMatrix (0.5*z+0.5) used by SampleCmp — do not omit or double-apply.
  float4 pos = mul(shadowUniforms.projectionMatrix, input.eye_position);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;
  return output;
}
)foo");
