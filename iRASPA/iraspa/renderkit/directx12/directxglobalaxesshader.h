/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <memory>
#include <string>
#include "rkstring.h"
#include "directxshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXGlobalAxesShader : public DirectXShader
{
public:
  DirectXGlobalAxesShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderDataSource(std::shared_ptr<RKRenderDataSource> source);
  void reloadData(ID3D12Device *device);
  void ensureTexturesUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *commandList);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
             D3D12_GPU_VIRTUAL_ADDRESS lightsCBV,
             D3D12_GPU_VIRTUAL_ADDRESS globalAxesCBV,
             int width, int height);

private:
  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
  };

  struct TextBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
    RKString fontName;
  };

  void initializeBackgroundPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                               DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeSystemPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                           DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeTextPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void reloadBackground(ID3D12Device *device);
  void reloadSystem(ID3D12Device *device);
  void reloadText(ID3D12Device *device);
  void setAxesViewport(ID3D12GraphicsCommandList *commandList, int width, int height);

  std::shared_ptr<RKRenderDataSource> _dataSource;

  ComPtr<ID3D12PipelineState> _backgroundPso;
  ComPtr<ID3D12PipelineState> _systemPso;
  ComPtr<ID3D12PipelineState> _textPso;
  bool _backgroundPsoReady = false;
  bool _systemPsoReady = false;
  bool _textPsoReady = false;

  MeshBuffers _backgroundBuffers;
  MeshBuffers _systemBuffers;
  TextBuffers _textBuffers;

  static const std::string _backgroundVertexShaderSource;
  static const std::string _backgroundPixelShaderSource;
  static const std::string _systemVertexShaderSource;
  static const std::string _systemPixelShaderSource;
  static const std::string _textVertexShaderSource;
  static const std::string _textPixelShaderSource;
};
