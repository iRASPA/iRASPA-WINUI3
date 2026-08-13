/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
********************************************************************************************************************/

#include "directxribbonambientocclusionshader.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <tuple>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "rkcamera.h"
#include "rkribbonmesh.h"
#include "ribbonaotexturepostprocess.h"
#include <mathkit.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
// Half-float encoding of 1.0
constexpr uint16_t kHalfFloatOne = 0x3C00;
// The ribbon is its own occluder, so every surface point compares its depth against a depth map it
// helped write. Half a shadow-map texel of slope is enough to make a lit surface shadow itself, and
// the bias is what keeps that from speckling the atlas. In depth-range units, so a fraction of an
// Angstrom on any structure worth baking.
constexpr float kSelfShadowBias = 2.0e-3f;
// A texel that the bake barely grazed carries a fraction of the weight it should, and normalizing
// against the brightest texel is what makes 'unoccluded' mean 1.0 whatever the direction set was.
constexpr float kAtlasContentThreshold = 1.0e-5f;

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

DirectXRibbonAmbientOcclusionShader::DirectXRibbonAmbientOcclusionShader()
{
  _cache.setMaxCost(64);
}

DirectXRibbonAmbientOcclusionShader::~DirectXRibbonAmbientOcclusionShader()
{
  _cache.clear();
  if (_fenceEvent)
  {
    CloseHandle(_fenceEvent);
    _fenceEvent = nullptr;
  }
}

void DirectXRibbonAmbientOcclusionShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXRibbonAmbientOcclusionShader::initialize(ID3D12Device *device, ID3D12CommandQueue *queue)
{
  if (!device || !queue)
    return;

  _srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  _rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&_commandAllocator))))
  {
    std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create command allocator";
    return;
  }
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(),
                                       nullptr, IID_PPV_ARGS(&_commandList))))
  {
    std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create command list";
    return;
  }
  _commandList->Close();

  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
  {
    std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create fence";
    return;
  }
  _fenceValue = 0;
  _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  // A 1x1 texture holding 1.0, so a structure with no bake reads as unoccluded rather than black.
  {
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_srvHeap))))
    {
      std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create initial SRV heap";
      return;
    }

    _whiteTexture = createTexture2D(device, 1, 1, DXGI_FORMAT_R16_FLOAT,
                                    D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!_whiteTexture)
      return;

    const uint16_t white = kHalfFloatOne;
    D3D12_RESOURCE_DESC texDesc = _whiteTexture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

    ComPtr<ID3D12Resource> upload = DirectXDeviceHelpers::createUploadBuffer(device, total);
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

void DirectXRibbonAmbientOcclusionShader::ensureGenerationPipelines(ID3D12Device *device,
                                                                    ID3D12RootSignature *rootSignature)
{
  // The atom bake owns the root signature both bakes are recorded against, so the two can share one
  // command list: b0 the direction's weight, b1 the structure, b2 the direction's shadow matrices,
  // t0 the depth map and s0 the comparison sampler that does the depth test.
  if (!device || !rootSignature)
    return;
  if (_genRootSignature == rootSignature && _shadowPso && _accumulatePso)
    return;

  _genRootSignature = rootSignature;
  _shadowPso.Reset();
  _accumulatePso.Reset();

  const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, position)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, normal)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, st)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  // Depth map. No pixel shader: the ribbon is real geometry, so the rasterized depth is the depth,
  // where the atom bake has to compute a sphere's depth from an impostor quad.
  {
    ComPtr<ID3DBlob> vs = compileShader(_vertexShadowMapShaderSource, "VSMain", "vs_5_0");
    if (!vs)
      return;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _genRootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // The swept cross-section is not consistently wound, exactly as in the opaque pass.
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
      std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create shadow PSO";
      return;
    }
  }

  // Atlas accumulation. Every direction adds its contribution, so the blend is additive and there is
  // no depth buffer: the target is indexed by mesh coordinate, where nothing overlaps.
  {
    ComPtr<ID3DBlob> vs = compileShader(_vertexAmbientOcclusionShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_pixelAmbientOcclusionShaderSource, "PSMain", "ps_5_0");
    if (!vs || !ps)
      return;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _genRootSignature;
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

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_accumulatePso))))
    {
      std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to create accumulate PSO";
      return;
    }
  }

}

void DirectXRibbonAmbientOcclusionShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXRibbonAmbientOcclusionShader::deleteBuffers()
{
  _atlases.clear();
  _totalStructureSlots = 0;
}

