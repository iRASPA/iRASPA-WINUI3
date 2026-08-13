/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxribbonshader.h"
#include "directxshader.h"
#include "rkribbonmesh.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

// A selected residue or secondary-structure segment, drawn as the same swept geometry pushed out
// along its normals. The three styles are the atom ones: worley noise and stripes are blended over
// the scene, while glow goes into the glow target and is blurred with the atom glow. Which style is
// used is the structure's atom selection style, as in OpenGL and Metal, where the appearance pane
// writes one setting for atoms, bonds and ribbons alike.
class DirectXRibbonSelectionShader : public DirectXShader
{
public:
  DirectXRibbonSelectionShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, DXGI_FORMAT glowRtvFormat);
  // The mesh lives in the main ribbon shader; this one only ever draws parts of it.
  void setRibbonShader(const DirectXRibbonShader *ribbonShader) { _ribbonShader = ribbonShader; }
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);

  void paintOverlay(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride);
  void paintGlow(ID3D12GraphicsCommandList *commandList,
                 D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                 UINT structureCBVStride);
  bool hasGlowWork() const;

private:
  // Which ranges are selected changes with the tree, not with the camera, so the answer is worked out
  // when the renderer reloads rather than once per frame.
  struct SelectionBuffers
  {
    std::vector<RKRibbonChainDrawRange> ranges;
  };

  void generateBuffers();
  ComPtr<ID3D12PipelineState> buildPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                       const std::string &vertexShaderSource,
                                       const std::string &pixelShaderSource,
                                       DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                       bool blended, bool multisampled);
  void paintStyle(ID3D12GraphicsCommandList *commandList,
                  D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                  UINT structureCBVStride,
                  RKSelectionStyle style, ID3D12PipelineState *pso);

  ComPtr<ID3D12PipelineState> _worleyPso;
  ComPtr<ID3D12PipelineState> _stripedPso;
  ComPtr<ID3D12PipelineState> _glowPso;

  const DirectXRibbonShader *_ribbonShader = nullptr;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<SelectionBuffers>> _buffers;

  static const std::string _worleyVertexShaderSource;
  static const std::string _worleyPixelShaderSource;
  static const std::string _stripedVertexShaderSource;
  static const std::string _stripedPixelShaderSource;
  static const std::string _glowVertexShaderSource;
  static const std::string _glowPixelShaderSource;
};
