/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxselectionshader.h"

void DirectXSelectionShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                        DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                        DXGI_FORMAT glowRtvFormat)
{
  _atomWorley.initialize(device, rootSignature, rtvFormat, dsvFormat);
  _atomStripes.initialize(device, rootSignature, rtvFormat, dsvFormat);
  _atomGlow.initialize(device, rootSignature, glowRtvFormat, dsvFormat);
  _bondSelection.initialize(device, rootSignature, rtvFormat, glowRtvFormat, dsvFormat);
}

void DirectXSelectionShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _atomWorley.setRenderStructures(structures);
  _atomStripes.setRenderStructures(structures);
  _atomGlow.setRenderStructures(structures);
  _bondSelection.setRenderStructures(structures);
}

void DirectXSelectionShader::reloadData(ID3D12Device *device)
{
  reloadSelectionData(device);
}

void DirectXSelectionShader::reloadSelectionData(ID3D12Device *device)
{
  _atomWorley.reloadData(device);
  _atomStripes.reloadData(device);
  _atomGlow.reloadData(device);
  _bondSelection.reloadData(device);
}

void DirectXSelectionShader::paintOverlays(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride,
                                           bool orthographic)
{
  // Bonds first, so an atom's overlay wins where the two touch.
  _bondSelection.paintOverlays(commandList, structureCBVBase, structureCBVStride);
  _atomWorley.paint(commandList, structureCBVBase, structureCBVStride, orthographic);
  _atomStripes.paint(commandList, structureCBVBase, structureCBVStride, _atomWorley, orthographic);
}

void DirectXSelectionShader::paintGlow(ID3D12GraphicsCommandList *commandList,
                                       D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                       UINT structureCBVStride,
                                       bool orthographic)
{
  _atomGlow.paint(commandList, structureCBVBase, structureCBVStride, _atomWorley, orthographic);
  _bondSelection.paintGlow(commandList, structureCBVBase, structureCBVStride);
}

bool DirectXSelectionShader::hasGlowWork() const
{
  return _atomWorley.hasGlowWork() || _bondSelection.hasGlowWork();
}