void DirectXRibbonAmbientOcclusionShader::generateBuffers()
{
  _atlases.resize(_renderStructures.size());
  _totalStructureSlots = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    _atlases[i].resize(_renderStructures[i].size());
    _totalStructureSlots += static_cast<UINT>(_renderStructures[i].size());
  }
}

UINT DirectXRibbonAmbientOcclusionShader::srvIndex(size_t i, size_t j) const
{
  return 1 + flatIndex(i, j); // 0 is the white fallback
}

UINT DirectXRibbonAmbientOcclusionShader::flatIndex(size_t i, size_t j) const
{
  UINT index = 0;
  for (size_t ii = 0; ii < i && ii < _renderStructures.size(); ++ii)
    index += static_cast<UINT>(_renderStructures[ii].size());
  return index + static_cast<UINT>(j);
}

void DirectXRibbonAmbientOcclusionShader::invalidateCachedAmbientOcclusionTexture(
    std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  for (const std::shared_ptr<RKRenderObject> &structure : structures)
    _cache.remove(structure.get());
}

void DirectXRibbonAmbientOcclusionShader::prepareAtlases(ID3D12Device *device)
{
  if (!device || !_initialized)
    return;

  const UINT neededDescriptors = std::max(1u, 1u + _totalStructureSlots);
  D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
  srvDesc.NumDescriptors = neededDescriptors;
  srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  _srvHeap.Reset();
  if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&_srvHeap))))
  {
    std::cerr << "DirectXRibbonAmbientOcclusionShader: failed to recreate SRV heap";
    return;
  }

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

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      AtlasResources &atlas = _atlases[i][j];
      atlas = AtlasResources{};

      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      if (!ribbon || !ribbon->drawRibbon() || !ribbon->ribbonAmbientOcclusion())
        continue;
      if (!_renderStructures[i][j]->isVisible() || ribbon->ribbonNumberOfChains() <= 0)
        continue;

      size_t numberOfAtoms = 0;
      if (auto *atomSource = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get()))
        numberOfAtoms = atomSource->renderAtoms().size();

      // One strip of the atlas per chain, as wide as the longest chain has rings.
      const auto [width, height, stripHeight] =
          RKRibbonMesh::ambientOcclusionAtlasDimensions(ribbon->ribbonMaxSplineSampleCount(),
                                                        ribbon->ribbonNumberOfChains(),
                                                        static_cast<int>(numberOfAtoms));
      (void)stripHeight;
      if (width <= 0 || height <= 0)
        continue;

      D3D12_CLEAR_VALUE clearValue = {};
      clearValue.Format = DXGI_FORMAT_R16_FLOAT;
      clearValue.Color[0] = 0.0f;

      atlas.texture = createTexture2D(device, static_cast<UINT>(width), static_cast<UINT>(height),
                                      DXGI_FORMAT_R16_FLOAT,
                                      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue);
      if (!atlas.texture)
        continue;
      atlas.width = width;
      atlas.height = height;

      D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
      rtvCpu.ptr += SIZE_T(flatIndex(i, j)) * _rtvDescriptorSize;
      D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
      rtv.Format = DXGI_FORMAT_R16_FLOAT;
      rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      device->CreateRenderTargetView(atlas.texture.Get(), &rtv, rtvCpu);

      D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = _srvHeap->GetCPUDescriptorHandleForHeapStart();
      srvCpu.ptr += SIZE_T(srvIndex(i, j)) * _srvDescriptorSize;
      D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
      srv.Format = DXGI_FORMAT_R16_FLOAT;
      srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(atlas.texture.Get(), &srv, srvCpu);
    }
  }
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXRibbonAmbientOcclusionShader::aoSrv(size_t i, size_t j) const
{
  if (!_srvHeap || !hasAo(i, j))
    return _whiteSrvGpu;

  D3D12_GPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(srvIndex(i, j)) * _srvDescriptorSize;
  return handle;
}

bool DirectXRibbonAmbientOcclusionShader::hasAo(size_t i, size_t j) const
{
  if (i >= _atlases.size() || j >= _atlases[i].size())
    return false;
  if (!_atlases[i][j].valid || !_atlases[i][j].texture)
    return false;

  auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
  if (!ribbon || !ribbon->ribbonAmbientOcclusion() || !_renderStructures[i][j]->isVisible())
    return false;
  return true;
}

