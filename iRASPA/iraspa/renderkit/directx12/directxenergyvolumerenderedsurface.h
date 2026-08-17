/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXEnergyVolumeRenderedSurface : public DirectXShader
{
public:
  DirectXEnergyVolumeRenderedSurface() = default;
  ~DirectXEnergyVolumeRenderedSurface() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device, ID3D12CommandQueue *commandQueue);
  void invalidateIsosurface(std::vector<std::shared_ptr<RKRenderObject>> structures);

  // Bind a readable scene-depth texture into every structure's t2 SRV slot.
  void bindSceneDepthSRV(ID3D12Device *device, ID3D12Resource *sceneDepthReadable);
  bool needsSceneDepth() const;

  // Ensure a same-format depth replica for sampling; call after opaque scene draws.
  void ensureSceneDepthCopy(ID3D12Device *device, UINT width, UINT height);
  // Bit-exact CopyResource of live DSV (must be unbound) → bind copy as t2.
  // Returns the copy resource so the caller can rebind the live DSV for volume depth writes.
  ID3D12Resource *copySceneDepthAndBind(ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
                                        ID3D12Resource *sceneDepthResource);

  // Both draw a single structure and return whether they issued a draw; the renderer calls them
  // in back-to-front order so overlapping transparent volumes blend correctly. A true return
  // also tells the caller that this shader's own root signature and descriptor heap are bound.
  bool paintOpaque(ID3D12GraphicsCommandList *commandList,
                   D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                   UINT isosurfaceCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS lightsCBV,
                   size_t sceneIndex, size_t movieIndex, size_t structureIndex);
  bool paintTransparent(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                        UINT isosurfaceCBVStride,
                        D3D12_GPU_VIRTUAL_ADDRESS lightsCBV,
                        size_t sceneIndex, size_t movieIndex, size_t structureIndex);

private:
  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> volumeTexture;
    ComPtr<ID3D12Resource> volumeUpload;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
    bool volumeValid = false;
  };

  void deleteBuffers();
  void generateBuffers();
  void createVolumeRootSignature(ID3D12Device *device);
  void initializePSOs(ID3D12Device *device, DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeTransferFunctionTexture(ID3D12Device *device, ID3D12CommandQueue *commandQueue);
  void initializeVertexBuffers(ID3D12Device *device, ID3D12CommandQueue *commandQueue);
  void ensureUploadInfrastructure(ID3D12Device *device);
  void executeAndWait(ID3D12CommandQueue *queue);
  bool paintCommon(ID3D12GraphicsCommandList *commandList,
                   ID3D12PipelineState *pso,
                   bool opaquePass,
                   D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                   UINT isosurfaceCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS lightsCBV,
                   size_t sceneIndex, size_t movieIndex, size_t structureIndex);
  void ensureFarDepthTexture(ID3D12Device *device, ID3D12CommandQueue *commandQueue);
  void bindFarDepthToAllSlots(ID3D12Device *device);
  void createDepthCopyPipeline(ID3D12Device *device);
  UINT flatStructureCount() const;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(UINT descriptorIndex) const;
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(UINT descriptorIndex) const;

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _opaquePso;
  ComPtr<ID3D12PipelineState> _transparentPso;
  bool _opaquePsoReady = false;
  bool _transparentPsoReady = false;

  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  UINT _srvDescriptorSize = 0;
  ComPtr<ID3D12Resource> _transferFunctionTexture;
  ComPtr<ID3D12Resource> _transferFunctionUpload;
  // Fallback only; paint replaces t2 with a full-res scene-depth copy when available.
  // NOTE: Texture2D.Load is OOB on a 1x1 for FragCoord != (0,0); the PS treats depth<=0 as far.
  ComPtr<ID3D12Resource> _farDepthTexture;
  ComPtr<ID3D12Resource> _farDepthUpload;

  // Full-res typeless depth replica (cannot sample the live DSV while it is bound).
  ComPtr<ID3D12Resource> _sceneDepthCopy;
  // Legacy fullscreen R32 blit path (kept compiled; CopyResource is preferred).
  ComPtr<ID3D12DescriptorHeap> _sceneDepthCopyRtvHeap;
  ComPtr<ID3D12DescriptorHeap> _depthCopySrcSrvHeap;
  ComPtr<ID3D12RootSignature> _depthCopyRootSignature;
  ComPtr<ID3D12PipelineState> _depthCopyPso;
  ComPtr<ID3D12Resource> _depthCopyVb;
  D3D12_VERTEX_BUFFER_VIEW _depthCopyVbv{};
  UINT _sceneDepthCopyWidth = 0;
  UINT _sceneDepthCopyHeight = 0;
  D3D12_RESOURCE_STATES _sceneDepthCopyState = D3D12_RESOURCE_STATE_COMMON;

  ComPtr<ID3D12CommandAllocator> _commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> _commandList;
  ComPtr<ID3D12Fence> _fence;
  HANDLE _fenceEvent = nullptr;
  UINT64 _fenceValue = 0;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<MeshBuffers>> _buffers;

  // Indices 1..9 cover sizes 2..512 (2^powerOfTwo).
  std::array<RKCache<RKRenderObject *, std::vector<float4>>, 10> _caches;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;

  static std::array<float4, 256> RASPA_PES_TransferFunction;
  static std::array<float4, 256> CoolWarmTransferFunction;
  static std::array<float4, 256> XrayTransferFunction;
  static std::array<float4, 256> GrayTransferFunction;
  static std::array<float4, 256> RainbowTransferFunction;
  static std::array<float4, 256> TurboTransferFunction;
  static std::array<float4, 256> GnuplotTransferFunction;
  static std::array<float4, 256> SpectralTransferFunction;
  static std::array<float4, 256> CoolTransferFunction;
  static std::array<float4, 256> ViridisTransferFunction;
  static std::array<float4, 256> PlasmaTransferFunction;
  static std::array<float4, 256> InfernoTransferFunction;
  static std::array<float4, 256> MagmaTransferFunction;
  static std::array<float4, 256> CividisTransferFunction;
  static std::array<float4, 256> SpringTransferFunction;
  static std::array<float4, 256> SummerTransferFunction;
  static std::array<float4, 256> AutumnTransferFunction;
  static std::array<float4, 256> WinterTransferFunction;
  static std::array<float4, 256> RedsTransferFunction;
  static std::array<float4, 256> GreensTransferFunction;
  static std::array<float4, 256> BluesTransferFunction;
  static std::array<float4, 256> PurplesTransferFunction;
  static std::array<float4, 256> OrangesTransferFunction;
};
