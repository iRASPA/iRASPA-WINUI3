/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <memory>
#include <vector>
#include "directxatomsphereshader.h"
#include "directxatomorthographicimpostershader.h"
#include "directxatomperspectiveimpostershader.h"
#include "directxambientocclusionshadowmapshader.h"
#include "rkcamera.h"
#include "rkrenderuniforms.h"

class DirectXAtomShader
{
public:
  DirectXAtomShader();

  void loadShader(ID3D12Device *device);
  void initialize(ID3D12Device *device, ID3D12CommandQueue *queue,
                  ID3D12RootSignature *rootSignature,
                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat);
  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);
  void reloadAmbientOcclusionData(ID3D12Device *device, ID3D12CommandQueue *queue,
                                   std::shared_ptr<RKRenderDataSource> dataSource,
                                   RKRenderQuality quality);
  void invalidateCachedAmbientOcclusionTextures(std::vector<std::shared_ptr<RKRenderObject>> structures);
  void paint(ID3D12GraphicsCommandList *commandList,
             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
             UINT structureCBVStride,
             std::shared_ptr<RKCamera> camera,
             RKRenderQuality quality);

  ID3D12DescriptorHeap *aoSrvHeap() const { return _atomAmbientOcclusionShader.srvHeap(); }

  DirectXAtomSphereShader &atomSphereShader() { return _atomSphereShader; }
  DirectXAtomOrthographicImposterShader &atomOrthographicImposterShader()
  {
    return _atomOrthographicImposterShader;
  }

  // The atom bake owns the depth map, so the ribbon bake rides along with it.
  void setRibbonAmbientOcclusionShader(DirectXRibbonAmbientOcclusionShader *shader)
  {
    _atomAmbientOcclusionShader.setRibbonAmbientOcclusionShader(shader);
  }

private:
  DirectXAtomSphereShader _atomSphereShader;
  DirectXAtomOrthographicImposterShader _atomOrthographicImposterShader;
  DirectXAtomPerspectiveImposterShader _atomPerspectiveImposterShader;
  DirectXAmbientOcclusionShadowMapShader _atomAmbientOcclusionShader;

  size_t _numberOfAtoms = 0;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
};