void DirectXRibbonAmbientOcclusionShader::waitGpu(ID3D12CommandQueue *queue)
{
  const UINT64 fenceToWait = ++_fenceValue;
  queue->Signal(_fence.Get(), fenceToWait);
  if (_fence->GetCompletedValue() < fenceToWait)
  {
    _fence->SetEventOnCompletion(fenceToWait, _fenceEvent);
    WaitForSingleObject(_fenceEvent, INFINITE);
  }
}

void DirectXRibbonAmbientOcclusionShader::resetCommandList()
{
  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);
}

void DirectXRibbonAmbientOcclusionShader::executeAndWait(ID3D12CommandQueue *queue)
{
  _commandList->Close();
  ID3D12CommandList *lists[] = { _commandList.Get() };
  queue->ExecuteCommandLists(1, lists);
  waitGpu(queue);
}

void DirectXRibbonAmbientOcclusionShader::uploadAtlas(ID3D12Device *device, ID3D12CommandQueue *queue,
                                                     size_t i, size_t j,
                                                     const std::vector<uint16_t> &texels)
{
  AtlasResources &atlas = _atlases[i][j];
  if (!atlas.texture || texels.empty())
    return;
  if (texels.size() != size_t(atlas.width) * size_t(atlas.height))
    return;

  D3D12_RESOURCE_DESC texDesc = atlas.texture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSize = 0;
  UINT64 total = 0;
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

  ComPtr<ID3D12Resource> upload = DirectXDeviceHelpers::createUploadBuffer(device, total);
  if (!upload)
    return;

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  upload->Map(0, &readRange, reinterpret_cast<void **>(&mapped));
  for (UINT row = 0; row < numRows; ++row)
  {
    std::memcpy(mapped + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch,
                texels.data() + size_t(row) * size_t(atlas.width),
                size_t(atlas.width) * sizeof(uint16_t));
  }
  upload->Unmap(0, nullptr);

  resetCommandList();
  transition(_commandList.Get(), atlas.texture.Get(),
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = atlas.texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = footprint;
  _commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  transition(_commandList.Get(), atlas.texture.Get(),
             D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  executeAndWait(queue);

  atlas.valid = true;
}

void DirectXRibbonAmbientOcclusionShader::uploadWhiteAtlas(ID3D12Device *device,
                                                           ID3D12CommandQueue *queue,
                                                           size_t i, size_t j)
{
  const AtlasResources &atlas = _atlases[i][j];
  if (!atlas.texture)
    return;
  const std::vector<uint16_t> white(size_t(atlas.width) * size_t(atlas.height), kHalfFloatOne);
  uploadAtlas(device, queue, i, j, white);
}

std::vector<DirectXRibbonAmbientOcclusionShader::Occluder>
DirectXRibbonAmbientOcclusionShader::occluders(size_t i) const
{
  std::vector<Occluder> result;
  if (!_ribbonShader || i >= _renderStructures.size())
    return result;

  for (size_t l = 0; l < _renderStructures[i].size(); ++l)
  {
    auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][l].get());
    if (!ribbon || !ribbon->drawRibbon() || !_renderStructures[i][l]->isVisible())
      continue;

    Occluder occluder;
    if (!_ribbonShader->geometryBuffers(i, l, occluder.vbv, occluder.ibv))
      continue;
    occluder.ranges = ribbon->ribbonChainDrawRanges();
    if (occluder.ranges.empty())
      continue;
    occluder.structureIndex = l;
    result.push_back(std::move(occluder));
  }
  return result;
}

bool DirectXRibbonAmbientOcclusionShader::needsBake(size_t i, size_t j)
{
  if (!_initialized || !_shadowPso || !_accumulatePso || !_ribbonShader)
    return false;
  if (i >= _atlases.size() || j >= _atlases[i].size() || !_atlases[i][j].texture)
    return false;

  const AtlasResources &atlas = _atlases[i][j];
  auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
  const int vertexCount = ribbon ? ribbon->ribbonNumberOfVertices() : 0;

  if (CachedAtlas *cached = _cache.object(_renderStructures[i][j].get()))
  {
    if (cached->width == atlas.width && cached->height == atlas.height &&
        cached->vertexCount == vertexCount)
    {
      return false;
    }
    _cache.remove(_renderStructures[i][j].get());
  }
  return true;
}

void DirectXRibbonAmbientOcclusionShader::uploadCachedAtlases(ID3D12Device *device,
                                                              ID3D12CommandQueue *queue)
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      if (i >= _atlases.size() || j >= _atlases[i].size() || !_atlases[i][j].texture)
        continue;
      const AtlasResources &atlas = _atlases[i][j];
      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      const int vertexCount = ribbon ? ribbon->ribbonNumberOfVertices() : 0;

      if (CachedAtlas *cached = _cache.object(_renderStructures[i][j].get()))
      {
        if (cached->width == atlas.width && cached->height == atlas.height &&
            cached->vertexCount == vertexCount)
        {
          uploadAtlas(device, queue, i, j, cached->texels);
        }
      }
    }
  }
}

