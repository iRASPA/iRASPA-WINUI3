/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <vector>
#include <mathkit.h>
#include <skcell.h>
#include "skspheresweep.h"

// The surface of the union of the probe-inflated atoms, measured patch by patch instead of by throwing
// points at it. The atoms may be inflated from Lennard-Jones sigma (force-field geometric surface) or
// from Bondi van der Waals radii (VDW geometric surface); the probe contribution is ½ σ_probe in both.
//
// Ported from Cocoa StructureKit SKGeometricSurface / raspa3 structurekit/diagrams/exact.

/// Minimal snapshot of framework data needed by geometric surface area helpers.
/// Matches Cocoa SimulationKitProtocols.SKFrameworkSnapshot (WINUI3 has no separate protocols file yet).
struct SKFrameworkSnapshot
{
  SKCell cell{};
  std::vector<double3> positions;
  std::vector<double2> potentialParameters;
  double2 probeParameters{};
  std::vector<double4> blockingPockets;
  double mass = 0.0;
  std::vector<int> elementIdentifiers;
};

/// Surface area in Å² plus the same volumetric / gravimetric conversions as Cocoa SKNitrogenSurfaceArea.
struct SKSurfaceAreaResult
{
  double area = 0.0;
  double gravimetric = 0.0;
  double volumetric = 0.0;

  SKSurfaceAreaResult() = default;
  SKSurfaceAreaResult(double areaValue, const SKFrameworkSnapshot &structure);
};

/// One neighbouring sphere that clips a patch, as a centre in Cartesian angstrom and an inflated radius.
struct SKGeometricSurfaceClip
{
  double3 center{};
  double radius = 0.0;

  SKGeometricSurfaceClip() = default;
  SKGeometricSurfaceClip(const double3 &centerValue, double radiusValue)
      : center(centerValue), radius(radiusValue)
  {
  }
};

/// One drawing copy of a patch: the sphere may be placed on a neighbouring lattice image so that the
/// part which stuck out of the cell re-enters through the opposite face. `cellOrigin` is the
/// Cartesian origin of the cell the copy is clipped to.
struct SKGeometricSurfacePatchCopy
{
  double3 center{};
  std::vector<SKGeometricSurfaceClip> clips;
  double3 cellOrigin{};

  SKGeometricSurfacePatchCopy() = default;
  SKGeometricSurfacePatchCopy(const double3 &centerValue, const std::vector<SKGeometricSurfaceClip> &clipsValue,
                              const double3 &cellOriginValue)
      : center(centerValue), clips(clipsValue), cellOrigin(cellOriginValue)
  {
  }
};

/// The exposed part of one probe-inflated atom: a spherical patch bounded by the caps its neighbours cut.
struct SKGeometricSurfacePatch
{
  int atomIndex = 0;
  double3 center{};
  double radius = 0.0;
  std::vector<SKGeometricSurfaceClip> clips;
  /// Exact area of this atom's exposed surface, in Å², from the latitude sweep.
  double area = 0.0;

  SKGeometricSurfacePatch() = default;
  SKGeometricSurfacePatch(int atomIndexValue, const double3 &centerValue, double radiusValue,
                          const std::vector<SKGeometricSurfaceClip> &clipsValue, double areaValue)
      : atomIndex(atomIndexValue), center(centerValue), radius(radiusValue), clips(clipsValue), area(areaValue)
  {
  }

  /// The home copy (centre already in the cell) plus a translated copy for every face, edge or
  /// corner the sphere overlaps, so that clipping each copy to the cell reconstructs the whole
  /// patch inside the box and nothing is drawn outside it.
  std::vector<SKGeometricSurfacePatchCopy> copiesInsideUnitCell(const SKCell &cell) const;

  /// How far in fractional coordinates a sphere of `radius` can reach along each cell axis. Used to
  /// decide which lattice images of a patch still overlap the unit cell.
  static double3 fractionalExtent(double radius, const double3x3 &inverseUnitCell);
};

/// The geometric accessible surface of a framework: the list of spherical patches the probe's centre
/// traces, and the exact area of their union.
class SKGeometricSurface
{
public:
  std::vector<SKGeometricSurfacePatch> patches;
  /// Total exposed area in Å². Sum of the patch areas; each point of the union belongs to one patch.
  double area = 0.0;

  SKGeometricSurface() = default;
  SKGeometricSurface(const std::vector<SKGeometricSurfacePatch> &patchesValue, double areaValue)
      : patches(patchesValue), area(areaValue)
  {
  }

  /// Inflated radius of an atom for the geometric accessible surface: half the mixed Lennard-Jones
  /// sigma, which is the contact distance of the probe's centre with that atom.
  static double inflatedRadius(double atomSigma, double probeSigma);

  /// Inflated Bondi van der Waals radius: the VDW sphere plus the probe's collision radius
  /// `½ σ_probe`, so a vanishing probe sits on the drawn VDW atoms.
  static double inflatedVanDerWaalsRadius(double atomVDW, double probeSigma);

  /// Smallest probe sigma used when building a geometric surface, in Å. Below this the sheet
  /// coincides with the Forcefield or VDW atoms and the two imposters still z-fight, even when
  /// both shade per sample. The inspector may store zero; the surface is inflated behind the
  /// scenes. Atom draw radii are not capped.
  static constexpr double minimumProbeSigma = 0.001;

  static double clampedProbeSigma(double probeSigma);

  /// Builds the patches of the union of the given spheres.
  static SKGeometricSurface build(const std::vector<double3> &fractionalPositions,
                                  const std::vector<double> &radii, const SKCell &cell,
                                  const std::vector<double4> &blockingPockets = {}, int subdivisions = 1);

  /// Force-field geometric surface: each atom is inflated by half the mixed Lennard-Jones sigma
  /// with `probeSigma`. Periodic images that can reach an atom are taken from `cell`. An atom swallowed
  /// whole by a neighbour is dropped, since it carries no exposed surface.
  ///
  /// `blockingPockets` are the applied pockets: a fractional centre and a radius in Å. Each is an extra
  /// clipping sphere, the same role they play on the energy grid, so an inaccessible cage's internal
  /// sheet is cut out rather than counted.
  static SKGeometricSurface build(const std::vector<double3> &fractionalPositions,
                                  const std::vector<double2> &potentialParameters, double probeSigma,
                                  const SKCell &cell, const std::vector<double4> &blockingPockets = {},
                                  int subdivisions = 1);

  /// Van der Waals geometric surface: Bondi radii inflated by half the probe sigma.
  static SKGeometricSurface buildVanDerWaals(const std::vector<double3> &fractionalPositions,
                                             const std::vector<int> &elementIdentifiers, double probeSigma,
                                             const SKCell &cell,
                                             const std::vector<double4> &blockingPockets = {},
                                             int subdivisions = 1);

  /// Fractional coordinates folded into the unit cube [0, 1). A patch whose centre sat outside the
  /// cell is the same patch after a lattice translation, and drawing it there puts it back inside.
  static double3 wrappedFractional(const double3 &s);

  /// Exact force-field geometric accessible surface area of each snapshot, in the same volumetric
  /// and gravimetric units as the nitrogen and well-surface areas. Blocking pockets on the snapshot
  /// are always applied.
  static std::vector<SKSurfaceAreaResult> surfaceAreas(const std::vector<SKFrameworkSnapshot> &snapshots);

  /// Exact van der Waals geometric accessible surface area of each snapshot (Bondi radii).
  static std::vector<SKSurfaceAreaResult> vanDerWaalsSurfaceAreas(const std::vector<SKFrameworkSnapshot> &snapshots);
};
