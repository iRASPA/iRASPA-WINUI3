/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mathkit.h>
#include "skdx12.h"

/// The analytic field the well surface is extracted from, and the refinement that puts its vertices on the
/// well floor.
///
/// The well surface is where an adsorbed molecule sits, as opposed to the iso-surface, which is where it
/// turns back. It is extracted in two steps that separate topology from geometry:
///
///   1. Topology from a distance field. d(x) = min over atoms of (|x - a| - rmin), the additively weighted
///      (Apollonius) distance, with rmin = 2^(1/6) sigma the probe-atom contact optimum. Its zero level set
///      is a smooth offset surface wrapping every wall: it cannot produce interior membranes, domes across
///      intersections, or flaps at sheet junctions, because d has exactly one zero crossing along any ray
///      into a wall. Necks narrower than the probe pinch closed of their own accord. The depth trim is a CSG
///      intersection with the energy: the field handed to marching cubes is max(-d, s (U - iso)), whose zero
///      set bounds the region { d > 0 and U < iso } --- the pore, trimmed to wells at least iso deep, with
///      smooth isosurface caps where the trim cuts. See SKWellSurface.
///
///   2. Geometry from the energy. Each marching-cubes vertex is slid along the ray toward its nearest atom
///      (the direction "orthogonal into the wall") to the 1D minimum of the exact analytic U, a bracketed
///      golden-section search that cannot wander. That lands the surface on the true multi-atom well floor,
///      which sits slightly off |x - a| = rmin wherever more than one atom contributes.
///
/// This replaces an earlier extraction of the crease set of the Hessian eigenframe, which was mathematically
/// correct but visually unusable: the crease set genuinely contains one-sided sheets --- window membranes,
/// intersection domes, flaps over wall bumps --- and no local quantity separates them from the wall sheet.
class SKComputeWellField : public SKDx12
{
public:
  SKComputeWellField(SKDx12 const &) = delete;
  void operator=(SKComputeWellField const &) = delete;

  /// Three floats per grid point: the energy U, the additively weighted (Apollonius) distance d, and the
  /// medial reliability rel --- the length of the softmin-weighted average of the unit vectors toward the
  /// atoms (1 against one wall, 0 on the medial axis of a channel where opposing walls cancel).
  ///
  /// Sampled at index/size, the same periodic convention marching cubes reads the grid back with.
  static std::vector<float> computeWellFieldGrid(int3 size, double2 probeParameter,
                                                 std::vector<double3> positions,
                                                 std::vector<double2> potentialParameters,
                                                 double3x3 unitCell, int3 numberOfReplicas,
                                                 std::vector<double4> blockingPockets = {});

  /// Slides every vertex of a marching-cubes mesh along the ray toward its nearest atom onto the 1D minimum
  /// of the exact analytic energy --- the true multi-atom well floor. \a triangleData is the buffer
  /// SKComputeIsosurface returns, three float4 per vertex (position in unit-cell fractional coordinates,
  /// normal, pad), and is refined in place. Vertices on the trim caps (energies at the iso level) belong to
  /// the isosurface and are left alone.
  static void refineWellSurfaceVertices(std::vector<float4> &triangleData, double2 probeParameter,
                                        std::vector<double3> positions,
                                        std::vector<double2> potentialParameters,
                                        double3x3 unitCell, int3 numberOfReplicas,
                                        std::vector<double4> blockingPockets, float isovalue);

private:
  SKComputeWellField();
  ~SKComputeWellField() override = default;

  static SKComputeWellField &getInstance()
  {
    static SKComputeWellField instance;
    return instance;
  }

  // What the two kernels are handed once the caller's doubles have been reduced to what the GPU reads.
  struct FieldInputs
  {
    std::vector<float> atomPositions;       // 4 floats per atom, replica-cell fractional
    std::vector<float> potentialParameters; // 2 floats per atom: 4 epsilon mixed with the probe, sigma
    std::vector<float> replicas;            // 4 floats per replica
    std::vector<float> blockingPockets;     // 4 floats per pocket, at least one element
    size_t numberOfAtoms = 0;
    size_t numberOfReplicas = 0;
    size_t numberOfBlockingPockets = 0;
    double3 correction{1.0, 1.0, 1.0};
    double3x3 replicaCell;
  };

  static FieldInputs prepareInputs(double2 probeParameter, const std::vector<double3> &positions,
                                   const std::vector<double2> &potentialParameters, double3x3 unitCell,
                                   int3 numberOfReplicas, const std::vector<double4> &blockingPockets);

  std::vector<float> computeWellFieldGridGPUImplementation(int3 size, const FieldInputs &inputs);
  static std::vector<float> computeWellFieldGridCPUImplementation(int3 size, const FieldInputs &inputs) noexcept;

  void refineWellSurfaceVerticesGPUImplementation(std::vector<float4> &triangleData, const FieldInputs &inputs,
                                                  float isovalue);
  static void refineWellSurfaceVerticesCPUImplementation(std::vector<float4> &triangleData,
                                                         const FieldInputs &inputs, float isovalue) noexcept;

  bool createPipeline(const std::string &source, const char *entryPoint, UINT numberOfSrvs, UINT numberOfUavs,
                      UINT numberOfConstants, ComPtr<ID3D12RootSignature> &rootSignature,
                      ComPtr<ID3D12PipelineState> &pso, ComPtr<ID3D12DescriptorHeap> &heap);

  ComPtr<ID3D12RootSignature> _fieldRootSignature;
  ComPtr<ID3D12PipelineState> _fieldPso;
  ComPtr<ID3D12DescriptorHeap> _fieldHeap;

  ComPtr<ID3D12RootSignature> _refineRootSignature;
  ComPtr<ID3D12PipelineState> _refinePso;
  ComPtr<ID3D12DescriptorHeap> _refineHeap;

  UINT _descriptorSize = 0;
  static constexpr UINT kThreadGroupSize = 64;

  static const std::string _wellFieldKernel;
  static const std::string _refineKernel;
};
