/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <optional>
#include <mathkit.h>
#include "skdx12.h"

class SKComputeEnergyGrid : public SKDx12
{
public:
  enum class GridSizeType : int64_t
  {
    custom = 0,
    size2x2x2 = 1,
    size4x4x4 = 2,
    size8x8x8 = 3,
    size16x16x16 = 4,
    size32x32x32 = 5,
    size64x64x64 = 6,
    size128x128x128 = 7,
    size256x256x256 = 8,
    size512x512x512 = 9,
    multiple_values = 10
  };

  SKComputeEnergyGrid(SKDx12 const &) = delete;
  void operator=(SKComputeEnergyGrid const &) = delete;

  static std::vector<float> ComputeEnergyGrid(int3 size, double2 probeParameter,
                                              std::vector<double3> positions, std::vector<double2> potentialParameters,
                                              double3x3 unitCell, int3 numberOfReplicas);
  static std::vector<float> computeEnergyGrid(int3 size, double2 probeParameter,
                                              std::vector<double3> positions, std::vector<double2> potentialParameters,
                                              double3x3 unitCell, int3 numberOfReplicas)
  {
    return ComputeEnergyGrid(size, probeParameter, positions, potentialParameters, unitCell, numberOfReplicas);
  }
  static std::vector<float> computeEnergyGridCPUImplementation(int3 size, double2 probeParameter,
                                                               std::vector<double3> positions, std::vector<double2> potentialParameters,
                                                               double3x3 unitCell, int3 numberOfReplicas) noexcept;

private:
  SKComputeEnergyGrid();
  ~SKComputeEnergyGrid() override = default;

  static SKComputeEnergyGrid &getInstance()
  {
    static SKComputeEnergyGrid instance;
    return instance;
  }

  std::vector<float> computeEnergyGridGPUImplementation(int3 size, double2 probeParameter,
                                                        std::vector<double3> positions, std::vector<double2> potentialParameters,
                                                        double3x3 unitCell, int3 numberOfReplicas);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
  UINT _descriptorSize = 0;
  static constexpr UINT kThreadGroupSize = 64;
  static const std::string _energyGridKernel;
};
