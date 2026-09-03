/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <vector>
#include <mathkit.h>

/// The well surface: where an adsorbed molecule sits, as opposed to the iso-surface, which is where it turns
/// back. Its topology comes from a distance field and its geometry from the energy (see SKComputeWellField
/// for the full picture):
///
///   - SKComputeWellField::computeWellFieldGrid supplies three floats per grid point: the energy U, the
///     additively weighted (Apollonius) distance d = min over atoms of (|x - a| - 2^(1/6) sigma), whose zero
///     level set is the probe-contact offset surface of the framework, and the medial reliability rel
///     (1 against one wall, 0 on the medial axis of a channel where opposing walls cancel).
///   - The field handed to the ordinary marching cubes is max(-d, s (U - iso)): its zero set bounds the
///     region { d > 0 and U < iso }, the pore trimmed to wells at least iso deep, with smooth isosurface
///     caps at the trim. Being the boundary of a region, it is watertight and single-sheeted --- no interior
///     membranes, domes, or flaps, which were the failure modes of the crease-set extraction this replaces.
///   - SKComputeWellField::refineWellSurfaceVertices then slides each vertex along the ray to its nearest
///     atom onto the exact 1D minimum of the analytic energy: the true multi-atom well floor.
///   - Where the channel is narrower than the probe's contact diameter the sheet cannot exist (d < 0 across
///     the whole cross-section), yet those are the deepest wells of all: the transverse minima have merged
///     onto the channel axis, a 1D filament. constructWellFilament draws it as a thin tube, the zero set of
///     max(rel - r0, d, s (U - iso)): enclosed by opposing walls, inside the contact region, and at least
///     iso deep. That is where an enclosed adsorbate sits --- there is no wall sheet left. Where the channel
///     widens, d crosses zero on the axis and the tube caps at the tip of the closing sheet; those gaps are
///     physical, not holes. Reliability is not a distance field, so thresholding it still punches one-voxel
///     gaps inside a pinch: morphological closing, clipped to { d < 0, U < iso }, fills those without
///     growing into the contact sheet. It is its own rendering method (the well-surface overlay), so a copy
///     of the structure can superimpose it on the well surface with a distinct material. The sheet
///     constructor returns nothing rather than substituting a high marching-cubes isovalue when that region
///     is empty, which would draw a surface inside the atoms. Well-surface mode then draws nothing; the
///     overlay is the only way to see the filament.
class SKWellSurface
{
public:
  /// One angstrom of distance field equals this many kelvin of energy in the combined field; it only shapes
  /// interpolation and normals near the trim seam, the zero set itself is independent of it.
  static constexpr float energyScale = 0.001f;

  /// Where opposing walls cancel (rel below this) the point is close enough to the channel medial axis to
  /// belong to the merged-well filament. Between fully surrounding walls rel falls to 0; against one wall,
  /// even in the creases between its atoms, the directions cannot cancel and rel stays above ~0.46
  /// (measured in MFI). The 0.45 threshold sits just under that floor, giving the tube its full transverse
  /// extent (~0.4 A in a deep pinch, about the thermal amplitude of a trapped molecule at room temperature)
  /// while the crease set stays out; whatever grazes through is specks, removed by the area filter.
  static constexpr float filamentReliabilityThreshold = 0.45f;

  /// Isolated filament specks smaller than this, in square angstrom, are crease leakage rather than channel
  /// filaments: a real merged-well tube runs the length of a channel segment and measures several.
  static constexpr double filamentMinimumArea = 2.0;

  /// Closing radius, in voxels, for reliability holes inside a pinch. Clipped to { d < 0, U < iso }, so it
  /// cannot grow into the contact sheet or join separate pinches through a wide pore.
  static constexpr int filamentClosingRadius = 2;

  /// The trim level actually used: the isovalue, unless that is below the deepest well on the grid (nothing
  /// would remain), in which case a quarter of the deepest well, like the marching-cubes fallback.
  static float effectiveTrimIsovalue(const std::vector<float> &field, double isovalue, int3 dimensions);

  /// The contact sheet, as the marching-cubes triangle buffer the isosurface pipeline draws: three float4
  /// per vertex (position in unit-cell fractional coordinates, normal, pad). Empty when the probe does not
  /// fit as a sheet anywhere in the structure.
  static std::vector<float4> constructWellSurface(const std::vector<float> &field, double isovalue,
                                                  int3 dimensions);

  /// The merged-well filament: the boundary of { rel < r0, d < 0, U < iso } --- enclosed by opposing walls,
  /// in a channel too narrow for the contact sheet, and at least iso deep. Empty when no wells have merged.
  static std::vector<float4> constructWellFilament(const std::vector<float> &field, double isovalue,
                                                   int3 dimensions, double3x3 unitCell);

private:
  static std::vector<bool> dilateBinary(const std::vector<bool> &mask, int nx, int ny, int nz);
  static std::vector<bool> erodeBinary(const std::vector<bool> &mask, int nx, int ny, int nz);
  /// Bubbles inside the tube that do not reach { d > 0 or U > iso }. Flood from the true exterior through
  /// the open voxels and mark whatever remains as interior.
  static void fillEnclosedCavities(std::vector<bool> &closed, const std::vector<bool> &candidate,
                                   int nx, int ny, int nz);
  /// Connected components of the filament mesh by periodically welded vertices; components below
  /// filamentMinimumArea are dropped.
  static std::vector<float4> removeFilamentSpecks(const std::vector<float4> &triangleData,
                                                  double3x3 unitCell);
};
