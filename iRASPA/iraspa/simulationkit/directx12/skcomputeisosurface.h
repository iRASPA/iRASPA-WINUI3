/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <vector>
#include <mathkit.h>
#include "skdx12.h"

class SKComputeIsosurface : public SKDx12
{
public:
  SKComputeIsosurface(SKDx12 const &) = delete;
  void operator=(SKComputeIsosurface const &) = delete;

  static std::vector<float4> computeIsosurface(int3 dimensions, std::vector<float> *voxels, double isoValue);
  static std::vector<float4> computeIsosurfaceCPUImplementation(int3 dimensions, std::vector<float> *voxels, double isoValue) noexcept;

private:
  SKComputeIsosurface();
  ~SKComputeIsosurface() override = default;

  static SKComputeIsosurface &getInstance()
  {
    static SKComputeIsosurface instance;
    return instance;
  }

  std::vector<float4> computeIsosurfaceGPUImplementation(int3 dimensions, std::vector<float> *voxels, double isoValue);
  bool ensureTraversePso(int powerOfTwo);

  bool createPso(const char *entry, ComPtr<ID3D12RootSignature> &rs, ComPtr<ID3D12PipelineState> &pso,
                 const D3D12_ROOT_PARAMETER *params, UINT numParams);

  ComPtr<ID3D12RootSignature> _classifyRs;
  ComPtr<ID3D12PipelineState> _classifyPso;
  ComPtr<ID3D12RootSignature> _constructBaseRs;
  ComPtr<ID3D12PipelineState> _constructBasePso;
  ComPtr<ID3D12RootSignature> _constructRs;
  ComPtr<ID3D12PipelineState> _constructPso;
  ComPtr<ID3D12RootSignature> _traverseRs;
  ComPtr<ID3D12PipelineState> _traversePso[10];

  ComPtr<ID3D12DescriptorHeap> _heap;
  UINT _descriptorSize = 0;

  static const std::string _marchingCubesKernel;
};
