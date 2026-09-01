/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "directxshader.h"

/// The edge cues of Tarini, Cignoni and Montani: a dark contour where one surface passes in front of
/// another, and a halo cast by the nearer of the two. Both are drawn here rather than in the shaders
/// that draw the molecule, because both are questions about the finished image: whether the surface
/// at a pixel stands in front of what surrounds it, which no single sphere can answer about itself.
///
/// What it reads: the resolved colour of the scene, its resolved depth, and its stencil, in which the
/// molecular passes recorded which cues the structure that drew each pixel asked for. Colour is read
/// from a texture of its own rather than from the target, one image not being readable and writable
/// at once, so the caller resolves into sceneTexture() and draws to wherever the frame is going.
class DirectXEdgeCueingShader : public DirectXShader
{
public:
  DirectXEdgeCueingShader() = default;

  void loadShader(ID3D12Device *device) override;

  /// \a sceneSampleCount decides how the stencil is declared, and so has to be the count the scene
  /// was drawn with: a multisampled stencil is read as it stands, sample by sample, there being no
  /// resolve for a stencil the way there is for colour and depth.
  void initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat, UINT sceneSampleCount);

  bool isReady() const { return _ready; }

  /// The texture the scene's colour is to be resolved into, made at this size if it is not already.
  /// Null if it cannot be made, in which case the caller should resolve straight to its target and
  /// leave the cues undrawn. Kept in the state a pixel shader reads it in, which is the state the
  /// resolve is asked to leave it in as well.
  ID3D12Resource *sceneTexture(ID3D12Device *device, int width, int height);

  /// Draws a rasterized frame with its cues: \a sceneColor is the resolved colour from
  /// sceneTexture(), and the depth and the tag come from the scene's depth-stencil buffer.
  void paint(ID3D12GraphicsCommandList *commandList, D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
             D3D12_GPU_VIRTUAL_ADDRESS frameConstants, ID3D12Resource *sceneColor,
             ID3D12Resource *sceneDepth, ID3D12Resource *sceneStencil, int width, int height);

  /// Draws a path-traced frame with its cues, in place of the plain blit of the composite. The
  /// tracer's molecular geometry never went through a raster pass, so neither the depth of what is
  /// visible nor the tag for it is in any attachment: both are read from the buffers its resolve
  /// kernel wrote, one value per pixel, which the caller has put in a state a pixel shader can read.
  void paintTraced(ID3D12GraphicsCommandList *commandList,
                   D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
                   D3D12_GPU_VIRTUAL_ADDRESS frameConstants, ID3D12Resource *compositeColor,
                   ID3D12Resource *tracedDepth, ID3D12Resource *tracedCueMask, int width,
                   int height);

  /// Whether a traced frame can be cued. False when the traced variant of the pass would not
  /// compile or create, in which case the caller blits the composite as it did before.
  bool canPaintTraced() const { return _ready && _tracedPso; }

private:
  void createRootSignature(ID3D12Device *device);
  void createFullscreenQuad(ID3D12Device *device);
  void createHeap(ID3D12Device *device);
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle(UINT slot) const;
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle(UINT slot) const;
  /// The part both paints share: bind, cover the target with the quad, draw.
  void drawFullscreen(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pso,
                      D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
                      D3D12_GPU_VIRTUAL_ADDRESS frameConstants, UINT firstSrvSlot, int width,
                      int height);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12PipelineState> _tracedPso;
  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  ComPtr<ID3D12Resource> _vertexBuffer;
  ComPtr<ID3D12Resource> _indexBuffer;
  ComPtr<ID3D12Resource> _sceneTexture;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};
  D3D12_INDEX_BUFFER_VIEW _ibv{};
  DXGI_FORMAT _rtvFormat = DXGI_FORMAT_UNKNOWN;
  UINT _sceneSampleCount = 1;
  UINT _srvStride = 0;
  UINT _indexCount = 0;
  int _sceneWidth = 0;
  int _sceneHeight = 0;
  bool _ready = false;
};
