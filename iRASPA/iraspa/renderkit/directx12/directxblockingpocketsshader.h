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
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

/// Draws the blocking pockets of a structure as translucent spheres of the radius they were read with.
///
/// The pockets enclose the atoms and the iso-surface they overlap, so they are drawn in the transparent
/// pass in back-to-front order alongside the iso-surface and the transparent primitives.
class DirectXBlockingPocketsShader : public DirectXShader
{
public:
  DirectXBlockingPocketsShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);

  /// Draws the blocking pockets of a single structure. The renderer calls this in back-to-front order
  /// so that pockets of different structures blend in the right order.
  void paintTransparent(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        D3D12_GPU_VIRTUAL_ADDRESS blockingPocketCBVBase,
                        UINT blockingPocketCBVStride,
                        size_t sceneIndex, size_t movieIndex, size_t structureIndex);

private:
  void initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

  DirectXDeviceHelpers::IndexedMesh _sphere;

  // Front-cull draws the far wall of a pocket, so that it blends underneath the near wall.
  ComPtr<ID3D12PipelineState> _frontCullPso;
  ComPtr<ID3D12PipelineState> _backCullPso;
  bool _psoReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  struct InstanceBuffer
  {
    ComPtr<ID3D12Resource> buffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    UINT instanceCount = 0;
  };
  std::vector<std::vector<InstanceBuffer>> _instanceBuffers;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
