/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomsphereshader.h"
#include "directxdevicehelpers.h"
#include <cstddef>

void DirectXAtomSphereShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _buffers.clear();
  _renderStructures = std::move(structures);

  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXAtomSphereShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      StructureBuffers &bufs = _buffers[i][j];
      bufs = StructureBuffers{};

      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      std::vector<RKInPerInstanceAttributesAtoms> atomData = source->renderAtoms();
      DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                                 bufs.instanceCount, atomData.data(), atomData.size(),
                                                 sizeof(RKInPerInstanceAttributesAtoms));
    }
  }
}

bool DirectXAtomSphereShader::isInstanceReady(size_t i, size_t j) const
{
  return i < _buffers.size() && j < _buffers[i].size()
      && _buffers[i][j].instanceBuffer != nullptr
      && _buffers[i][j].instanceCount > 0;
}

UINT DirectXAtomSphereShader::instanceCount(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return 0;
  return _buffers[i][j].instanceCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSphereShader::instanceVbv(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return {};
  return _buffers[i][j].instanceVbv;
}
