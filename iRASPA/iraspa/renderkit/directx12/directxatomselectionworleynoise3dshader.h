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

// Besides drawing the Worley-noise overlay this shader owns the selected-atom instance buffers and
// the imposter quad that the striped and glow overlays draw with as well.
class DirectXAtomSelectionWorleyNoise3DShader : public DirectXShader
{
public:
  DirectXAtomSelectionWorleyNoise3DShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride,
             bool orthographic);

  bool isInstanceReady(size_t i, size_t j) const;
  UINT instanceCount(size_t i, size_t j) const;
  D3D12_VERTEX_BUFFER_VIEW instanceVbv(size_t i, size_t j) const;
  bool isQuadReady() const;
  UINT quadIndexCount() const;
  D3D12_VERTEX_BUFFER_VIEW quadVbv() const;
  D3D12_INDEX_BUFFER_VIEW quadIbv() const;
  bool hasGlowWork() const;

private:
  void deleteBuffers();
  void generateBuffers();
  void initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, bool orthographic);

  struct StructureBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
  };

  DirectXDeviceHelpers::IndexedMesh _quadMesh;

  ComPtr<ID3D12PipelineState> _orthographicPso;
  ComPtr<ID3D12PipelineState> _perspectivePso;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBuffers>> _buffers;
  bool _orthographicPsoReady = false;
  bool _perspectivePsoReady = false;

  static std::string vertexShaderSource(bool orthographic);
  static std::string pixelShaderSource(bool orthographic);
};
