/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "rkribbonmesh.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXRibbonAmbientOcclusionShader;

// Protein ribbons: a swept cross-section along a spline, built on the CPU and drawn as one
// indexed triangle mesh per structure (shared ring vertices + index ranges).
class DirectXRibbonShader : public DirectXShader
{
public:
  DirectXRibbonShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);

  // The occlusion atlas belongs to the bake, and the descriptor heap it lives in has to be the one
  // bound when this draws. Without it the ribbon is lit unoccluded.
  void paintOpaque(ID3D12GraphicsCommandList *commandList,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride,
                   const DirectXRibbonAmbientOcclusionShader *aoShader = nullptr);

  // The picking pass draws the very same vertices and ranges, so hidden ribbon is not pickable and
  // there is no second copy of the mesh on the GPU. False when this structure has no ribbon to draw.
  bool pickGeometry(size_t sceneIndex, size_t structureIndex, D3D12_VERTEX_BUFFER_VIEW &vbv,
                    D3D12_INDEX_BUFFER_VIEW &ibv,
                    std::vector<RKRibbonChainDrawRange> &visibleRanges) const;

  // The selection and AO passes draw sub-ranges of this same mesh, so they borrow the GPU buffers
  // rather than uploading the ribbon twice. False when this structure has no ribbon on the GPU.
  bool geometryBuffers(size_t sceneIndex, size_t structureIndex, D3D12_VERTEX_BUFFER_VIEW &vbv,
                       D3D12_INDEX_BUFFER_VIEW &ibv) const;

private:
  struct RibbonBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    // The ranges a structure is drawn by at the finest level the tree can hide it at. The mesh is one
    // vertex/index buffer either way; which ranges are actually encoded is decided per frame by the
    // structure, and this only says whether there is anything to encode at all.
    std::vector<RKRibbonChainDrawRange> drawRanges;
  };

  // By reference: the structure keeps the merged ranges until its visibility changes, so a frame
  // that hides nothing new neither walks the tree nor allocates.
  static const std::vector<RKRibbonChainDrawRange> &visibleDrawRanges(const RibbonBuffers &buffers,
                                                                     const RKRenderRibbonSource *ribbon);

  void deleteBuffers();
  void generateBuffers();
  void initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

  ComPtr<ID3D12PipelineState> _opaquePso;
  bool _opaquePsoReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<RibbonBuffers>> _buffers;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
