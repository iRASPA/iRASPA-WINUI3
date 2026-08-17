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
  _atomOrthographicImposterShader.loadShader(device);
  _atomPerspectiveImposterShader.loadShader(device);
  _atomAmbientOcclusionShader.loadShader(device);
}

void DirectXAtomShader::initialize(ID3D12Device *device, ID3D12CommandQueue *queue,
                                   ID3D12RootSignature *rootSignature,
                                   DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
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
}

void DirectXAtomShader::reloadData(ID3D12Device *device)
{
  _atomSphereShader.reloadData(device);
  _atomOrthographicImposterShader.reloadData(device);
  _atomPerspectiveImposterShader.reloadData(device);
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
                              std::shared_ptr<RKCamera> camera)
{
  // Atoms are always ray-traced sphere imposters; the only choice left is which projection the
  // ray is set up for. The quality/speed trade-off is made inside the imposter shaders, which
  // shade per-sample or per-pixel depending on the render quality of this frame.
  DirectXAmbientOcclusionShadowMapShader *ao = &_atomAmbientOcclusionShader;

  if (camera && camera->isOrthographic())
  {
    _atomOrthographicImposterShader.paint(commandList, structureCBVBase, structureCBVStride, ao);
  }
  else
  {
    _atomPerspectiveImposterShader.paint(commandList, structureCBVBase, structureCBVStride, ao);
  }
}
