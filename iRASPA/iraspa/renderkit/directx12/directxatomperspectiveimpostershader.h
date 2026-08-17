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

class DirectXAtomSphereShader;
class DirectXAtomOrthographicImposterShader;

class DirectXAtomPerspectiveImposterShader : public DirectXShader
{
public:
  DirectXAtomPerspectiveImposterShader(DirectXAtomSphereShader &atomSphereShader,
                                       DirectXAtomOrthographicImposterShader &orthoImposter);

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride,
             class DirectXAmbientOcclusionShadowMapShader *aoShader);

private:
  void initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, bool perSample);

  DirectXAtomSphereShader &_atomSphereShader;
  DirectXAtomOrthographicImposterShader &_orthoImposter;
  // The quality path shades per-sample, the fast path once per pixel; the renderer picks between
  // them per frame.
  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12PipelineState> _perPixelPso;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  bool _psoReady = false;
  bool _perPixelPsoReady = false;

  static const std::string _vertexShaderSource;
  static std::string pixelShaderSource(bool perSample);
};
