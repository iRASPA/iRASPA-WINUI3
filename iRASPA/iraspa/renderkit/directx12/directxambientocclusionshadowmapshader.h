/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "directxatomsphereshader.h"
#include "directxatomorthographicimpostershader.h"
#include "directxribbonambientocclusionshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

// Owns the depth map every ambient-occlusion bake reads from. Atoms and ribbons are drawn into one
// map per direction, so each shades the other, and the several hundred directions are swept once for
// both bakes; the ribbon shader is handed the map and gathers its atlas alongside the atom texture.
class DirectXAmbientOcclusionShadowMapShader : public DirectXShader
{
public:
  DirectXAmbientOcclusionShadowMapShader(DirectXAtomSphereShader &atomSphere,
                                         DirectXAtomOrthographicImposterShader &orthoImposter);
  ~DirectXAmbientOcclusionShadowMapShader() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12CommandQueue *queue);
  void setRibbonAmbientOcclusionShader(DirectXRibbonAmbientOcclusionShader *shader) { _ribbonAo = shader; }
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device, ID3D12CommandQueue *queue,
                  std::shared_ptr<RKRenderDataSource> dataSource, RKRenderQuality quality);
  void invalidateCachedAmbientOcclusionTexture(std::vector<std::shared_ptr<RKRenderObject>> structures);

  ID3D12DescriptorHeap *srvHeap() const { return _srvHeap.Get(); }
  D3D12_GPU_DESCRIPTOR_HANDLE aoSrv(size_t i, size_t j) const;
  bool hasAo(size_t i, size_t j) const;
  D3D12_GPU_DESCRIPTOR_HANDLE whiteAoSrv() const { return _whiteSrvGpu; }

private:
  void deleteBuffers();
  void generateBuffers();
  // True when a visible structure wants an atom bake or a ribbon bake.
  bool anyStructureNeedsAmbientOcclusion() const;
  void adjustAmbientOcclusionTextureSize(ID3D12Device *device);
  void updateAmbientOcclusionTextures(ID3D12Device *device, ID3D12CommandQueue *queue,
                                      std::shared_ptr<RKRenderDataSource> dataSource,
                                      RKRenderQuality quality);
  void createGenerationPipelines(ID3D12Device *device);
  void ensureTransientShadowResources(ID3D12Device *device);
  void waitGpu(ID3D12CommandQueue *queue);
  void resetCommandList();
  void executeAndWait(ID3D12CommandQueue *queue);
  void uploadAoTextureData(ID3D12Device *device, ID3D12CommandQueue *queue,
                           size_t i, size_t j, const std::vector<uint16_t> &data, int textureSize);
  UINT srvIndex(size_t i, size_t j) const;

  struct AoStructureResources
  {
    ComPtr<ID3D12Resource> texture;
    int textureSize = 0;
    bool valid = false;
  };

  DirectXAtomSphereShader &_atomSphereShader;
  DirectXAtomOrthographicImposterShader &_atomOrthographicImposterShader;
  DirectXRibbonAmbientOcclusionShader *_ribbonAo = nullptr;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<AoStructureResources>> _aoResources;

  ComPtr<ID3D12RootSignature> _genRootSignature;
  ComPtr<ID3D12PipelineState> _shadowPso;
  ComPtr<ID3D12PipelineState> _aoAccumulatePso;

  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  ComPtr<ID3D12DescriptorHeap> _rtvHeap;
  ComPtr<ID3D12DescriptorHeap> _dsvHeap;
  ComPtr<ID3D12DescriptorHeap> _genSrvHeap;
  ComPtr<ID3D12DescriptorHeap> _genSamplerHeap;

  ComPtr<ID3D12Resource> _whiteTexture;
  D3D12_GPU_DESCRIPTOR_HANDLE _whiteSrvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE _whiteSrvCpu{};

  ComPtr<ID3D12Resource> _shadowDepthTexture;
  D3D12_CPU_DESCRIPTOR_HANDLE _shadowDsvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE _shadowSrvGpu{};
  D3D12_CPU_DESCRIPTOR_HANDLE _shadowSrvCpu{};

  ComPtr<ID3D12CommandAllocator> _commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> _commandList;
  ComPtr<ID3D12Fence> _fence;
  UINT64 _fenceValue = 0;
  HANDLE _fenceEvent = nullptr;

  UINT _srvDescriptorSize = 0;
  UINT _rtvDescriptorSize = 0;
  UINT _totalStructureSlots = 0;
  bool _initialized = false;

  RKCache<RKRenderObject *, std::vector<uint16_t>> _cache;

  static const std::string _vertexAmbientOcclusionShaderSource;
  static const std::string _pixelAmbientOcclusionShaderSource;
  static const std::string _vertexShadowMapShaderSource;
  static const std::string _pixelShadowMapShaderSource;
};
