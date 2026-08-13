/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <string>
#include "directxshader.h"

class DirectXCompositeShader : public DirectXShader
{
public:
  DirectXCompositeShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *sceneRootSignature, DXGI_FORMAT rtvFormat);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_DESCRIPTOR_HANDLE blurredSrv);

private:
  void createFullscreenQuad(ID3D12Device *device);

  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12Resource> _vertexBuffer;
  ComPtr<ID3D12Resource> _indexBuffer;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};
  D3D12_INDEX_BUFFER_VIEW _ibv{};
  UINT _indexCount = 0;
  bool _ready = false;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
