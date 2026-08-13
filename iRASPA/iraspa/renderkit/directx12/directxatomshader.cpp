/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomshader.h"

DirectXAtomShader::DirectXAtomShader()
  : _atomOrthographicImposterShader(_atomSphereShader),
    _atomPerspectiveImposterShader(_atomSphereShader, _atomOrthographicImposterShader),
    _atomAmbientOcclusionShader(_atomSphereShader, _atomOrthographicImposterShader)
{
}

void DirectXAtomShader::loadShader(ID3D12Device *device)
{
  _atomSphereShader.loadShader(device);
  _atomOrthographicImposterShader.loadShader(device);
  _atomPerspectiveImposterShader.loadShader(device);
  _atomAmbientOcclusionShader.loadShader(device);
}

void DirectXAtomShader::initialize(ID3D12Device *device, ID3D12CommandQueue *queue,
                                   ID3D12RootSignature *rootSignature,
                                   DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  _atomSphereShader.initialize(device, rootSignature, rtvFormat, dsvFormat);
  _atomOrthographicImposterShader.initialize(device, rootSignature, rtvFormat, dsvFormat);
  _atomPerspectiveImposterShader.initialize(device, rootSignature, rtvFormat, dsvFormat);
  _atomAmbientOcclusionShader.initialize(device, queue);
}

void DirectXAtomShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = structures;
  _atomSphereShader.setRenderStructures(structures);
  _atomOrthographicImposterShader.setRenderStructures(structures);
  _atomPerspectiveImposterShader.setRenderStructures(structures);
  _atomAmbientOcclusionShader.setRenderStructures(structures);
  _numberOfAtoms = 0;
}

void DirectXAtomShader::reloadData(ID3D12Device *device)
{
  _atomSphereShader.reloadData(device);
  _atomOrthographicImposterShader.reloadData(device);
  _atomPerspectiveImposterShader.reloadData(device);

  _numberOfAtoms = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
      _numberOfAtoms += _atomSphereShader.instanceCount(i, j);
  }
}

void DirectXAtomShader::reloadAmbientOcclusionData(ID3D12Device *device, ID3D12CommandQueue *queue,
                                                   std::shared_ptr<RKRenderDataSource> dataSource,
                                                   RKRenderQuality quality)
{
  _atomAmbientOcclusionShader.reloadData(device, queue, dataSource, quality);
}

void DirectXAtomShader::invalidateCachedAmbientOcclusionTextures(
    std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  _atomAmbientOcclusionShader.invalidateCachedAmbientOcclusionTexture(structures);
}

void DirectXAtomShader::paint(ID3D12GraphicsCommandList *commandList,
                              D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                              UINT structureCBVStride,
                              std::shared_ptr<RKCamera> camera,
                              RKRenderQuality quality)
{
  DirectXAmbientOcclusionShadowMapShader *ao = &_atomAmbientOcclusionShader;

  if ((quality == RKRenderQuality::high && _numberOfAtoms < 10000) || quality == RKRenderQuality::picture)
  {
    _atomSphereShader.paint(commandList, structureCBVBase, structureCBVStride, ao);
  }
  else if (camera && camera->isOrthographic())
  {
    _atomOrthographicImposterShader.paint(commandList, structureCBVBase, structureCBVStride, ao);
  }
  else
  {
    _atomPerspectiveImposterShader.paint(commandList, structureCBVBase, structureCBVStride, ao);
  }
}
