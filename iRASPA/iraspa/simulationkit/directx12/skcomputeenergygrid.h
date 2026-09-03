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

  /// The energy of a point the probe cannot occupy, and the ceiling on the accumulated energy.
  static constexpr float overlapEnergy = 10000000.0f;

  /// What a point inside a blocking pocket is worth per angstrom of the depth it lies at, rather than one
  /// flat overlap energy for the whole sphere.
  ///
  /// A flat fill leaves marching cubes nothing to interpolate between two points inside a pocket: the
  /// crossing on an edge into one is then fixed by the outside end alone and cannot track the sphere, so the
  /// rim comes out as a staircase on the grid planes. A ramp gives the seam a gradient of the same order as
  /// the framework's own walls, which is what makes it follow the sphere instead. What it costs is that the
  /// excluded region is the part of the sphere deeper than isovalue/rate rather than all of it --- nothing
  /// for the isovalues the inspector offers, which run from the deepest well up to zero.
  ///
  /// The rate is the reciprocal of SKWellSurface::energyScale, which is what an angstrom of the distance
  /// field is worth in kelvin where the well surface trims one against the other. Sharing it makes the two
  /// halves of a pocket's contribution to the well field --- its energy and its distance --- the same
  /// function of depth.
  static constexpr float blockedEnergyPerAngstrom = 1000.0f;

  /// The spheres the probe may not enter, as a fractional position of the unit cell (xyz) with a radius in
  /// angstrom (w). Empty leaves the grid exactly as it was.
  static std::vector<float> ComputeEnergyGrid(int3 size, double2 probeParameter,
                                              std::vector<double3> positions, std::vector<double2> potentialParameters,
                                              double3x3 unitCell, int3 numberOfReplicas,
                                              std::vector<double4> blockingPockets = {});
  static std::vector<float> computeEnergyGrid(int3 size, double2 probeParameter,
                                              std::vector<double3> positions, std::vector<double2> potentialParameters,
                                              double3x3 unitCell, int3 numberOfReplicas,
                                              std::vector<double4> blockingPockets = {})
  {
    return ComputeEnergyGrid(size, probeParameter, positions, potentialParameters, unitCell, numberOfReplicas,
                             blockingPockets);
  }
  static std::vector<float> computeEnergyGridCPUImplementation(int3 size, double2 probeParameter,
                                                               std::vector<double3> positions, std::vector<double2> potentialParameters,
                                                               double3x3 unitCell, int3 numberOfReplicas,
                                                               std::vector<double4> blockingPockets = {}) noexcept;

  /// Which points of a grid of this size lie inside a blocking pocket, in the order the energies come back
  /// in.
  ///
  /// A blocked point is given an energy that rises with the depth it lies at rather than one flat overlap
  /// value, because that is what lets a level set follow the sphere. An average over the grid cannot read
  /// exclusion out of such a value --- a point just inside the rim is worth almost as much as open pore ---
  /// so a caller averaging over the grid asks here which points to leave out instead.
  static std::vector<bool> blockedGridPoints(int3 size, double3x3 unitCell,
                                             const std::vector<double4> &blockingPockets) noexcept;

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
                                                        double3x3 unitCell, int3 numberOfReplicas,
                                                        const std::vector<double4> &blockingPockets);

  ComPtr<ID3D12RootSignature> _rootSignature;
  ComPtr<ID3D12PipelineState> _pso;
  ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
  UINT _descriptorSize = 0;
  static constexpr UINT kThreadGroupSize = 64;
  static const std::string _energyGridKernel;
};
