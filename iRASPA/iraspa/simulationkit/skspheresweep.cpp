/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skspheresweep.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

void sortByBeginning(std::vector<SKCoveredArc> &arcs)
{
  if (arcs.size() < 2)
    return;
  for (size_t i = 1; i < arcs.size(); ++i)
  {
    const SKCoveredArc held = arcs[i];
    size_t j = i;
    while (j > 0 && arcs[j - 1].begin > held.begin)
    {
      arcs[j] = arcs[j - 1];
      --j;
    }
    arcs[j] = held;
  }
}

SKGaussRule buildUnitIntervalGaussRule()
{
  SKGaussRule constructed;
  constructed.nodes.assign(static_cast<size_t>(SKExactQuadratureOrder), 0.0);
  constructed.weights.assign(static_cast<size_t>(SKExactQuadratureOrder), 0.0);
  const double order = static_cast<double>(SKExactQuadratureOrder);
  for (int i = 0; i < SKExactQuadratureOrder; ++i)
  {
    double abscissa = std::cos(kPi * (static_cast<double>(i) + 0.75) / (order + 0.5));
    double derivative = 0.0;
    for (int iter = 0; iter < 100; ++iter)
    {
      double previous = 1.0;
      double current = abscissa;
      if (SKExactQuadratureOrder >= 2)
      {
        for (int k = 2; k <= SKExactQuadratureOrder; ++k)
        {
          const double next =
              ((2.0 * static_cast<double>(k) - 1.0) * abscissa * current - (static_cast<double>(k) - 1.0) * previous) /
              static_cast<double>(k);
          previous = current;
          current = next;
        }
      }
      derivative = order * (abscissa * current - previous) / (abscissa * abscissa - 1.0);
      const double step = current / derivative;
      abscissa -= step;
      if (std::abs(step) < 1.0e-15)
        break;
    }
    constructed.nodes[static_cast<size_t>(i)] = 0.5 * (1.0 - abscissa);
    constructed.weights[static_cast<size_t>(i)] = 1.0 / ((1.0 - abscissa * abscissa) * derivative * derivative);
  }
  return constructed;
}
} // namespace

double3 SKPerpendicularTo(const double3 &axis)
{
  double3 helper(1.0, 0.0, 0.0);
  if (std::abs(axis.y) < std::abs(axis.x))
    helper = double3(0.0, 1.0, 0.0);
  if (std::abs(axis.z) < std::min(std::abs(axis.x), std::abs(axis.y)))
    helper = double3(0.0, 0.0, 1.0);
  const double3 perpendicular = double3::cross(helper, axis);
  const double length = perpendicular.length();
  return length > 0.0 ? perpendicular / length : double3(1.0, 0.0, 0.0);
}

double SKFoldedPolarAngle(double angle)
{
  double wrapped = std::fmod(std::abs(angle), kTwoPi);
  if (wrapped > kPi)
    wrapped = kTwoPi - wrapped;
  return wrapped;
}

SKSweepFrameAxes SKSweepFrame(const std::vector<double3> &axes)
{
  const double3 candidates[6] = {
      double3(1.0, 2.0, 3.0),  double3(-3.0, 1.0, 2.0), double3(2.0, -3.0, 1.0),
      double3(3.0, 2.0, -1.0), double3(1.0, -3.0, -2.0), double3(-2.0, 3.0, -1.0)};

  double3 polarAxis = double3::normalize(candidates[0]);
  double bestSeparation = -1.0;
  for (const double3 &candidate : candidates)
  {
    const double3 direction = double3::normalize(candidate);
    double separation = 1.0;
    for (const double3 &axis : axes)
      separation = std::min(separation, 1.0 - std::abs(double3::dot(direction, axis)));
    if (separation > bestSeparation)
    {
      bestSeparation = separation;
      polarAxis = direction;
    }
    if (bestSeparation > 0.01)
      break;
  }

  const double3 first = SKPerpendicularTo(polarAxis);
  double3 second = double3::cross(polarAxis, first);
  const double secondLength = second.length();
  if (secondLength > 0.0)
    second = second / secondLength;
  return SKSweepFrameAxes{first, second, polarAxis};
}

bool SKDiscWithinDisc(double cosineBetweenAxes, double cosineInner, double sineInner, double cosineOuter,
                      double sineOuter)
{
  if (cosineOuter > cosineInner)
    return false;
  return cosineBetweenAxes >= cosineOuter * cosineInner + sineOuter * sineInner;
}

