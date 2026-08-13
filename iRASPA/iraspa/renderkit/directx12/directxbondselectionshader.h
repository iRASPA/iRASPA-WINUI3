/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "directxshader.h"
#include "directxdevicehelpers.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"

class DirectXBondSelectionShader : public DirectXShader
{
public:
  DirectXBondSelectionShader() = default;

  void loadShader(ID3D12Device *device) override;
  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT overlayRtvFormat, DXGI_FORMAT glowRtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void paintOverlays(ID3D12GraphicsCommandList *commandList,
                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                     UINT structureCBVStride);
  void paintGlow(ID3D12GraphicsCommandList *commandList,
                 D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                 UINT structureCBVStride);
  bool hasGlowWork() const;

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
  static void uploadInstances(ID3D12Device *device, MeshBuffers &bufs,
                              const std::vector<RKInPerInstanceAttributesBonds> &instances);
  static void drawMesh(ID3D12GraphicsCommandList *commandList,
                       const DirectXDeviceHelpers::IndexedMesh &mesh,
                       const MeshBuffers &bufs);
  void paintBondSet(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride,
                    const std::vector<std::vector<StructureBondBuffers>> &buffers,
                    RKSelectionStyle style);

  ComPtr<ID3D12PipelineState> _stripesPso;
  ComPtr<ID3D12PipelineState> _worleyPso;
  ComPtr<ID3D12PipelineState> _glowPso;
  ComPtr<ID3D12PipelineState> _externalStripesPso;
  ComPtr<ID3D12PipelineState> _externalWorleyPso;
  ComPtr<ID3D12PipelineState> _externalGlowPso;
  bool _stripesReady = false;
  bool _worleyReady = false;
  bool _glowReady = false;
  bool _externalStripesReady = false;
  bool _externalWorleyReady = false;
  bool _externalGlowReady = false;

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBondBuffers>> _internalBuffers;
  std::vector<std::vector<StructureBondBuffers>> _externalBuffers;
  DirectXDeviceHelpers::IndexedMesh _meshSingle;
  DirectXDeviceHelpers::IndexedMesh _meshDouble;
  DirectXDeviceHelpers::IndexedMesh _meshPartialDouble;
  DirectXDeviceHelpers::IndexedMesh _meshTriple;

  static const std::string _vertexShaderSource;
  static const std::string _externalVertexShaderSource;
  static const std::string _stripesPixelShaderSource;
  static const std::string _worleyPixelShaderSource;
  static const std::string _glowPixelShaderSource;
};
