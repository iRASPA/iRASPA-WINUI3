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
  // Geometry only; the caller has already bound the picking PSO.
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
  void initializeInternalPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeExternalPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeStencilPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                            DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void initializeBoxPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                        DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void uploadBoxMesh(ID3D12Device *device);
  static void uploadInstances(ID3D12Device *device, MeshBuffers &bufs,
                              const std::vector<RKInPerInstanceAttributesBonds> &instances);
  static void drawMesh(ID3D12GraphicsCommandList *commandList,
                       const DirectXDeviceHelpers::IndexedMesh &mesh,
                       const MeshBuffers &bufs);
  void paintBondSet(ID3D12GraphicsCommandList *commandList,
                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                    UINT structureCBVStride,
                    const std::vector<std::vector<StructureBondBuffers>> &buffers) const;
  void paintExternalStencilAndBox(ID3D12GraphicsCommandList *commandList,
                                  D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                  UINT structureCBVStride);

  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12PipelineState> _externalPso;
  ComPtr<ID3D12PipelineState> _stencilPso;
  ComPtr<ID3D12PipelineState> _boxPso;
  DirectXDeviceHelpers::IndexedMesh _boxMesh;
  DirectXDeviceHelpers::IndexedMesh _meshSingle;
  DirectXDeviceHelpers::IndexedMesh _meshDouble;
  DirectXDeviceHelpers::IndexedMesh _meshPartialDouble;
  DirectXDeviceHelpers::IndexedMesh _meshTriple;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBondBuffers>> _internalBuffers;
  std::vector<std::vector<StructureBondBuffers>> _externalBuffers;
  bool _psoReady = false;
  bool _externalPsoReady = false;
  bool _stencilPsoReady = false;
  bool _boxPsoReady = false;

  static const std::string _vertexShaderSource;
  static const std::string _externalVertexShaderSource;
  static const std::string _stencilVertexShaderSource;
  static const std::string _pixelShaderSource;
  static const std::string _stencilPixelShaderSource;
  static const std::string _boxVertexShaderSource;
  static const std::string _boxPixelShaderSource;
};
