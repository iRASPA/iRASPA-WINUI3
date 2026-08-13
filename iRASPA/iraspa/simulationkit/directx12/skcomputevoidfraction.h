/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <vector>
#include <mathkit.h>
#include "skdx12.h"

class SKComputeVoidFraction : public SKDx12
{
public:
  SKComputeVoidFraction(SKDx12 const &) = delete;
  void operator=(SKComputeVoidFraction const &) = delete;

  static double ComputeVoidFraction(std::vector<float> *voxels);

private:
  SKComputeVoidFraction();
  ~SKComputeVoidFraction() override = default;

  static SKComputeVoidFraction &getInstance()
  {
    static SKComputeVoidFraction instance;
    return instance;
  }

  double computeVoidFractionGPU(std::vector<float> *voxels);
  static double computeVoidFractionCPU(std::vector<float> *voxels);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
  UINT _descriptorSize = 0;
  static constexpr UINT kThreadGroupSize = 256;
  static const std::string _voidFractionKernel;
};
