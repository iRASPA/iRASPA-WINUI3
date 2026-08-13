/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "rkstring.h"
#include "directxshader.h"
#include "rkfontatlas.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

struct DirectXFontAtlasGpu
{
  std::unique_ptr<RKFontAtlas> cpu;
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12Resource> uploadBuffer;
  ComPtr<ID3D12DescriptorHeap> srvHeap;
  bool uploaded = false;
};

class DirectXTextRenderingShader : public DirectXShader
{
public:
  DirectXTextRenderingShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  // Records font texture uploads if needed (call while command list is open for copies).
  void ensureTexturesUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *commandList);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride);

  static DirectXFontAtlasGpu *getOrCreateFontAtlas(const RKString &fontName, ID3D12Device *device);
  static void uploadFontAtlasTexture(DirectXFontAtlasGpu *entry, ID3D12Device *device,
                                     ID3D12GraphicsCommandList *commandList);

private:
  struct StructureBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
    RKString fontName;
  };

  void deleteBuffers();
  void generateBuffers();
  void initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);

  ComPtr<ID3D12PipelineState> _pso;
  bool _psoReady = false;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBuffers>> _buffers;

  static std::unordered_map<std::string, std::unique_ptr<DirectXFontAtlasGpu>> _fontCache;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
