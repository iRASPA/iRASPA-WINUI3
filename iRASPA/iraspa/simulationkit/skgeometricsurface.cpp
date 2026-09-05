/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skgeometricsurface.h"

#include <algorithm>
#include <cmath>
#include <constants.h>
#include <skelement.h>

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

SKSurfaceAreaResult::SKSurfaceAreaResult(double areaValue, const SKFrameworkSnapshot &structure)
    : area(areaValue)
{
  gravimetric =
      structure.mass > 0.0 ? area * Constants::AvogadroConstantPerAngstromSquared / structure.mass : 0.0;
  const double volume = structure.cell.volume();
  volumetric = volume > 0.0 ? area * 1.0e4 / volume : 0.0;
}

double SKGeometricSurface::inflatedRadius(double atomSigma, double probeSigma)
{
  return 0.5 * (atomSigma + probeSigma);
}

double SKGeometricSurface::inflatedVanDerWaalsRadius(double atomVDW, double probeSigma)
{
  return atomVDW + 0.5 * probeSigma;
}

double SKGeometricSurface::clampedProbeSigma(double probeSigma)
{
  return std::max(probeSigma, minimumProbeSigma);
}

double3 SKGeometricSurface::wrappedFractional(const double3 &s)
{
  return double3(s.x - std::floor(s.x), s.y - std::floor(s.y), s.z - std::floor(s.z));
}

SKGeometricSurface SKGeometricSurface::build(const std::vector<double3> &fractionalPositions,
                                             const std::vector<double> &radii, const SKCell &cell,
                                             const std::vector<double4> &blockingPockets, int subdivisions)
{
  const size_t count = std::min(fractionalPositions.size(), radii.size());
  if (count == 0)
    return SKGeometricSurface({}, 0.0);

  const double3x3 unitCell = cell.unitCell();
  std::vector<double3> centres;
  centres.reserve(count);
  std::vector<double> usedRadii;
  usedRadii.reserve(count);
  for (size_t i = 0; i < count; ++i)
  {
    const double3 wrapped = wrappedFractional(fractionalPositions[i]);
    centres.push_back(unitCell * wrapped);
    usedRadii.push_back(radii[i]);
  }

  std::vector<double3> clipperCentres = centres;
  std::vector<double> clipperRadii = usedRadii;
  clipperCentres.reserve(count + blockingPockets.size());
  clipperRadii.reserve(count + blockingPockets.size());
  for (const double4 &pocket : blockingPockets)
  {
    if (pocket.w <= 0.0)
      continue;
    const double3 wrapped = wrappedFractional(double3(pocket.x, pocket.y, pocket.z));
    clipperCentres.push_back(unitCell * wrapped);
    clipperRadii.push_back(pocket.w);
  }

  const double maxRadius = usedRadii.empty() ? 0.0 : *std::max_element(usedRadii.begin(), usedRadii.end());
  const double maxClipperRadius =
      clipperRadii.empty() ? maxRadius : *std::max_element(clipperRadii.begin(), clipperRadii.end());
  const int3 replicas = cell.numberOfReplicas(std::max(maxRadius + maxClipperRadius, 1.0));

  std::vector<SKGeometricSurfacePatch> patches;
  patches.reserve(count);
  double totalArea = 0.0;
  SKSweepWorkspace work;

  const int nx = replicas.x;
  const int ny = replicas.y;
  const int nz = replicas.z;

  for (size_t atomIndex = 0; atomIndex < count; ++atomIndex)
  {
    const double radius = usedRadii[atomIndex];
    if (radius <= 0.0)
      continue;

    std::vector<SKSweepCircle> circles;
    std::vector<SKGeometricSurfaceClip> clips;
    bool buried = false;

    for (size_t j = 0; j < clipperCentres.size() && !buried; ++j)
    {
      const double neighbourRadius = clipperRadii[j];
      if (neighbourRadius <= 0.0)
        continue;
      for (int kx = -nx; kx <= nx && !buried; ++kx)
      {
        for (int ky = -ny; ky <= ny && !buried; ++ky)
        {
          for (int kz = -nz; kz <= nz; ++kz)
          {
            if (j == atomIndex && kx == 0 && ky == 0 && kz == 0)
              continue;
            const double3 image(static_cast<double>(kx), static_cast<double>(ky), static_cast<double>(kz));
            const double3 neighbourCentre = clipperCentres[j] + unitCell * image;
            const double3 delta = neighbourCentre - centres[atomIndex];
            const double distance = delta.length();
            if (distance < 1.0e-12)
            {
              if (neighbourRadius > radius)
              {
                buried = true;
                break;
              }
              continue;
            }

            const double cosineHalfAngle =
                (radius * radius + distance * distance - neighbourRadius * neighbourRadius) /
                (2.0 * radius * distance);
            if (cosineHalfAngle >= 1.0)
              continue;
            if (cosineHalfAngle <= -1.0)
            {
              buried = true;
              break;
            }

            const double3 axis = delta / distance;
            if (std::optional<SKSweepCircle> circle = SKMakeSweepCircle(axis, cosineHalfAngle))
            {
              circles.push_back(*circle);
              clips.emplace_back(neighbourCentre, neighbourRadius);
            }
          }
        }
      }
    }

    if (buried)
      continue;

    // Contained discs add latitudes at which nothing happens to the area sweep; they do not change
    // the clip test, a point inside an inner sphere already being inside the outer one.
    SKPruneContainedDiscs(circles);

    double patchArea = 0.0;
    if (circles.empty())
    {
      patchArea = 4.0 * kPi * radius * radius;
    }
    else
    {
      work.axes.clear();
      work.axes.reserve(circles.size());
      for (const SKSweepCircle &circle : circles)
        work.axes.push_back(circle.axis);
      const SKSweepFrameAxes frame = SKSweepFrame(work.axes);
      SKSweepExposedLatitudes(circles, frame, nullptr, subdivisions, work,
                              [&](const SKLatitudeGap &gap) {
                                patchArea += radius * radius * gap.sineLatitude * gap.span * gap.weight;
                              });
    }

    if (patchArea <= 0.0)
      continue;

    patches.emplace_back(static_cast<int>(atomIndex), centres[atomIndex], radius, clips, patchArea);
    totalArea += patchArea;
  }

  return SKGeometricSurface(patches, totalArea);
}

