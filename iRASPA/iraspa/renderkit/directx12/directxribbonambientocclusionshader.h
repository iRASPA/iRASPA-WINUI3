/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "directxribbonshader.h"
#include "rkcache.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

// The ribbon is shaded from a baked lightmap rather than a screen-space effect: for each of several
// hundred directions the ribbon is rendered into a depth map and every surface point facing that
// direction adds its visibility to the atlas texel its 'st' coordinate names. Because the atlas is
// indexed by mesh coordinate and not by pixel, the result survives camera moves and is baked once per
// structure and cached.
//
// The depth map itself belongs to the atom occlusion shader, which draws the atoms of the scene and
// the ribbons of the scene into one map per direction and then asks both bakes to gather from it.
// Atoms therefore shade the ribbon and the ribbon shades the atoms, as they do in Cocoa, and the
// several hundred directions are swept once rather than once per kind of geometry.
class DirectXRibbonAmbientOcclusionShader : public DirectXShader
{
public:
  // One ribbon of the scene as the depth map sees it.
  struct Occluder
  {
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    std::vector<RKRibbonChainDrawRange> ranges;
    size_t structureIndex = 0;
  };

  DirectXRibbonAmbientOcclusionShader();
  ~DirectXRibbonAmbientOcclusionShader() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12CommandQueue *queue);

  // The bake draws the very mesh the opaque pass draws, so there is one ribbon on the GPU.
  void setRibbonShader(const DirectXRibbonShader *ribbonShader) { _ribbonShader = ribbonShader; }
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void invalidateCachedAmbientOcclusionTexture(std::vector<std::shared_ptr<RKRenderObject>> structures);

  // Driven by the atom occlusion shader, in this order: pipelines and atlases once, then for each
  // structure that needs one a begin, an accumulate per direction and a finish.
  void ensureGenerationPipelines(ID3D12Device *device, ID3D12RootSignature *rootSignature);
  void prepareAtlases(ID3D12Device *device);
  ID3D12PipelineState *shadowPipelineState() const { return _shadowPso.Get(); }
  std::vector<Occluder> occluders(size_t i) const;
  bool needsBake(size_t i, size_t j);
  void uploadCachedAtlases(ID3D12Device *device, ID3D12CommandQueue *queue);
  bool beginBake(ID3D12Device *device, ID3D12CommandQueue *queue, size_t i, size_t j);
  void recordAccumulate(ID3D12GraphicsCommandList *commandList, float weight,
                        D3D12_GPU_VIRTUAL_ADDRESS shadowCBV,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBV);
  void finishBake(ID3D12Device *device, ID3D12CommandQueue *queue, size_t i, size_t j);

  ID3D12DescriptorHeap *srvHeap() const { return _srvHeap.Get(); }
  D3D12_GPU_DESCRIPTOR_HANDLE aoSrv(size_t i, size_t j) const;
  bool hasAo(size_t i, size_t j) const;
  D3D12_GPU_DESCRIPTOR_HANDLE whiteAoSrv() const { return _whiteSrvGpu; }

private:
  struct AtlasResources
  {
    ComPtr<ID3D12Resource> texture;
    int width = 0;
    int height = 0;
    bool valid = false;
  };

  // Half-float texels as they are handed to the GPU, alongside what the mesh looked like when they
  // were baked. A rebuild that changes the atlas shape or the vertex count invalidates them; one
  // that only widens a sheet does not, so the ribbon keeps the occlusion of the mesh it replaced
  // until something asks for a rebake.
  struct CachedAtlas
  {
    std::vector<uint16_t> texels;
    int width = 0;
    int height = 0;
    int vertexCount = 0;
  };

  void deleteBuffers();
  void generateBuffers();
  bool readbackAtlas(ID3D12Device *device, ID3D12CommandQueue *queue, size_t i, size_t j,
                     std::vector<uint16_t> &texels);
  void uploadAtlas(ID3D12Device *device, ID3D12CommandQueue *queue, size_t i, size_t j,
                   const std::vector<uint16_t> &texels);
  void uploadWhiteAtlas(ID3D12Device *device, ID3D12CommandQueue *queue, size_t i, size_t j);
  void waitGpu(ID3D12CommandQueue *queue);
  void resetCommandList();
  void executeAndWait(ID3D12CommandQueue *queue);
  UINT srvIndex(size_t i, size_t j) const;
  UINT flatIndex(size_t i, size_t j) const;

  const DirectXRibbonShader *_ribbonShader = nullptr;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<AtlasResources>> _atlases;

  ID3D12RootSignature *_genRootSignature = nullptr;
  ComPtr<ID3D12PipelineState> _shadowPso;
  ComPtr<ID3D12PipelineState> _accumulatePso;

  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  ComPtr<ID3D12DescriptorHeap> _rtvHeap;

  ComPtr<ID3D12Resource> _whiteTexture;
  D3D12_GPU_DESCRIPTOR_HANDLE _whiteSrvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE _whiteSrvCpu{};

  // The structure whose atlas the accumulate is currently gathering into.
  D3D12_VERTEX_BUFFER_VIEW _bakeVbv{};
  D3D12_INDEX_BUFFER_VIEW _bakeIbv{};
  std::vector<RKRibbonChainDrawRange> _bakeRanges;
  D3D12_CPU_DESCRIPTOR_HANDLE _bakeRtv{};
  D3D12_VIEWPORT _bakeViewport{};
  D3D12_RECT _bakeScissor{};
  int _bakeVertexCount = 0;

  ComPtr<ID3D12CommandAllocator> _commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> _commandList;
  ComPtr<ID3D12Fence> _fence;
  UINT64 _fenceValue = 0;
  HANDLE _fenceEvent = nullptr;

  UINT _srvDescriptorSize = 0;
  UINT _rtvDescriptorSize = 0;
  UINT _totalStructureSlots = 0;
  bool _initialized = false;

  RKCache<RKRenderObject *, CachedAtlas> _cache;

  static const std::string _vertexAmbientOcclusionShaderSource;
  static const std::string _pixelAmbientOcclusionShaderSource;
  static const std::string _vertexShadowMapShaderSource;
};