bool DirectXRibbonAmbientOcclusionShader::beginBake(ID3D12Device * /*device*/,
                                                    ID3D12CommandQueue *queue, size_t i, size_t j)
{
  _bakeRanges.clear();
  _bakeVbv = {};
  _bakeIbv = {};
  _bakeVertexCount = 0;

  AtlasResources &atlas = _atlases[i][j];
  auto *target = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
  if (!target || !atlas.texture)
    return false;
  if (!_ribbonShader->geometryBuffers(i, j, _bakeVbv, _bakeIbv))
    return false;

  _bakeRanges = target->ribbonChainDrawRanges();
  if (_bakeRanges.empty())
    return false;
  _bakeVertexCount = target->ribbonNumberOfVertices();

  _bakeRtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
  _bakeRtv.ptr += SIZE_T(flatIndex(i, j)) * _rtvDescriptorSize;

  _bakeViewport = {};
  _bakeViewport.Width = float(atlas.width);
  _bakeViewport.Height = float(atlas.height);
  _bakeViewport.MaxDepth = 1.0f;
  _bakeScissor = {0, 0, LONG(atlas.width), LONG(atlas.height)};

  resetCommandList();
  transition(_commandList.Get(), atlas.texture.Get(),
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  _commandList->ClearRenderTargetView(_bakeRtv, clearColor, 0, nullptr);
  executeAndWait(queue);
  return true;
}

void DirectXRibbonAmbientOcclusionShader::recordAccumulate(ID3D12GraphicsCommandList *commandList,
                                                           float weight,
                                                           D3D12_GPU_VIRTUAL_ADDRESS shadowCBV,
                                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBV)
{
  if (_bakeRanges.empty())
    return;

  commandList->OMSetRenderTargets(1, &_bakeRtv, FALSE, nullptr);
  commandList->RSSetViewports(1, &_bakeViewport);
  commandList->RSSetScissorRects(1, &_bakeScissor);
  commandList->SetPipelineState(_accumulatePso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  commandList->SetGraphicsRoot32BitConstants(0, 1, &weight, 0);
  commandList->SetGraphicsRootConstantBufferView(2, shadowCBV);
  commandList->SetGraphicsRootConstantBufferView(1, structureCBV);
  commandList->IASetVertexBuffers(0, 1, &_bakeVbv);
  commandList->IASetIndexBuffer(&_bakeIbv);
  for (const RKRibbonChainDrawRange &range : _bakeRanges)
  {
    if (range.indexCount > 0)
      commandList->DrawIndexedInstanced(UINT(range.indexCount), 1, UINT(range.indexStart), 0, 0);
  }
}

void DirectXRibbonAmbientOcclusionShader::finishBake(ID3D12Device *device, ID3D12CommandQueue *queue,
                                                     size_t i, size_t j)
{
  std::vector<uint16_t> texels;
  if (readbackAtlas(device, queue, i, j, texels))
  {
    const AtlasResources &atlas = _atlases[i][j];
    auto *entry = new CachedAtlas{texels, atlas.width, atlas.height, _bakeVertexCount};
    uploadAtlas(device, queue, i, j, texels);
    _cache.insert(_renderStructures[i][j].get(), entry);
  }
  else
  {
    // Nothing was drawn, so leaving the cleared atlas in place would shade the ribbon black. The
    // empty result is cached like any other, or every reload would try the bake again and drag the
    // atom textures of the scene along with it.
    const AtlasResources &atlas = _atlases[i][j];
    std::vector<uint16_t> white(size_t(atlas.width) * size_t(atlas.height), kHalfFloatOne);
    uploadAtlas(device, queue, i, j, white);
    _cache.insert(_renderStructures[i][j].get(),
                  new CachedAtlas{std::move(white), atlas.width, atlas.height, _bakeVertexCount});
  }

  _bakeRanges.clear();
  _bakeVbv = {};
  _bakeVertexCount = 0;
}

bool DirectXRibbonAmbientOcclusionShader::readbackAtlas(ID3D12Device *device,
                                                        ID3D12CommandQueue *queue, size_t i, size_t j,
                                                        std::vector<uint16_t> &texels)
{
  AtlasResources &atlas = _atlases[i][j];
  if (!atlas.texture || _bakeRanges.empty())
    return false;

  // Read the atlas back to finish it on the CPU: the texels the rasterizer missed have to be filled
  // in from their neighbours before anything samples them.
  D3D12_RESOURCE_DESC texDesc = atlas.texture->GetDesc();
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
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback))))
    {
      return false;
    }
  }

  resetCommandList();
  transition(_commandList.Get(), atlas.texture.Get(),
             D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = atlas.texture.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;
  _commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  transition(_commandList.Get(), atlas.texture.Get(),
             D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  executeAndWait(queue);

  const size_t texelCount = size_t(atlas.width) * size_t(atlas.height);
  std::vector<float> channel(texelCount, 0.0f);
  {
    uint8_t *mapped = nullptr;
    D3D12_RANGE readRange = {0, total};
    if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
      return false;
    for (UINT row = 0; row < numRows; ++row)
    {
      const uint16_t *sourceRow = reinterpret_cast<const uint16_t *>(
          mapped + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch);
      for (int x = 0; x < atlas.width; ++x)
        channel[size_t(row) * size_t(atlas.width) + size_t(x)] =
            RKHalfFloat::floatFromHalfBits(sourceRow[x]);
    }
    D3D12_RANGE written = {0, 0};
    readback->Unmap(0, &written);
  }

  const float peak = *std::max_element(channel.begin(), channel.end());
  if (!(peak > kAtlasContentThreshold))
    return false;

  RibbonAOTexturePostProcess::dilateAndSmooth(channel, atlas.width, atlas.height);
  RibbonAOTexturePostProcess::gaussianBlur(channel, atlas.width, atlas.height);

  texels.resize(texelCount);
  for (size_t index = 0; index < texelCount; ++index)
    texels[index] = RKHalfFloat::halfBitsFromFloat(channel[index]);
  return true;
}

const std::string DirectXRibbonAmbientOcclusionShader::_vertexAmbientOcclusionShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float2 vertexST : TEXCOORD0;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 worldPosition : TEXCOORD0;
  float3 worldNormal : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  // The ribbon is laid flat into its atlas: the lightmap coordinate is the position, so a triangle
  // covers exactly the texels its surface owns. Clip Y is flipped because D3D puts +Y at the top of
  // the target, where a V of zero belongs.
  float2 clipPos = input.vertexST * 2.0 - 1.0;
  output.position = float4(clipPos.x, -clipPos.y, 0.0, 1.0);
  output.worldPosition = mul(structureUniforms.modelMatrix, input.vertexPosition).xyz;
  output.worldNormal = normalize(mul((float3x3)structureUniforms.modelMatrix, input.vertexNormal.xyz));
  return output;
}
)foo");

