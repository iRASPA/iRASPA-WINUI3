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

class DirectXAtomSelectionWorleyNoise3DShader;

class DirectXAtomSelectionGlowShader : public DirectXShader
{
public:
  DirectXAtomSelectionGlowShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride,
             const DirectXAtomSelectionWorleyNoise3DShader &instanceSource,
             bool orthographic);

private:
  void initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, bool orthographic);

  ComPtr<ID3D12PipelineState> _orthographicPso;
  ComPtr<ID3D12PipelineState> _perspectivePso;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  bool _orthographicPsoReady = false;
  bool _perspectivePsoReady = false;

  static std::string vertexShaderSource(bool orthographic);
  static std::string pixelShaderSource(bool orthographic);
};
