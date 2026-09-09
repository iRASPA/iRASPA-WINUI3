/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <stdexcept>
#include <vector>
#include <mathkit.h>

// Metric-aware Voronoi construction with a native fractional-coordinate interface.
//
// Ported from Cocoa StructureKit SKVoronoi / raspa3 symmetrykit/skvoronoi. The Voronoi tessellation
// of a periodic system is not invariant under the anisotropic scaling that maps a unit cell onto the
// fractional unit cube, so fractional positions cannot simply be tessellated in the unit cube. This
// type takes positions in fractional coordinates together with the unit-cell matrix h (lattice vectors
// as columns), bins and searches neighbors in fractional space, and accounts for the cell geometry
// exactly: candidate separations are mapped through h, so every bisector plane is the true Cartesian
// one.
//
// When per-site radii are given, the radical (power) diagram is built instead: the plane between
// sites i and j is offset towards the smaller atom by ½(rᵢ² − rⱼ²)/|Δ|, which is the tessellation
// used by zeo++/voro++ for radii-dependent pore analysis. The Apollonius diagram uses this as a
// cheap adjacency heuristic for candidate site quadruples.

enum class SKVoronoiError
{
  SingularUnitCell,
  NoPositions,
  RadiiSizeMismatch,
  CoincidingSites,
  ConstructionFailed
};

inline const char *SKVoronoiErrorMessage(SKVoronoiError error)
{
  switch (error)
  {
  case SKVoronoiError::SingularUnitCell:
    return "SKVoronoi: singular unit cell";
  case SKVoronoiError::NoPositions:
    return "SKVoronoi: no positions given";
  case SKVoronoiError::RadiiSizeMismatch:
    return "SKVoronoi: radii size does not match positions size";
  case SKVoronoiError::CoincidingSites:
    return "SKVoronoi: coinciding sites";
  case SKVoronoiError::ConstructionFailed:
    return "SKVoronoi: cell construction failed (initial bounding cube face survived)";
  }
  return "SKVoronoi: unknown error";
}

[[noreturn]] inline void throwSKVoronoiError(SKVoronoiError error)
{
  throw std::runtime_error(SKVoronoiErrorMessage(error));
}

/// A single face of a Voronoi cell; the face is the metric perpendicular bisector between
/// the cell's site and the periodic image `neighborImage` of site `neighborIndex`.
struct SKVoronoiFace
{
  int neighborIndex = 0;
  int3 neighborImage{};
  double neighborDistance = 0.0;
  std::vector<int> vertexIndices;
  double area = 0.0;
  double3 normalCartesian{};
};

/// A Voronoi cell; vertex positions are stored relative to the site position, both in
/// fractional coordinates and mapped to Cartesian coordinates.
struct SKVoronoiCell
{
  int siteIndex = 0;
  double3 sitePositionFractional{};
  std::vector<double3> verticesFractional;
  std::vector<double3> verticesCartesian;
  std::vector<SKVoronoiFace> faces;
  double volume = 0.0;
  double3 centroidCartesian{};
};

class SKVoronoi
{
public:
  /// When `radii` is empty the construction is the ordinary (unweighted) Voronoi diagram.
  /// When per-site radii are given, the radical (power) diagram is built instead.
  SKVoronoi(const double3x3 &unitCell, const std::vector<double3> &fractionalPositions,
            const std::vector<double> &radii = {});

  SKVoronoiCell computeCell(int siteIndex) const;
  std::vector<SKVoronoiCell> computeAllCells() const;

private:
  double3x3 _unitCell{};
  double3x3 _inverseUnitCell{};
  double _cellVolume = 0.0;
  double3 _perpendicularWidths{};
  std::vector<double3> _positions;
  std::vector<double3> _cartesianPositions;
  std::vector<double> _radiiSquared;
  double _maximumRadiusSquared = 0.0;
  int3 _gridSize{};
  std::vector<std::vector<int>> _bins;
  double _minimumBinWidth = 0.0;
  double _initialHalfWidth = 0.0;
};