const std::string DirectXRibbonAmbientOcclusionShader::_pixelAmbientOcclusionShaderSource =
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
  float3 worldPosition : TEXCOORD0;
  float3 worldNormal : TEXCOORD1;
};

float PSMain(PSInput input) : SV_TARGET
{
  float4 shadowCoordinate = mul(shadowUniforms.shadowMatrix, float4(input.worldPosition, 1.0));
  float3 shadowPos = shadowCoordinate.xyz / shadowCoordinate.w;
  float4 viewNormal = mul(shadowUniforms.viewMatrix, float4(normalize(input.worldNormal), 0.0));

  // A surface turned away from this direction receives nothing from it, and contributes in
  // proportion to how squarely it faces it.
  float normalWeight = max(viewNormal.z, 0.0);
  if (normalWeight < 1.0e-4)
    discard;

  // shadowMatrix carries the OpenGL depth bias the depth map was written with. Flip V because a D3D
  // texture starts at the top-left where the OpenGL shadow coordinate starts at the bottom-left.
  shadowPos.y = 1.0 - shadowPos.y;
  float visibility = shadowMapTexture.SampleCmpLevelZero(shadowMapSampler, shadowPos.xy,
                                                         shadowPos.z - )foo") +
std::to_string(kSelfShadowBias) + std::string(R"foo();
  return weight * normalWeight * visibility;
}
)foo");

const std::string DirectXRibbonAmbientOcclusionShader::_vertexShadowMapShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::ShadowUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float2 vertexST : TEXCOORD0;
};

struct VSOutput
{
  float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 eyePosition = mul(shadowUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.vertexPosition));
  float4 clip = mul(shadowUniforms.projectionMatrix, eyePosition);
  // OpenGL NDC Z [-1,1] -> D3D clip Z [0,1], the same bias shadowMatrix applies when comparing.
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");