std::optional<SKSweepCircle> SKMakeSweepCircle(const double3 &axis, double cosineHalfAngle)
{
  if (cosineHalfAngle >= 1.0)
    return std::nullopt;

  SKSweepCircle circle;
  circle.axis = axis;
  circle.cosineHalfAngle = cosineHalfAngle;
  if (cosineHalfAngle <= -1.0)
  {
    circle.halfAngle = kPi;
    circle.sineHalfAngle = 0.0;
    return circle;
  }
  circle.halfAngle = std::acos(cosineHalfAngle);
  circle.sineHalfAngle = std::sin(circle.halfAngle);
  return circle;
}

void SKPruneContainedDiscs(std::vector<SKSweepCircle> &circles)
{
  if (circles.size() < 2)
    return;

  std::vector<bool> redundant(circles.size(), false);
  for (size_t i = 0; i < circles.size(); ++i)
  {
    for (size_t j = 0; j < circles.size(); ++j)
    {
      if (i == j || redundant[j])
        continue;
      if (SKDiscWithinDisc(double3::dot(circles[i].axis, circles[j].axis), circles[i].cosineHalfAngle,
                           circles[i].sineHalfAngle, circles[j].cosineHalfAngle, circles[j].sineHalfAngle))
      {
        redundant[i] = true;
        break;
      }
    }
  }

  std::vector<SKSweepCircle> kept;
  kept.reserve(circles.size());
  for (size_t i = 0; i < circles.size(); ++i)
  {
    if (!redundant[i])
      kept.push_back(circles[i]);
  }
  circles = std::move(kept);
}

std::vector<SKCapCrossing> SKUncoveredCrossings(const std::vector<SKSweepCircle> &circles)
{
  std::vector<SKCapCrossing> crossings;
  if (circles.size() < 2)
    return crossings;

  for (size_t j = 0; j + 1 < circles.size(); ++j)
  {
    for (size_t k = j + 1; k < circles.size(); ++k)
    {
      const SKSweepCircle &first = circles[j];
      const SKSweepCircle &second = circles[k];
      const double alignment = double3::dot(first.axis, second.axis);
      const double denominator = 1.0 - alignment * alignment;
      if (denominator < 1.0e-14)
        continue;

      const double alongFirst = (first.cosineHalfAngle - alignment * second.cosineHalfAngle) / denominator;
      const double alongSecond = (second.cosineHalfAngle - alignment * first.cosineHalfAngle) / denominator;
      const double outOfPlaneSquared =
          (1.0 - alongFirst * first.cosineHalfAngle - alongSecond * second.cosineHalfAngle) / denominator;
      if (outOfPlaneSquared <= 0.0)
        continue;

      const double3 inPlane = first.axis * alongFirst + second.axis * alongSecond;
      const double3 outOfPlane = double3::cross(first.axis, second.axis) * std::sqrt(outOfPlaneSquared);
      for (int side = 0; side < 2; ++side)
      {
        double3 direction = (side == 0) ? inPlane + outOfPlane : inPlane - outOfPlane;
        const double length = direction.length();
        if (length <= 0.0)
          continue;
        direction = direction / length;

        bool covered = false;
        for (size_t l = 0; l < circles.size(); ++l)
        {
          if (l == j || l == k)
            continue;
          if (double3::dot(direction, circles[l].axis) > circles[l].cosineHalfAngle + SKCapCoverTolerance)
          {
            covered = true;
            break;
          }
        }
        if (!covered)
          crossings.push_back(SKCapCrossing{static_cast<int>(j), static_cast<int>(k), direction});
      }
    }
  }
  return crossings;
}

double3 SKLatitudeGap::at(const SKSweepFrameAxes &frame, double cosineAzimuth, double sineAzimuth) const
{
  return frame.first * (sineLatitude * cosineAzimuth) + frame.second * (sineLatitude * sineAzimuth) +
         frame.polar * cosineLatitude;
}

double3 SKLatitudeGap::atBegin(const SKSweepFrameAxes &frame) const
{
  return at(frame, cosineBegin, sineBegin);
}

double3 SKLatitudeGap::atEnd(const SKSweepFrameAxes &frame) const
{
  return at(frame, cosineEnd, sineEnd);
}

const SKGaussRule &SKUnitIntervalGaussRule()
{
  static const SKGaussRule rule = buildUnitIntervalGaussRule();
  return rule;
}

