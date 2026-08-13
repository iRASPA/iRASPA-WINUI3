/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "rkrenderkitprotocols.h"

class DirectXBackgroundShader : public DirectXShader
{
public:
  DirectXBackgroundShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature, DXGI_FORMAT rtvFormat,
                  D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle);
  void reload(std::shared_ptr<RKRenderDataSource> source);
  // Call while command list is open (before or during paint). Returns true if an upload copy was recorded.
  bool ensureTextureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *commandList);
  void paint(ID3D12GraphicsCommandList *commandList, D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle);

  ID3D12PipelineState *pipelineState() const { return _pso.Get(); }

private:
  void setPendingRgba(UINT width, UINT height, const uint8_t *rgba, int srcPitchBytes);
  void setPendingWhite(UINT width, UINT height);

  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12Resource> _vertexBuffer;
  ComPtr<ID3D12Resource> _indexBuffer;
  ComPtr<ID3D12Resource> _texture;
  ComPtr<ID3D12Resource> _textureUpload;
  D3D12_CPU_DESCRIPTOR_HANDLE _srvCpuHandle{};
  D3D12_VERTEX_BUFFER_VIEW _vbv{};
  D3D12_INDEX_BUFFER_VIEW _ibv{};
  UINT _indexCount = 0;
  std::vector<uint8_t> _pendingRgba;
  UINT _pendingWidth = 0;
  UINT _pendingHeight = 0;
  bool _textureDirty = true;
  bool _initialized = false;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
