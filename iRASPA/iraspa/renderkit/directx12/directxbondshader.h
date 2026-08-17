/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "directxbondimposter.h"
#include "directxdevicehelpers.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXBondShader : public DirectXShader
{
public:
  DirectXBondShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride);
  // Geometry only; the caller has already bound the picking PSO, which rasterizes the same hulls.
  void drawPickGeometry(ID3D12GraphicsCommandList *commandList,
                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                        UINT structureCBVStride,
                        bool internal) const;

private:
  struct MeshBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
  };

  struct StructureBondBuffers
  {
    MeshBuffers all;
    MeshBuffers single;
    MeshBuffers doubleBond;
    MeshBuffers partialDouble;
    MeshBuffers triple;
  };

  void deleteBuffers();
  void generateBuffers();
  void initializeImposterPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                             bool external, bool perSample);
  static void uploadInstances(ID3D12Device *device, MeshBuffers &bufs,
                              const std::vector<RKInPerInstanceAttributesBonds> &instances);
  static void drawMesh(ID3D12GraphicsCommandList *commandList,
                       const DirectXDeviceHelpers::IndexedMesh &mesh,
                       const MeshBuffers &bufs);
  void paintBondSet(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride,
                    const std::vector<std::vector<StructureBondBuffers>> &buffers) const;

  // The quality path shades per-sample, the fast path once per pixel; the renderer picks between
  // them per frame.
  ComPtr<ID3D12PipelineState> _imposterPso;
  ComPtr<ID3D12PipelineState> _imposterPerPixelPso;
  ComPtr<ID3D12PipelineState> _externalImposterPso;
  ComPtr<ID3D12PipelineState> _externalImposterPerPixelPso;
  bool _imposterPsoReady = false;
  bool _imposterPerPixelPsoReady = false;
  bool _externalImposterPsoReady = false;
  bool _externalImposterPerPixelPsoReady = false;

  DirectXBondImposter::Hulls _hulls;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBondBuffers>> _internalBuffers;
  std::vector<std::vector<StructureBondBuffers>> _externalBuffers;

  static const std::string _imposterVertexShaderSource;
  static std::string imposterPixelShaderSource(bool external, bool perSample);
};
