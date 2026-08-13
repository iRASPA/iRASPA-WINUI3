/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "directxdevicehelpers.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXAtomSphereShader : public DirectXShader
{
public:
  DirectXAtomSphereShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride,
             class DirectXAmbientOcclusionShadowMapShader *aoShader);

  // Shared instance buffers for imposters (valid after reloadData).
  bool isInstanceReady(size_t i, size_t j) const;
  UINT instanceCount(size_t i, size_t j) const;
  D3D12_VERTEX_BUFFER_VIEW instanceVbv(size_t i, size_t j) const;

  bool isSphereMeshReady() const;
  UINT sphereIndexCount() const;
  D3D12_VERTEX_BUFFER_VIEW sphereVbv() const;
  D3D12_INDEX_BUFFER_VIEW sphereIbv() const;

private:
  void deleteBuffers();
  void generateBuffers();

  struct StructureBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
  };

  DirectXDeviceHelpers::IndexedMesh _sphereMesh;

  ComPtr<ID3D12PipelineState> _pso;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBuffers>> _buffers;
  bool _psoReady = false;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
