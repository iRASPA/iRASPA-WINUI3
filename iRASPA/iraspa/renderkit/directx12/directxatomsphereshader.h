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

// The atoms themselves are drawn as ray-traced sphere imposters; this class only owns the
// per-structure atom instance buffers shared by those shaders, by picking and by the
// ambient-occlusion bake.
class DirectXAtomSphereShader
{
public:
  DirectXAtomSphereShader() = default;

  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void reloadData(ID3D12Device *device);

  bool isInstanceReady(size_t i, size_t j) const;
  UINT instanceCount(size_t i, size_t j) const;
  D3D12_VERTEX_BUFFER_VIEW instanceVbv(size_t i, size_t j) const;

private:
  struct StructureBuffers
  {
    ComPtr<ID3D12Resource> instanceBuffer;
    D3D12_VERTEX_BUFFER_VIEW instanceVbv{};
    UINT instanceCount = 0;
  };

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> _renderStructures;
  std::vector<std::vector<StructureBuffers>> _buffers;
};
