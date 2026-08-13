/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include "directxshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXBoundingBoxShader : public DirectXShader
{
public:
  DirectXBoundingBoxShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderDataSource(std::shared_ptr<RKRenderDataSource> source);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList);

private:
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

  void initializeSpherePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                           DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeCylinderPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

  ComPtr<ID3D12PipelineState> _spherePso;
  ComPtr<ID3D12PipelineState> _cylinderPso;
  std::shared_ptr<RKRenderDataSource> _dataSource;
  MeshBuffers _sphereBuffers;
  MeshBuffers _cylinderBuffers;
  bool _spherePsoReady = false;
  bool _cylinderPsoReady = false;

  static const std::string _sphereVertexShaderSource;
  static const std::string _spherePixelShaderSource;
  static const std::string _cylinderVertexShaderSource;
  static const std::string _cylinderPixelShaderSource;
};