void SKPrepareSweep(std::vector<SKSweepCircle> &circles, const SKSweepFrameAxes &frame,
                    const std::vector<double3> *knownCrossings, SKSweepWorkspace &work)
{
  const double3 firstAxis = frame.first;
  const double3 secondAxis = frame.second;
  const double3 polarAxis = frame.polar;

  work.breakpoints.clear();
  work.breakpoints.push_back(0.0);
  work.breakpoints.push_back(kPi);

  for (size_t i = 0; i < circles.size(); ++i)
  {
    circles[i].polarAngle =
        std::acos(std::min(1.0, std::max(-1.0, double3::dot(circles[i].axis, polarAxis))));
    circles[i].cosinePolar = std::cos(circles[i].polarAngle);
    circles[i].sinePolar = std::sin(circles[i].polarAngle);
    circles[i].azimuth =
        std::atan2(double3::dot(circles[i].axis, secondAxis), double3::dot(circles[i].axis, firstAxis));
    if (circles[i].azimuth < 0.0)
      circles[i].azimuth += kTwoPi;
    circles[i].cosineAzimuth = std::cos(circles[i].azimuth);
    circles[i].sineAzimuth = std::sin(circles[i].azimuth);

    circles[i].lowestLatitude = SKFoldedPolarAngle(circles[i].polarAngle - circles[i].halfAngle);
    circles[i].highestLatitude = SKFoldedPolarAngle(circles[i].polarAngle + circles[i].halfAngle);
    circles[i].reachesOverPole = circles[i].polarAngle < circles[i].halfAngle;
    circles[i].reachesOverAntipole = kPi - circles[i].polarAngle < circles[i].halfAngle;

    work.breakpoints.push_back(circles[i].lowestLatitude);
    work.breakpoints.push_back(circles[i].highestLatitude);
  }

  std::sort(circles.begin(), circles.end(),
            [](const SKSweepCircle &a, const SKSweepCircle &b) { return a.azimuth < b.azimuth; });

  if (knownCrossings != nullptr)
  {
    for (const double3 &crossing : *knownCrossings)
      work.breakpoints.push_back(std::acos(std::min(1.0, std::max(-1.0, double3::dot(crossing, polarAxis)))));
  }
  else
  {
    work.crossings = SKUncoveredCrossings(circles);
    for (const SKCapCrossing &crossing : work.crossings)
      work.breakpoints.push_back(
          std::acos(std::min(1.0, std::max(-1.0, double3::dot(crossing.direction, polarAxis)))));
  }

  std::sort(work.breakpoints.begin(), work.breakpoints.end());
}

void SKPanelBoundaries(double begin, double end, int subdivisions, bool cut, std::vector<double> &panels)
{
  const int parts = std::max(1, subdivisions);
  panels.clear();
  for (int part = 0; part <= parts; ++part)
    panels.push_back(begin + (end - begin) * static_cast<double>(part) / static_cast<double>(parts));
  panels.front() = begin;
  panels.back() = end;

  if (!cut)
    return;

  constexpr int halvingLimit = 60;
  const double roomBelow = begin;
  for (int i = 0; i < halvingLimit; ++i)
  {
    if (panels[1] - panels[0] <= SKPoleClearance * roomBelow)
      break;
    panels.insert(panels.begin() + 1, 0.5 * (panels[0] + panels[1]));
  }

  const double roomAbove = kPi - end;
  for (int i = 0; i < halvingLimit; ++i)
  {
    const size_t last = panels.size() - 1;
    if (panels[last] - panels[last - 1] <= SKPoleClearance * roomAbove)
      break;
    panels.insert(panels.begin() + static_cast<std::ptrdiff_t>(last), 0.5 * (panels[last - 1] + panels[last]));
  }
}

