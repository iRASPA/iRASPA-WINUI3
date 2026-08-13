/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <string>
#include "directxshader.h"

class DirectXBlurShader : public DirectXShader
{
public:
  DirectXBlurShader() = default;
  ~DirectXBlurShader() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat);
  void resize(ID3D12Device *device, int width, int height);
  void paint(ID3D12GraphicsCommandList *commandList,
             ID3D12Resource *glowTexture,
             D3D12_RESOURCE_STATES &glowState);

  ID3D12Resource *blurredTexture() const { return _vertical.Get(); }
  D3D12_GPU_DESCRIPTOR_HANDLE blurredSrv() const;
  ID3D12DescriptorHeap *srvHeap() const { return _srvHeap.Get(); }

private:
  struct PassTargets
  {
    ComPtr<ID3D12Resource> texture;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  };

  void createFullscreenRootSignature(ID3D12Device *device);
  void createPipelines(ID3D12Device *device, DXGI_FORMAT rtvFormat);
  void createTargets(ID3D12Device *device, int width, int height);
  void createFullscreenQuad(ID3D12Device *device);
  void drawFullscreen(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pso,
                      D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_GPU_DESCRIPTOR_HANDLE srv);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _copyPso;
  ComPtr<ID3D12PipelineState> _horizontalPso;
  ComPtr<ID3D12PipelineState> _verticalPso;
  ComPtr<ID3D12DescriptorHeap> _rtvHeap;
  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  ComPtr<ID3D12Resource> _vertexBuffer;
  ComPtr<ID3D12Resource> _indexBuffer;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};
  D3D12_INDEX_BUFFER_VIEW _ibv{};
  UINT _indexCount = 0;

  PassTargets _down;
  PassTargets _horizontal;
  ComPtr<ID3D12Resource> _vertical;
  D3D12_CPU_DESCRIPTOR_HANDLE _verticalRtv{};
  D3D12_CPU_DESCRIPTOR_HANDLE _verticalSrvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE _verticalSrvGpu{};
  D3D12_RESOURCE_STATES _verticalState = D3D12_RESOURCE_STATE_COMMON;

  int _width = 0;
  int _height = 0;
  bool _ready = false;
  DXGI_FORMAT _rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  // Temporary glow SRV slot (index 3 in srv heap): down=0, horiz=1, vert=2, glowInput=3
  D3D12_CPU_DESCRIPTOR_HANDLE _glowSrvCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE _glowSrvGpu{};

  static const std::string _fullscreenVertexShaderSource;
  static const std::string _copyPixelShaderSource;
  static const std::string _blurHorizontalVertexShaderSource;
  static const std::string _blurVerticalVertexShaderSource;
  static const std::string _blurPixelShaderSource;
};