SKGeometricSurface SKGeometricSurface::build(const std::vector<double3> &fractionalPositions,
                                             const std::vector<double2> &potentialParameters,
                                             double probeSigma, const SKCell &cell,
                                             const std::vector<double4> &blockingPockets, int subdivisions)
{
  const size_t count = std::min(fractionalPositions.size(), potentialParameters.size());
  const double clamped = clampedProbeSigma(probeSigma);
  std::vector<double> radii;
  radii.reserve(count);
  for (size_t i = 0; i < count; ++i)
    radii.push_back(inflatedRadius(potentialParameters[i].y, clamped));

  std::vector<double3> positions(fractionalPositions.begin(),
                                 fractionalPositions.begin() + static_cast<std::ptrdiff_t>(count));
  return build(positions, radii, cell, blockingPockets, subdivisions);
}

SKGeometricSurface SKGeometricSurface::buildVanDerWaals(const std::vector<double3> &fractionalPositions,
                                                        const std::vector<int> &elementIdentifiers,
                                                        double probeSigma, const SKCell &cell,
                                                        const std::vector<double4> &blockingPockets,
                                                        int subdivisions)
{
  const size_t count = std::min(fractionalPositions.size(), elementIdentifiers.size());
  const double clamped = clampedProbeSigma(probeSigma);
  const std::vector<SKElement> &elements = PredefinedElements::predefinedElements;
  std::vector<double> radii;
  radii.reserve(count);
  for (size_t i = 0; i < count; ++i)
  {
    const int elementId = elementIdentifiers[i];
    const double vdw =
        (elementId >= 0 && static_cast<size_t>(elementId) < elements.size()) ? elements[static_cast<size_t>(elementId)]._VDWRadius
                                                                              : 0.0;
    radii.push_back(inflatedVanDerWaalsRadius(vdw, clamped));
  }

  std::vector<double3> positions(fractionalPositions.begin(),
                                 fractionalPositions.begin() + static_cast<std::ptrdiff_t>(count));
  return build(positions, radii, cell, blockingPockets, subdivisions);
}

