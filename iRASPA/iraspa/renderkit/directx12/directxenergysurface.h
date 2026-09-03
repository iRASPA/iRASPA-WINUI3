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

class DirectXEnergySurface : public DirectXShader
{
public:
  DirectXEnergySurface() = default;
  ~DirectXEnergySurface() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void invalidateIsosurface(std::vector<std::shared_ptr<RKRenderObject>> structures);

  void paintOpaque(ID3D12GraphicsCommandList *commandList,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                   UINT isosurfaceCBVStride);
  // Draws the transparent isosurface of a single structure. The renderer calls this in
  // back-to-front order so overlapping transparent surfaces blend correctly.
  void paintTransparent(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                        UINT isosurfaceCBVStride,
                        size_t sceneIndex, size_t movieIndex, size_t structureIndex);

private:
  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT vertexCount = 0;   // 3 * numberOfTriangles (DrawInstanced vertexCountPerInstance)
    UINT instanceCount = 0;
    size_t numberOfIndices = 0; // triangle count (OpenGL _surfaceNumberOfIndices)
  };

  void deleteBuffers();
  void generateBuffers();
  void initializeVertexBuffers(ID3D12Device *device);
  void initializeOpaquePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                           DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeTransparentPSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                 DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc(ID3D12RootSignature *rootSignature,
                                                 ID3DBlob *vs, ID3DBlob *ps,
                                                 DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                                 const D3D12_INPUT_ELEMENT_DESC *inputLayout,
                                                 UINT inputLayoutCount) const;

  ComPtr<ID3D12PipelineState> _opaquePso;
  ComPtr<ID3D12PipelineState> _transparentFrontCullPso;
  ComPtr<ID3D12PipelineState> _transparentBackCullPso;
  bool _opaquePsoReady = false;
  bool _transparentPsoReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<MeshBuffers>> _buffers;

  std::array<RKCache<RKRenderObject *, std::vector<float>>, 9> _caches;
  /// The well surface reads a three-floats-per-point (energy, Apollonius distance, medial
  /// reliability) field rather than the energy grid; cached apart so that switching rendering
  /// methods cannot hand one builder the other's data. Like the energy grid it depends on the probe
  /// and the force field, and is purged by the same invalidateIsosurface calls.
  std::array<RKCache<RKRenderObject *, std::vector<float>>, 9> _wellFieldCaches;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