void SKSweepExposedLatitudes(std::vector<SKSweepCircle> &circles, const SKSweepFrameAxes &frame,
                             const std::vector<double3> *knownCrossings, int subdivisions,
                             SKSweepWorkspace &work, const std::function<void(const SKLatitudeGap &)> &measure)
{
  SKPrepareSweep(circles, frame, knownCrossings, work);

  const SKGaussRule &rule = SKUnitIntervalGaussRule();
  const int parts = std::max(1, subdivisions);

  size_t piece = 0;
  while (piece + 1 < work.breakpoints.size())
  {
    const double pieceBegin = work.breakpoints[piece];
    const double pieceEnd = work.breakpoints[piece + 1];
    ++piece;
    if (pieceEnd - pieceBegin < 1.0e-14)
      continue;

    const double interior = 0.5 * (pieceBegin + pieceEnd);
    work.cutting.clear();
    bool buried = false;
    for (size_t i = 0; i < circles.size(); ++i)
    {
      const SKSweepCircle &circle = circles[i];
      if (interior > circle.lowestLatitude && interior < circle.highestLatitude)
      {
        work.cutting.push_back(static_cast<int>(i));
      }
      else if ((interior <= circle.lowestLatitude && circle.reachesOverPole) ||
               (interior >= circle.highestLatitude && circle.reachesOverAntipole))
      {
        buried = true;
        break;
      }
    }
    if (buried)
      continue;

    SKPanelBoundaries(pieceBegin, pieceEnd, parts, !work.cutting.empty(), work.panels);

    size_t panel = 0;
    while (panel + 1 < work.panels.size())
    {
      const double begin = work.panels[panel];
      const double end = work.panels[panel + 1];
      ++panel;
      const double middle = 0.5 * (begin + end);

      for (int half = 0; half < 2; ++half)
      {
        const double anchor = (half == 0) ? begin : end;
        const double span = (half == 0) ? middle - begin : end - middle;
        const double direction = (half == 0) ? 1.0 : -1.0;

        for (int node = 0; node < SKExactQuadratureOrder; ++node)
        {
          const double parameter = rule.nodes[static_cast<size_t>(node)];
          const double latitude = anchor + direction * span * parameter * parameter;
          const double sineLatitude = std::sin(latitude);
          if (sineLatitude <= 0.0)
            continue;

          SKLatitudeGap gap;
          gap.sineLatitude = sineLatitude;
          gap.cosineLatitude = std::cos(latitude);
          gap.weight = 2.0 * span * parameter * rule.weights[static_cast<size_t>(node)];

          work.covered.clear();
          for (int cuttingIndex : work.cutting)
          {
            const SKSweepCircle &circle = circles[static_cast<size_t>(cuttingIndex)];
            const double cosineHalfWidth =
                (circle.cosineHalfAngle - gap.cosineLatitude * circle.cosinePolar) /
                (sineLatitude * circle.sinePolar);
            if (cosineHalfWidth >= 1.0)
              continue;

            const bool whole = cosineHalfWidth <= -1.0;
            const double halfWidth = whole ? kPi : std::acos(cosineHalfWidth);
            const double cosineHalfWidthClamped = whole ? -1.0 : cosineHalfWidth;
            const double sineHalfWidth =
                whole ? 0.0 : std::sqrt(std::max(0.0, 1.0 - cosineHalfWidth * cosineHalfWidth));

            SKCoveredArc arc;
            arc.begin = circle.azimuth - halfWidth;
            arc.end = circle.azimuth + halfWidth;
            arc.cosineBegin = circle.cosineAzimuth * cosineHalfWidthClamped + circle.sineAzimuth * sineHalfWidth;
            arc.sineBegin = circle.sineAzimuth * cosineHalfWidthClamped - circle.cosineAzimuth * sineHalfWidth;
            arc.cosineEnd = circle.cosineAzimuth * cosineHalfWidthClamped - circle.sineAzimuth * sineHalfWidth;
            arc.sineEnd = circle.sineAzimuth * cosineHalfWidthClamped + circle.cosineAzimuth * sineHalfWidth;

            if (arc.begin < 0.0)
            {
              work.covered.push_back(SKCoveredArc{arc.begin + kTwoPi, kTwoPi, arc.cosineBegin, arc.sineBegin, 1.0, 0.0});
              work.covered.push_back(SKCoveredArc{0.0, arc.end, 1.0, 0.0, arc.cosineEnd, arc.sineEnd});
            }
            else if (arc.end > kTwoPi)
            {
              work.covered.push_back(SKCoveredArc{arc.begin, kTwoPi, arc.cosineBegin, arc.sineBegin, 1.0, 0.0});
              work.covered.push_back(SKCoveredArc{0.0, arc.end - kTwoPi, 1.0, 0.0, arc.cosineEnd, arc.sineEnd});
            }
            else
            {
              work.covered.push_back(arc);
            }
          }
          sortByBeginning(work.covered);

          double cursor = 0.0;
          double cosineCursor = 1.0;
          double sineCursor = 0.0;
          for (size_t arcIndex = 0; arcIndex <= work.covered.size(); ++arcIndex)
          {
            const bool last = (arcIndex == work.covered.size());
            const double gapEnd = last ? kTwoPi : work.covered[arcIndex].begin;

            if (gapEnd - cursor > SKSweepGapTolerance)
            {
              gap.begin = cursor;
              gap.end = gapEnd;
              gap.span = gapEnd - cursor;
              gap.cosineBegin = cosineCursor;
              gap.sineBegin = sineCursor;
              gap.cosineEnd = last ? 1.0 : work.covered[arcIndex].cosineBegin;
              gap.sineEnd = last ? 0.0 : work.covered[arcIndex].sineBegin;
              measure(gap);
            }

            if (!last && work.covered[arcIndex].end > cursor)
            {
              cursor = work.covered[arcIndex].end;
              cosineCursor = work.covered[arcIndex].cosineEnd;
              sineCursor = work.covered[arcIndex].sineEnd;
            }
          }
        }
      }
    }
  }
}
