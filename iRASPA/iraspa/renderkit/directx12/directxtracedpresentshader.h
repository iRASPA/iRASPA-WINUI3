/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#pragma once

#include <string>
#include "directxshader.h"

/// Draws the path tracer's finished image onto the back buffer, as a quad covering it.
///
/// A copy would be the obvious thing, and is what the export path does, but the traced image is
/// always RGBA while a swap chain may store its channels the other way round, and a copy cannot
/// convert between the two. Sampling it in a pixel shader can, the output being written in whatever
/// order the render target asks for.
class DirectXTracedPresentShader : public DirectXShader
{
public:
  DirectXTracedPresentShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *sceneRootSignature,
                  DXGI_FORMAT rtvFormat);
  void release();

  bool isReady() const { return _ready; }

  /// Points the shader at \a traced, which must be in PIXEL_SHADER_RESOURCE, and draws it. The view
  /// is rewritten per call because the tracer reallocates its image whenever the view is resized.
  void paint(ID3D12Device *device, ID3D12GraphicsCommandList *commandList, ID3D12Resource *traced);

private:
  void createFullscreenQuad(ID3D12Device *device);

  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12DescriptorHeap> _srvHeap;
  ComPtr<ID3D12Resource> _vertexBuffer;
  ComPtr<ID3D12Resource> _indexBuffer;
  D3D12_VERTEX_BUFFER_VIEW _vbv{};
  D3D12_INDEX_BUFFER_VIEW _ibv{};
  UINT _indexCount = 0;
  bool _ready = false;

  static const std::string _vertexShaderSource;
  static const std::string _pixelShaderSource;
};
