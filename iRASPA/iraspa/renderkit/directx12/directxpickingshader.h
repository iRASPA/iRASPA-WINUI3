/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXRibbonShader;
class DirectXAtomSphereShader;
class DirectXAtomOrthographicImposterShader;
class DirectXBondShader;
class DirectXObjectShader;

class DirectXPickingShader : public DirectXShader
{
public:
  DirectXPickingShader() = default;
  ~DirectXPickingShader() override;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  ID3D12CommandQueue *commandQueue);
  // The ribbon is picked from the geometry the ribbon shader already uploaded.
  void setRibbonShader(const DirectXRibbonShader *ribbonShader) { _ribbonShader = ribbonShader; }
  void setAtomSphereShader(const DirectXAtomSphereShader *shader) { _atomSphereShader = shader; }
  void setAtomOrthographicImposterShader(const DirectXAtomOrthographicImposterShader *shader)
  {
    _atomOrthoImposterShader = shader;
  }
  void setBondShader(const DirectXBondShader *shader) { _bondShader = shader; }
  void setObjectShader(const DirectXObjectShader *shader) { _objectShader = shader; }
  void resize(ID3D12Device *device, int width, int height);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  // Atoms are picked from the same sphere imposters the scene draws, which needs the projection the
  // camera is using; bonds read theirs out of the projection matrix.
  void setOrthographic(bool orthographic) { _orthographic = orthographic; }

  // Called each frame before the main scene (logical viewport).
  void paintPickPass(ID3D12GraphicsCommandList *commandList,
                     D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                     UINT structureCBVStride);

  // Dedicated pick pass + readback (preferred for correctness).
  std::array<int, 4> pickTexture(D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                                 D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                 UINT structureCBVStride,
                                 int x, int y, int width, int height);

private:
  void createPickTargets(ID3D12Device *device, int width, int height);
  void ensurePickCommandResources(ID3D12Device *device);
  void waitForPickGPU();
  void drawPickContents(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride);
  void drawAtomPick(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride);
  void drawBondPick(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride,
                    ID3D12PipelineState *pso, bool psoReady, bool internal);
  void drawObjectPick(ID3D12GraphicsCommandList *commandList,
                      D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                      UINT structureCBVStride);
  void drawRibbonPick(ID3D12GraphicsCommandList *commandList,
                      D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                      UINT structureCBVStride);

  ID3D12Device *_device = nullptr;
  ID3D12CommandQueue *_commandQueue = nullptr;
  ID3D12RootSignature *_rootSignature = nullptr;

  ComPtr<ID3D12CommandAllocator> _pickAllocator;
  ComPtr<ID3D12GraphicsCommandList> _pickCommandList;
  ComPtr<ID3D12Fence> _pickFence;
  HANDLE _pickFenceEvent = nullptr;
  UINT64 _pickFenceValue = 0;

  ComPtr<ID3D12DescriptorHeap> _rtvHeap;
  ComPtr<ID3D12DescriptorHeap> _dsvHeap;
  ComPtr<ID3D12Resource> _pickColor;
  ComPtr<ID3D12Resource> _pickDepth;
  ComPtr<ID3D12Resource> _readbackBuffer;
  D3D12_RESOURCE_STATES _pickColorState = D3D12_RESOURCE_STATE_COMMON;
  int _width = 0;
  int _height = 0;

  ComPtr<ID3D12PipelineState> _atomPso;
  ComPtr<ID3D12PipelineState> _atomPerspectivePso;
  ComPtr<ID3D12PipelineState> _bondPso;
  ComPtr<ID3D12PipelineState> _externalBondPso;
  ComPtr<ID3D12PipelineState> _objectPso;
  ComPtr<ID3D12PipelineState> _ribbonPso;
  bool _atomPsoReady = false;
  bool _atomPerspectivePsoReady = false;
  bool _bondPsoReady = false;
  bool _externalBondPsoReady = false;
  bool _objectPsoReady = false;
  bool _ribbonPsoReady = false;

  const DirectXRibbonShader *_ribbonShader = nullptr;
  const DirectXAtomSphereShader *_atomSphereShader = nullptr;
  const DirectXAtomOrthographicImposterShader *_atomOrthoImposterShader = nullptr;
  const DirectXBondShader *_bondShader = nullptr;
  const DirectXObjectShader *_objectShader = nullptr;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  bool _orthographic = true;

  static std::string atomVertexShaderSource(bool orthographic);
  static std::string atomPixelShaderSource(bool orthographic);
  // Bonds are picked from the same ray-traced imposter hulls the scene pass draws, so the
  // identifier and the depth written here match the surface the user sees. Internal and external
  // bonds share the hull; only the external pixel shader clips at the unit cell.
  static const std::string _bondVertexShaderSource;
  static const std::string _bondPixelShaderSource;
  static const std::string _externalBondPixelShaderSource;
  static const std::string _objectVertexShaderSource;
  static const std::string _objectPixelShaderSource;
  static const std::string _ribbonVertexShaderSource;
  static const std::string _ribbonPixelShaderSource;
};
