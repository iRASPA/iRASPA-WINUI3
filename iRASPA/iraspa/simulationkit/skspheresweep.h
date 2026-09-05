/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <vector>
#include <functional>
#include <optional>
#include <mathkit.h>

// Sweeping the exposed part of one sphere, latitude by latitude.
//
// Ported from raspa3 / Cocoa StructureKit SKSphereSweep. Three of the exact analyses need the part of
// a sphere that no other sphere covers. The surface area sweeps the boundary of the union of the
// inflated atoms. The region is a sphere less a set of spherical caps. Cut it into circles of
// latitude in a frame chosen so that no cap sits on the pole, and on each circle the covered part
// is a union of arcs, one per cap that reaches that latitude. What is left between them is exposed,
// in closed form, and the integral over latitude is smooth between the latitudes at which the arcs
// appear, vanish, or run into one another.

/// How many nodes the latitude rule uses on each half of each smooth piece. Ten integrate a
/// polynomial of degree nineteen exactly.
constexpr int SKExactQuadratureOrder = 10;

/// Gaps in a circle of latitude shorter than this are dropped. They are the seams where two caps
/// meet almost tangentially.
constexpr double SKSweepGapTolerance = 1.0e-12;

/// Covered has to mean covered with room to spare. A framework is symmetric, so three spheres
/// meeting in one point is the ordinary case rather than a coincidence.
constexpr double SKCapCoverTolerance = 1.0e-9;

/// How long a panel of the latitude rule may be where it sits next to a pole, as a multiple of the
/// room between the piece it belongs to and that pole.
constexpr double SKPoleClearance = 4.0;

/// Orthonormal frame `{first, second, polar}` used by the latitude sweep.
struct SKSweepFrameAxes
{
  double3 first{};
  double3 second{};
  double3 polar{};
};

/// A unit vector perpendicular to `axis`, chosen so that the cross product behind it is well conditioned.
double3 SKPerpendicularTo(const double3 &axis);

/// A polar angle folded back into [0, pi], which is where the extreme latitudes of a cap live.
double SKFoldedPolarAngle(double angle);

/// The frame a sphere is swept in, given the axes of the caps covering it. Latitude slicing
/// degenerates for a cap whose axis sits on the polar axis, so the polar axis is chosen to be as
/// far as possible from every one of them.
SKSweepFrameAxes SKSweepFrame(const std::vector<double3> &axes);

/// Whether the disc of the inner cap lies within the disc of the outer, so that the inner one
/// bounds nothing of its own and covers nothing the outer does not.
bool SKDiscWithinDisc(double cosineBetweenAxes, double cosineInner, double sineInner,
                      double cosineOuter, double sineOuter);

/// A point where two of the caps cross that no third one covers, with the two caps it belongs to.
struct SKCapCrossing
{
  int firstCircle = 0;
  int secondCircle = 0;
  double3 direction{};
};

/// One cap of the sphere being swept, placed in the frame the sweep is done in.
struct SKSweepCircle
{
  double3 axis{};
  double cosineHalfAngle = 0.0;
  double sineHalfAngle = 0.0;
  double halfAngle = 0.0;

  double polarAngle = 0.0;
  double cosinePolar = 0.0;
  double sinePolar = 0.0;
  double azimuth = 0.0;
  double cosineAzimuth = 1.0;
  double sineAzimuth = 0.0;

  double lowestLatitude = 0.0;
  double highestLatitude = 0.0;
  bool reachesOverPole = false;
  bool reachesOverAntipole = false;
};

/// A cap from an axis and the cosine of its half angle, or nullopt where the cap covers none of the sphere.
std::optional<SKSweepCircle> SKMakeSweepCircle(const double3 &axis, double cosineHalfAngle);

/// Drops the caps whose discs lie inside another's.
void SKPruneContainedDiscs(std::vector<SKSweepCircle> &circles);

/// Every crossing of two of the caps that no third one covers.
std::vector<SKCapCrossing> SKUncoveredCrossings(const std::vector<SKSweepCircle> &circles);

/// One exposed stretch of one circle of latitude, as the sweep hands it to whatever is being measured.
struct SKLatitudeGap
{
  double sineLatitude = 0.0;
  double cosineLatitude = 0.0;
  double weight = 0.0;

  double begin = 0.0;
  double end = 0.0;
  double span = 0.0;

  double cosineBegin = 1.0;
  double sineBegin = 0.0;
  double cosineEnd = 1.0;
  double sineEnd = 0.0;

  double3 at(const SKSweepFrameAxes &frame, double cosineAzimuth, double sineAzimuth) const;
  double3 atBegin(const SKSweepFrameAxes &frame) const;
  double3 atEnd(const SKSweepFrameAxes &frame) const;
};

/// One arc of a circle of latitude that a cap covers.
struct SKCoveredArc
{
  double begin = 0.0;
  double end = 0.0;
  double cosineBegin = 1.0;
  double sineBegin = 0.0;
  double cosineEnd = 1.0;
  double sineEnd = 0.0;
};

/// Scratch the sweep needs, kept by the caller so that a structure's worth of spheres costs one
/// allocation rather than one per sphere.
struct SKSweepWorkspace
{
  std::vector<double3> axes;
  std::vector<SKCapCrossing> crossings;
  std::vector<double> breakpoints;
  std::vector<int> cutting;
  std::vector<double> panels;
  std::vector<SKCoveredArc> covered;
};

/// Gauss-Legendre nodes and weights on the unit interval, found once by Newton's method on the
/// Legendre polynomial with the usual Chebyshev starting guess.
struct SKGaussRule
{
  std::vector<double> nodes;
  std::vector<double> weights;
};

const SKGaussRule &SKUnitIntervalGaussRule();

/// Places every cap in the frame, puts them in order of their own azimuth, and collects the
/// latitudes at which the exposed length of a circle of latitude stops being analytic.
/// When `knownCrossings` is non-null, those directions are used for breakpoints; otherwise
/// uncovered crossings are computed into `work.crossings`.
void SKPrepareSweep(std::vector<SKSweepCircle> &circles,
                    const SKSweepFrameAxes &frame,
                    const std::vector<double3> *knownCrossings,
                    SKSweepWorkspace &work);

/// The latitudes one smooth piece is cut at.
void SKPanelBoundaries(double begin, double end, int subdivisions, bool cut, std::vector<double> &panels);

/// Walks the exposed part of the sphere and calls `measure` for every exposed stretch of every
/// circle of latitude the quadrature visits. The whole of the geometry is here and none of what is
/// being measured: the area of a gap is `radius * radius * gap.sineLatitude * gap.span * gap.weight`.
void SKSweepExposedLatitudes(std::vector<SKSweepCircle> &circles,
                             const SKSweepFrameAxes &frame,
                             const std::vector<double3> *knownCrossings,
                             int subdivisions,
                             SKSweepWorkspace &work,
                             const std::function<void(const SKLatitudeGap &)> &measure);
