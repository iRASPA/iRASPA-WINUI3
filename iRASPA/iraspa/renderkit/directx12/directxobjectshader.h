/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

// Primitive objects: molecule + crystal ellipsoids, cylinders, polygonal prisms.
class DirectXObjectShader : public DirectXShader
{
public:
  DirectXObjectShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paintOpaque(ID3D12GraphicsCommandList *commandList,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride);
  // Draws the transparent primitives of a single structure. The renderer calls this in
  // back-to-front order so overlapping transparent objects blend correctly.
  void paintTransparent(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        size_t sceneIndex, size_t movieIndex, size_t structureIndex);
  // Geometry only; the caller has already bound the picking PSO.
  void drawPickGeometry(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride) const;

private:
  enum class Kind
  {
    crystalEllipse = 0,
    crystalCylinder,
    crystalPrism,
    ellipse,
    cylinder,
    prism,
    count
  };

  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
    UINT instanceCount = 0;
  };

  void deleteBuffers();
  void generateBuffers();
  void initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void drawKind(ID3D12GraphicsCommandList *commandList, Kind kind, bool opaque,
                D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase, UINT structureCBVStride,
                size_t sceneIndex, size_t movieIndex, size_t structureIndex);
  void reloadKind(ID3D12Device *device, Kind kind);

  ComPtr<ID3D12PipelineState> _opaquePso;
  ComPtr<ID3D12PipelineState> _transparentPso;
  bool _opaquePsoReady = false;
  bool _transparentPsoReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<MeshBuffers>> _buffers[static_cast<size_t>(Kind::count)];

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
