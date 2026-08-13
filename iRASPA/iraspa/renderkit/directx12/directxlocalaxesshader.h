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

class DirectXLocalAxesShader : public DirectXShader
{
public:
  DirectXLocalAxesShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride);

private:
  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
  };

  void deleteBuffers();
  void generateBuffers();
  void initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

  ComPtr<ID3D12PipelineState> _pso;
  bool _psoReady = false;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<MeshBuffers>> _buffers;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
