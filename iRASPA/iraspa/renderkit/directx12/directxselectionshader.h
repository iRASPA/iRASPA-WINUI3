/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <vector>
#include "directxatomselectionworleynoise3dshader.h"
#include "directxatomselectionstripesshader.h"
#include "directxatomselectionglowshader.h"
#include "directxbondselectionshader.h"
#include "rkrenderkitprotocols.h"

class DirectXSelectionShader
{
public:
  DirectXSelectionShader() = default;

  void initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat, DXGI_FORMAT glowRtvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void reloadSelectionData(ID3D12Device *device);

  void paintOverlays(ID3D12GraphicsCommandList *commandList,
                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                     UINT structureCBVStride);
  void paintGlow(ID3D12GraphicsCommandList *commandList,
                 D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                 UINT structureCBVStride);
  bool hasGlowWork() const;

private:
  DirectXAtomSelectionWorleyNoise3DShader _atomWorley;
  DirectXAtomSelectionStripesShader _atomStripes;
  DirectXAtomSelectionGlowShader _atomGlow;
  DirectXBondSelectionShader _bondSelection;
};