std::vector<SKSurfaceAreaResult> SKGeometricSurface::surfaceAreas(const std::vector<SKFrameworkSnapshot> &snapshots)
{
  std::vector<SKSurfaceAreaResult> results;
  results.reserve(snapshots.size());
  for (const SKFrameworkSnapshot &snapshot : snapshots)
  {
    const SKGeometricSurface surface =
        build(snapshot.positions, snapshot.potentialParameters, snapshot.probeParameters.y, snapshot.cell,
              snapshot.blockingPockets);
    results.emplace_back(surface.area, snapshot);
  }
  return results;
}

std::vector<SKSurfaceAreaResult>
SKGeometricSurface::vanDerWaalsSurfaceAreas(const std::vector<SKFrameworkSnapshot> &snapshots)
{
  std::vector<SKSurfaceAreaResult> results;
  results.reserve(snapshots.size());
  for (const SKFrameworkSnapshot &snapshot : snapshots)
  {
    const SKGeometricSurface surface =
        buildVanDerWaals(snapshot.positions, snapshot.elementIdentifiers, snapshot.probeParameters.y,
                         snapshot.cell, snapshot.blockingPockets);
    results.emplace_back(surface.area, snapshot);
  }
  return results;
}

double3 SKGeometricSurfacePatch::fractionalExtent(double radius, const double3x3 &inverseUnitCell)
{
  // Cocoa: row lengths of the inverse unit cell (inv[col][row] → row vectors).
  const double3 row0(inverseUnitCell[0].x, inverseUnitCell[1].x, inverseUnitCell[2].x);
  const double3 row1(inverseUnitCell[0].y, inverseUnitCell[1].y, inverseUnitCell[2].y);
  const double3 row2(inverseUnitCell[0].z, inverseUnitCell[1].z, inverseUnitCell[2].z);
  return double3(radius * row0.length(), radius * row1.length(), radius * row2.length());
}

std::vector<SKGeometricSurfacePatchCopy> SKGeometricSurfacePatch::copiesInsideUnitCell(const SKCell &cell) const
{
  const double3x3 inverse = cell.inverseUnitCell();
  const double3x3 unitCell = cell.unitCell();
  double3 fractional = inverse * center;
  fractional = SKGeometricSurface::wrappedFractional(fractional);
  const double3 wrappedCenter = unitCell * fractional;
  const double3 wrapShift = wrappedCenter - center;
  std::vector<SKGeometricSurfaceClip> wrappedClips;
  wrappedClips.reserve(clips.size());
  for (const SKGeometricSurfaceClip &clip : clips)
    wrappedClips.emplace_back(clip.center + wrapShift, clip.radius);

  const double3 extent = fractionalExtent(radius, inverse);
  const int nx = std::max(1, static_cast<int>(std::ceil(extent.x)));
  const int ny = std::max(1, static_cast<int>(std::ceil(extent.y)));
  const int nz = std::max(1, static_cast<int>(std::ceil(extent.z)));

  std::vector<SKGeometricSurfacePatchCopy> copies;
  for (int wx = -nx; wx <= nx; ++wx)
  {
    for (int wy = -ny; wy <= ny; ++wy)
    {
      for (int wz = -nz; wz <= nz; ++wz)
      {
        const double3 offset(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz));
        const double3 image = fractional + offset;
        if (image.x + extent.x > 0.0 && image.x - extent.x < 1.0 && image.y + extent.y > 0.0 &&
            image.y - extent.y < 1.0 && image.z + extent.z > 0.0 && image.z - extent.z < 1.0)
        {
          const double3 shift = unitCell * offset;
          std::vector<SKGeometricSurfaceClip> shiftedClips;
          shiftedClips.reserve(wrappedClips.size());
          for (const SKGeometricSurfaceClip &clip : wrappedClips)
            shiftedClips.emplace_back(clip.center + shift, clip.radius);
          copies.emplace_back(wrappedCenter + shift, shiftedClips, double3());
        }
      }
    }
  }
  return copies;
}
