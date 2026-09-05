/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxdevicehelpers.h"
#include "directxshader.h"
#include "rkcamera.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

/// Draws the geometric accessible surface as sphere-imposter patches (Cocoa MetalGeometricSurfaceShader).
class DirectXGeometricSurface : public DirectXShader
{
public:
  DirectXGeometricSurface() = default;
  ~DirectXGeometricSurface() override = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);

  void paintOpaque(ID3D12GraphicsCommandList *commandList,
                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                   UINT structureCBVStride,
                   D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                   UINT isosurfaceCBVStride,
                   std::shared_ptr<RKCamera> camera);
  void paintTransparent(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                        UINT isosurfaceCBVStride,
                        std::shared_ptr<RKCamera> camera,
                        size_t sceneIndex, size_t movieIndex, size_t structureIndex);

private:
  struct PatchBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    ComPtr<ID3D12Resource> clipBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
  };

  void deleteBuffers();
  void generateBuffers();
  void initializeVertexBuffers(ID3D12Device *device);
  void initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void paint(ID3D12GraphicsCommandList *commandList, bool opaque,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase, UINT structureCBVStride,
             D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase, UINT isosurfaceCBVStride,
             std::shared_ptr<RKCamera> camera,
             size_t *sceneIndex, size_t *movieIndex, size_t *structureIndex);

  static std::string vertexShaderSource(bool orthographic);
  static std::string pixelShaderSource(bool orthographic, bool analyticCoverage);

  // [orthographic?0:1][opaque?0:1][perSample?0:1] — eight PSOs matching Cocoa.
  ComPtr<ID3D12PipelineState> _psos[2][2][2];
  bool _psoReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<PatchBuffers>> _buffers;
  DirectXDeviceHelpers::IndexedMesh _quadMesh;
};
