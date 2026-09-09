/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skapolloniusdiagram.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "skvoronoi.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;

double3 skFract(const double3 &p)
{
  return double3(p.x - std::floor(p.x), p.y - std::floor(p.y), p.z - std::floor(p.z));
}

template <typename T>
T skTripleGet(const std::array<T, 3> &values, int i)
{
  return values[static_cast<size_t>(i)];
}

bool skSiteImageLess(const std::pair<int, int3> &lhs, const std::pair<int, int3> &rhs)
{
  if (lhs.first != rhs.first)
    return lhs.first < rhs.first;
  if (lhs.second.x != rhs.second.x)
    return lhs.second.x < rhs.second.x;
  if (lhs.second.y != rhs.second.y)
    return lhs.second.y < rhs.second.y;
  return lhs.second.z < rhs.second.z;
}

bool skSiteImageEqual(const std::pair<int, int3> &lhs, const std::pair<int, int3> &rhs)
{
  return lhs.first == rhs.first && lhs.second == rhs.second;
}

std::vector<std::pair<int, int3>> skUniquedSiteImages(std::vector<std::pair<int, int3>> items)
{
  std::sort(items.begin(), items.end(), skSiteImageLess);
  std::vector<std::pair<int, int3>> unique;
  unique.reserve(items.size());
  for (const auto &item : items)
  {
    if (unique.empty() || !skSiteImageEqual(unique.back(), item))
      unique.push_back(item);
  }
  return unique;
}

std::pair<int32_t, int32_t> skSplitCoordinate(int32_t coordinate, int32_t extent)
{
  int32_t image = 0;
  if (coordinate >= 0)
    image = coordinate / extent;
  else
    image = -(((-coordinate) + extent - 1) / extent);
  return {coordinate - image * extent, image};
}

int skFindParent(std::vector<int> &parent, int node)
{
  int current = node;
  while (parent[static_cast<size_t>(current)] != current)
  {
    parent[static_cast<size_t>(current)] = parent[static_cast<size_t>(parent[static_cast<size_t>(current)])];
    current = parent[static_cast<size_t>(current)];
  }
  return current;
}

int skFindMapParent(std::unordered_map<int, int> &parent, int node)
{
  int current = node;
  while (parent[current] != current)
  {
    parent[current] = parent[parent[current]];
    current = parent[current];
  }
  return current;
}

struct SKApolloniusSiteTuple
{
  std::array<int64_t, 16> data{};
  int64_t orientation = 0;

  bool operator==(const SKApolloniusSiteTuple &other) const
  {
    return data == other.data && orientation == other.orientation;
  }

  bool operator<(const SKApolloniusSiteTuple &other) const
  {
    if (data != other.data)
      return data < other.data;
    return orientation < other.orientation;
  }
};

struct SKApolloniusSiteTupleHash
{
  size_t operator()(const SKApolloniusSiteTuple &key) const
  {
    size_t h = 0;
    for (int64_t value : key.data)
      hash_combine(h, value);
    hash_combine(h, key.orientation);
    return h;
  }
};

struct Int64VectorHash
{
  size_t operator()(const std::vector<int64_t> &key) const
  {
    size_t h = 0;
    for (int64_t value : key)
      hash_combine(h, value);
    return h;
  }
};

struct Int3Hash
{
  size_t operator()(const int3 &key) const
  {
    size_t h = 0;
    hash_combine(h, key.x);
    hash_combine(h, key.y);
    hash_combine(h, key.z);
    return h;
  }
};

std::pair<SKApolloniusSiteTuple, int3> skCanonicalTuple(const std::vector<int> &indices,
                                                       const std::vector<int3> &images, int count,
                                                       std::array<int, 4> *permutation)
{
  struct Pair
  {
    int64_t a = 0, b = 0, c = 0, d = 0, e = 0;
  };
  std::array<Pair, 4> pairs{};
  for (int i = 0; i < count; ++i)
    pairs[static_cast<size_t>(i)] = {static_cast<int64_t>(indices[static_cast<size_t>(i)]),
                                     static_cast<int64_t>(images[static_cast<size_t>(i)].x),
                                     static_cast<int64_t>(images[static_cast<size_t>(i)].y),
                                     static_cast<int64_t>(images[static_cast<size_t>(i)].z),
                                     static_cast<int64_t>(i)};

  std::sort(pairs.begin(), pairs.begin() + count, [](const Pair &lhs, const Pair &rhs) {
    if (lhs.a != rhs.a)
      return lhs.a < rhs.a;
    if (lhs.b != rhs.b)
      return lhs.b < rhs.b;
    if (lhs.c != rhs.c)
      return lhs.c < rhs.c;
    return lhs.d < rhs.d;
  });

  const int3 appliedOffset(static_cast<int>(pairs[0].b), static_cast<int>(pairs[0].c),
                           static_cast<int>(pairs[0].d));
  SKApolloniusSiteTuple key;
  for (int i = 0; i < count; ++i)
  {
    key.data[static_cast<size_t>(4 * i + 0)] = pairs[static_cast<size_t>(i)].a;
    key.data[static_cast<size_t>(4 * i + 1)] = pairs[static_cast<size_t>(i)].b - appliedOffset.x;
    key.data[static_cast<size_t>(4 * i + 2)] = pairs[static_cast<size_t>(i)].c - appliedOffset.y;
    key.data[static_cast<size_t>(4 * i + 3)] = pairs[static_cast<size_t>(i)].d - appliedOffset.z;
    if (permutation != nullptr)
      (*permutation)[static_cast<size_t>(i)] = static_cast<int>(pairs[static_cast<size_t>(i)].e);
  }
  return {key, appliedOffset};
}

std::pair<SKApolloniusSiteTuple, int3> skCanonicalTuple(const std::vector<int> &indices,
                                                       const std::vector<int3> &images, int count)
{
  return skCanonicalTuple(indices, images, count, nullptr);
}

std::vector<int> skCoincidentGroups(double3x3 unitCell, double3x3 inverseCell,
                                    const double3 &perpendicularWidths, const std::vector<double3> &positions,
                                    double tolerance)
{
  const int32_t mostDivisions = 1 << 20;
  auto divisionsAlong = [&](double width) -> int32_t {
    const double raw = std::min(width / tolerance, 1.0e9);
    return std::max(int32_t(1), std::min(static_cast<int32_t>(raw), mostDivisions));
  };
  const int3 divisions(divisionsAlong(perpendicularWidths.x), divisionsAlong(perpendicularWidths.y),
                       divisionsAlong(perpendicularWidths.z));

  auto cellOf = [&](const double3 &position) -> int3 {
    const double3 fractional = skFract(inverseCell * position);
    return int3(std::min(divisions.x - 1, static_cast<int32_t>(fractional.x * double(divisions.x))),
                std::min(divisions.y - 1, static_cast<int32_t>(fractional.y * double(divisions.y))),
                std::min(divisions.z - 1, static_cast<int32_t>(fractional.z * double(divisions.z))));
  };

  std::unordered_map<int3, std::vector<int>, Int3Hash> occupants;
  std::vector<int> group(positions.size(), 0);
  for (size_t p = 0; p < positions.size(); ++p)
  {
    group[p] = static_cast<int>(p);
    const int3 own = cellOf(positions[p]);

    bool found = false;
    for (int32_t ox = -1; ox <= 1 && !found; ++ox)
    {
      for (int32_t oy = -1; oy <= 1 && !found; ++oy)
      {
        for (int32_t oz = -1; oz <= 1 && !found; ++oz)
        {
          const int3 neighbour(((own.x + ox + divisions.x) % divisions.x),
                               ((own.y + oy + divisions.y) % divisions.y),
                               ((own.z + oz + divisions.z) % divisions.z));
          const auto it = occupants.find(neighbour);
          if (it == occupants.end())
            continue;
          for (int other : it->second)
          {
            double3 delta = inverseCell * (positions[p] - positions[static_cast<size_t>(other)]);
            delta = double3(delta.x - std::round(delta.x), delta.y - std::round(delta.y),
                            delta.z - std::round(delta.z));
            if ((unitCell * delta).length() < tolerance)
            {
              group[p] = other;
              found = true;
              break;
            }
          }
        }
      }
    }
    occupants[own].push_back(static_cast<int>(p));
  }
  return group;
}

std::vector<SKApolloniusBranch> skVertexBranches(const double3 &centre, const std::vector<double3> &siteCentres,
                                                 int &ambiguous)
{
  const int count = static_cast<int>(siteCentres.size());
  std::vector<double3> unitToSite(static_cast<size_t>(count));
  for (int s = 0; s < count; ++s)
  {
    const double3 delta = centre - siteCentres[static_cast<size_t>(s)];
    const double length = delta.length();
    unitToSite[static_cast<size_t>(s)] = (length > 1.0e-12) ? (1.0 / length) * delta : double3();
  }

  const double decidableRate = 1.0e-9;
  std::vector<SKApolloniusBranch> branches;
  for (int i = 0; i < count; ++i)
  {
    for (int j = i + 1; j < count; ++j)
    {
      for (int k = j + 1; k < count; ++k)
      {
        double3 tangent = double3::cross(unitToSite[static_cast<size_t>(i)] - unitToSite[static_cast<size_t>(j)],
                                         unitToSite[static_cast<size_t>(j)] - unitToSite[static_cast<size_t>(k)]);
        const double length = tangent.length();
        if (length < 1.0e-14)
          continue;
        tangent = (1.0 / length) * tangent;

        for (double sign : {1.0, -1.0})
        {
          const double3 direction = sign * tangent;
          bool outgoing = true;
          bool undecided = false;
          for (int m = 0; m < count && outgoing; ++m)
          {
            if (m == i || m == j || m == k)
              continue;
            const double rate =
                double3::dot(unitToSite[static_cast<size_t>(m)] - unitToSite[static_cast<size_t>(i)], direction);
            if (rate < -decidableRate)
              outgoing = false;
            else if (rate <= decidableRate)
              undecided = true;
          }
          if (!outgoing)
            continue;
          if (undecided)
            ++ambiguous;
          branches.emplace_back(std::array<int, 3>{{i, j, k}}, direction);
        }
      }
    }
  }
  return branches;
}

int64_t skTangencyOrientation(const double3 &centre, double radius, const std::vector<double3> &siteCentres,
                              const std::array<int, 4> &permutation)
{
  std::array<double3, 4> tangency{};
  for (int i = 0; i < 4; ++i)
  {
    const double3 delta = siteCentres[static_cast<size_t>(permutation[static_cast<size_t>(i)])] - centre;
    const double length = delta.length();
    tangency[static_cast<size_t>(i)] = (length > 1.0e-12) ? centre + (radius / length) * delta : centre;
  }
  const double determinant =
      double3::dot(tangency[1] - tangency[0], double3::cross(tangency[2] - tangency[0], tangency[3] - tangency[0]));
  return (determinant >= 0.0) ? 1 : -1;
}

namespace SKApolloniusAlgebra
{
std::vector<SKApolloniusTangentSphere> solveTangencyRows(const std::array<double3, 3> &rows,
                                                         const double3 &timeCoefficients, const double3 &constants,
                                                         const double3 &referenceCentre, double referenceRadius,
                                                         bool allowNegativeRadius)
{
  std::array<std::array<double, 4>, 3> system{};
  double scale = 0.0;
  for (int i = 0; i < 3; ++i)
  {
    system[static_cast<size_t>(i)] = {rows[static_cast<size_t>(i)].x, rows[static_cast<size_t>(i)].y,
                                      rows[static_cast<size_t>(i)].z, timeCoefficients[i]};
    scale = std::max(scale, std::sqrt(rows[static_cast<size_t>(i)].length_squared() +
                                      timeCoefficients[i] * timeCoefficients[i]));
  }

  auto minorWithout = [&](int skipped) -> double {
    std::array<int, 3> kept{};
    int written = 0;
    for (int column = 0; column < 4; ++column)
    {
      if (column != skipped)
      {
        kept[static_cast<size_t>(written)] = column;
        ++written;
      }
    }
    auto entry = [&](int row, int column) -> double {
      return system[static_cast<size_t>(row)][static_cast<size_t>(kept[static_cast<size_t>(column)])];
    };
    return entry(0, 0) * (entry(1, 1) * entry(2, 2) - entry(1, 2) * entry(2, 1)) -
           entry(0, 1) * (entry(1, 0) * entry(2, 2) - entry(1, 2) * entry(2, 0)) +
           entry(0, 2) * (entry(1, 0) * entry(2, 1) - entry(1, 1) * entry(2, 0));
  };

  std::array<double, 4> along = {minorWithout(0), -minorWithout(1), minorWithout(2), -minorWithout(3)};
  const double alongNorm =
      std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2] + along[3] * along[3]);

  if (scale <= 0.0 || alongNorm < 1.0e-12 * scale * scale * scale)
    return {};

  for (int i = 0; i < 4; ++i)
    along[static_cast<size_t>(i)] /= alongNorm;

  int held = 0;
  for (int column = 1; column < 4; ++column)
  {
    if (std::abs(along[static_cast<size_t>(column)]) > std::abs(along[static_cast<size_t>(held)]))
      held = column;
  }
  std::array<int, 3> kept{};
  int written = 0;
  for (int column = 0; column < 4; ++column)
  {
    if (column != held)
    {
      kept[static_cast<size_t>(written)] = column;
      ++written;
    }
  }

  double3x3 reduced(double3(system[0][static_cast<size_t>(kept[0])], system[1][static_cast<size_t>(kept[0])],
                            system[2][static_cast<size_t>(kept[0])]),
                    double3(system[0][static_cast<size_t>(kept[1])], system[1][static_cast<size_t>(kept[1])],
                            system[2][static_cast<size_t>(kept[1])]),
                    double3(system[0][static_cast<size_t>(kept[2])], system[1][static_cast<size_t>(kept[2])],
                            system[2][static_cast<size_t>(kept[2])]));
  const double3 solved = reduced.inverse() * constants;

  std::array<double, 4> base = {0.0, 0.0, 0.0, 0.0};
  base[static_cast<size_t>(held)] = 0.0;
  for (int column = 0; column < 3; ++column)
    base[static_cast<size_t>(kept[static_cast<size_t>(column)])] = solved[column];

  const double3 baseCentre(base[0], base[1], base[2]);
  const double baseRadius = base[3];
  const double3 alongCentre(along[0], along[1], along[2]);
  const double alongRadius = along[3];

  const double3 offset = baseCentre - referenceCentre;
  const double reach = referenceRadius + baseRadius;
  const double a = double3::dot(alongCentre, alongCentre) - alongRadius * alongRadius;
  const double b = 2.0 * (double3::dot(offset, alongCentre) - reach * alongRadius);
  const double c = double3::dot(offset, offset) - reach * reach;

  std::array<double, 2> roots = {0.0, 0.0};
  int rootCount = 0;
  if (std::abs(a) < 1.0e-14)
  {
    if (std::abs(b) > 1.0e-14 * scale)
    {
      roots[static_cast<size_t>(rootCount)] = -c / b;
      ++rootCount;
    }
  }
  else
  {
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
      return {};
    const double squareRoot = std::sqrt(discriminant);
    roots[static_cast<size_t>(rootCount++)] = (-b + squareRoot) / (2.0 * a);
    roots[static_cast<size_t>(rootCount++)] = (-b - squareRoot) / (2.0 * a);
  }

  std::vector<SKApolloniusTangentSphere> spheres;
  for (int root = 0; root < rootCount; ++root)
  {
    const double s = roots[static_cast<size_t>(root)];
    const double t = baseRadius + s * alongRadius;
    if (t < 0.0 && !allowNegativeRadius)
      continue;
    if (referenceRadius + t < 0.0)
      continue;
    spheres.emplace_back(baseCentre + s * alongCentre, t);
  }
  return spheres;
}

void tangencyRow(const double3 &centre, double radius, const double3 &referenceCentre, double referenceRadius,
                 double3 &row, double &timeCoefficient, double &constant)
{
  row = 2.0 * (centre - referenceCentre);
  timeCoefficient = 2.0 * (radius - referenceRadius);
  constant = (double3::dot(centre, centre) - radius * radius) -
             (double3::dot(referenceCentre, referenceCentre) - referenceRadius * referenceRadius);
}

std::vector<double3> trisectorPointsAtClearance(const std::vector<double3> &centres, const std::vector<double> &radii,
                                                double clearance)
{
  const double referenceDistance = radii[0] + clearance;
  if (referenceDistance < 0.0)
    return {};

  std::array<double3, 2> rows{};
  std::array<double, 2> constants = {0.0, 0.0};
  for (int i = 0; i < 2; ++i)
  {
    double timeCoefficient = 0.0;
    tangencyRow(centres[static_cast<size_t>(i + 1)], radii[static_cast<size_t>(i + 1)], centres[0], radii[0],
                rows[static_cast<size_t>(i)], timeCoefficient, constants[static_cast<size_t>(i)]);
    constants[static_cast<size_t>(i)] -= timeCoefficient * clearance;
  }

  const double3 direction = double3::cross(rows[0], rows[1]);
  const double directionLength = direction.length();
  const double scale = std::max(rows[0].length(), rows[1].length());
  if (scale <= 0.0 || directionLength < 1.0e-12 * scale * scale)
    return {};

  double3x3 matrix(double3(rows[0].x, rows[1].x, direction.x), double3(rows[0].y, rows[1].y, direction.y),
                   double3(rows[0].z, rows[1].z, direction.z));
  if (std::abs(matrix.determinant()) < 1.0e-30)
    return {};
  const double3 base = matrix.inverse() * double3(constants[0], constants[1], 0.0);

  const double3 offset = base - centres[0];
  const double a = double3::dot(direction, direction);
  const double b = 2.0 * double3::dot(offset, direction);
  const double c = double3::dot(offset, offset) - referenceDistance * referenceDistance;
  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0.0)
    return {};

  const double root = std::sqrt(discriminant);
  std::vector<double3> points;
  points.push_back(base + ((-b + root) / (2.0 * a)) * direction);
  if (root > 1.0e-9 * (std::abs(b) + a))
    points.push_back(base + ((-b - root) / (2.0 * a)) * direction);
  return points;
}

std::optional<double3> bisectorSheetPoint(const double3 &firstCentre, double firstRadius, const double3 &secondCentre,
                                          double secondRadius, double clearance, double angle)
{
  const double firstDistance = firstRadius + clearance;
  const double secondDistance = secondRadius + clearance;
  if (firstDistance < 0.0 || secondDistance < 0.0)
    return std::nullopt;

  double3 axis = secondCentre - firstCentre;
  const double separation = axis.length();
  if (separation < 1.0e-12)
    return std::nullopt;
  axis = (1.0 / separation) * axis;

  const double along =
      (separation * separation + firstDistance * firstDistance - secondDistance * secondDistance) / (2.0 * separation);
  const double radialSquared = firstDistance * firstDistance - along * along;
  if (radialSquared < 0.0)
    return std::nullopt;
  const double radial = std::sqrt(radialSquared);

  const double3 reference = (std::abs(axis.x) < 0.9) ? double3(1.0, 0.0, 0.0) : double3(0.0, 1.0, 0.0);
  double3 inPlane = double3::cross(axis, reference);
  inPlane = (1.0 / inPlane.length()) * inPlane;
  const double3 across = double3::cross(axis, inPlane);

  return firstCentre + along * axis + (radial * std::cos(angle)) * inPlane + (radial * std::sin(angle)) * across;
}

std::vector<SKApolloniusTangentSphere> tangentSpheresOf(const std::vector<double3> &centres,
                                                        const std::vector<double> &radii, bool allowNegativeRadius)
{
  std::array<double3, 3> rows{};
  double3 timeArray{};
  double3 constantArray{};
  for (int i = 0; i < 3; ++i)
  {
    tangencyRow(centres[static_cast<size_t>(i + 1)], radii[static_cast<size_t>(i + 1)], centres[0], radii[0],
                rows[static_cast<size_t>(i)], timeArray[i], constantArray[i]);
  }

  const std::vector<SKApolloniusTangentSphere> solutions =
      solveTangencyRows(rows, timeArray, constantArray, centres[0], radii[0], allowNegativeRadius);

  std::vector<SKApolloniusTangentSphere> kept;
  for (const SKApolloniusTangentSphere &sphere : solutions)
  {
    bool tangent = true;
    for (int i = 0; i < 4 && tangent; ++i)
    {
      const double tangentDistance = radii[static_cast<size_t>(i)] + sphere.radius;
      if (tangentDistance < 0.0 ||
          std::abs((sphere.centre - centres[static_cast<size_t>(i)]).length() - tangentDistance) >
              1.0e-6 * std::max(1.0, tangentDistance))
      {
        tangent = false;
      }
    }
    if (tangent)
      kept.push_back(sphere);
  }
  return kept;
}
} // namespace SKApolloniusAlgebra

struct SKApolloniusSiteGrid
{
  double3x3 cell{};
  double3x3 inverseCell{};
  int3 binCount{1, 1, 1};
  double3 binWidth{1.0, 1.0, 1.0};
  std::vector<double3> positions;
  std::vector<std::vector<int>> bins;

  void build(const double3x3 &unitCell, const double3x3 &unitCellInverse, const double3 &perpendicularWidths,
             const std::vector<double3> &fractionalPositions, double volume)
  {
    cell = unitCell;
    inverseCell = unitCellInverse;
    const int count = static_cast<int>(fractionalPositions.size());
    const double target = std::cbrt(volume / std::max(1.0, double(count) / 4.0));
    binCount = int3(std::max(1, static_cast<int32_t>(perpendicularWidths.x / target)),
                    std::max(1, static_cast<int32_t>(perpendicularWidths.y / target)),
                    std::max(1, static_cast<int32_t>(perpendicularWidths.z / target)));
    binWidth = double3(perpendicularWidths.x / double(binCount.x), perpendicularWidths.y / double(binCount.y),
                       perpendicularWidths.z / double(binCount.z));

    positions.assign(static_cast<size_t>(count), double3());
    bins.assign(static_cast<size_t>(binCount.x) * static_cast<size_t>(binCount.y) * static_cast<size_t>(binCount.z),
                {});
    for (int i = 0; i < count; ++i)
    {
      const double3 fractional = skFract(fractionalPositions[static_cast<size_t>(i)]);
      positions[static_cast<size_t>(i)] = cell * fractional;
      const int32_t bx = std::min(binCount.x - 1, static_cast<int32_t>(fractional.x * double(binCount.x)));
      const int32_t by = std::min(binCount.y - 1, static_cast<int32_t>(fractional.y * double(binCount.y)));
      const int32_t bz = std::min(binCount.z - 1, static_cast<int32_t>(fractional.z * double(binCount.z)));
      bins[static_cast<size_t>((bz * binCount.y + by) * binCount.x + bx)].push_back(i);
    }
  }

  template <typename Visitor>
  void forEachNear(const double3 &wrappedCentre, double searchRadius, Visitor &&visit) const
  {
    const double3 fractional = inverseCell * wrappedCentre;
    const int32_t cx = std::min(binCount.x - 1, static_cast<int32_t>(fractional.x * double(binCount.x)));
    const int32_t cy = std::min(binCount.y - 1, static_cast<int32_t>(fractional.y * double(binCount.y)));
    const int32_t cz = std::min(binCount.z - 1, static_cast<int32_t>(fractional.z * double(binCount.z)));

    const int3 span(static_cast<int32_t>(std::ceil(searchRadius / binWidth.x)) + 1,
                    static_cast<int32_t>(std::ceil(searchRadius / binWidth.y)) + 1,
                    static_cast<int32_t>(std::ceil(searchRadius / binWidth.z)) + 1);

    for (int32_t oz = -span.z; oz <= span.z; ++oz)
    {
      for (int32_t oy = -span.y; oy <= span.y; ++oy)
      {
        for (int32_t ox = -span.x; ox <= span.x; ++ox)
        {
          const auto x = skSplitCoordinate(cx + ox, binCount.x);
          const auto y = skSplitCoordinate(cy + oy, binCount.y);
          const auto z = skSplitCoordinate(cz + oz, binCount.z);
          const int3 image(x.second, y.second, z.second);
          const double3 shift = cell * double3(image);
          for (int j : bins[static_cast<size_t>((z.first * binCount.y + y.first) * binCount.x + x.first)])
            visit(j, positions[static_cast<size_t>(j)] + shift, image);
        }
      }
    }
  }

  bool intrudes(const double3 &wrappedCentre, double radius, const std::vector<double> &radii, double maximumRadius,
                double tolerance) const
  {
    const double searchRadius = std::max(0.0, radius + maximumRadius);
    const double3 fractional = inverseCell * wrappedCentre;
    const int32_t cx = std::min(binCount.x - 1, static_cast<int32_t>(fractional.x * double(binCount.x)));
    const int32_t cy = std::min(binCount.y - 1, static_cast<int32_t>(fractional.y * double(binCount.y)));
    const int32_t cz = std::min(binCount.z - 1, static_cast<int32_t>(fractional.z * double(binCount.z)));

    const int3 span(static_cast<int32_t>(std::ceil(searchRadius / binWidth.x)) + 1,
                    static_cast<int32_t>(std::ceil(searchRadius / binWidth.y)) + 1,
                    static_cast<int32_t>(std::ceil(searchRadius / binWidth.z)) + 1);
    const int32_t widest = std::max(span.x, std::max(span.y, span.z));

    for (int32_t shell = 0; shell <= widest; ++shell)
    {
      const int32_t zLimit = std::min(shell, span.z);
      for (int32_t oz = -zLimit; oz <= zLimit; ++oz)
      {
        const int32_t yLimit = std::min(shell, span.y);
        for (int32_t oy = -yLimit; oy <= yLimit; ++oy)
        {
          const int32_t xLimit = std::min(shell, span.x);
          for (int32_t ox = -xLimit; ox <= xLimit; ++ox)
          {
            if (std::max(std::abs(ox), std::max(std::abs(oy), std::abs(oz))) != shell)
              continue;

            const auto x = skSplitCoordinate(cx + ox, binCount.x);
            const auto y = skSplitCoordinate(cy + oy, binCount.y);
            const auto z = skSplitCoordinate(cz + oz, binCount.z);
            const double3 shift = cell * double3(double(x.second), double(y.second), double(z.second));
            for (int j : bins[static_cast<size_t>((z.first * binCount.y + y.first) * binCount.x + x.first)])
            {
              if ((wrappedCentre - (positions[static_cast<size_t>(j)] + shift)).length() <
                  radii[static_cast<size_t>(j)] + radius - tolerance)
                return true;
            }
          }
        }
      }
    }
    return false;
  }

  double clearance(const double3 &wrappedPoint, const std::vector<double> &radii, double upperBound,
                   double maximumRadius, int &nearestIndex, int3 &nearestImage) const
  {
    double best = upperBound;
    nearestIndex = std::numeric_limits<int>::max();
    forEachNear(wrappedPoint, upperBound + maximumRadius,
                [&](int j, const double3 &image, const int3 &latticeImage) {
                  const double value = (wrappedPoint - image).length() - radii[static_cast<size_t>(j)];
                  if (value < best)
                  {
                    best = value;
                    nearestIndex = j;
                    nearestImage = latticeImage;
                  }
                });
    return best;
  }
};

namespace SKApolloniusBuild
{
struct IncidentBranch
{
  int vertex = 0;
  int branch = 0;
  int3 offset{};
};

struct CandidateSite
{
  int index = 0;
  int3 image{};
  double3 position{};
};

struct Cube
{
  double3 originFractional{};
  int level = 0;
  int parentNearestCount = 0;
};

struct PatchEdge
{
  int edgeIndex = 0;
  int ownerLocal = 0;
  int otherLocal = 0;
};

struct PrimitivePatch
{
  int owner = 0;
  int other = 0;
  int3 relativeImage{};
  std::vector<PatchEdge> edges;
};

void storeCertifiedVertex(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                          double maximumRadius, double emptyRadiusBound, double emptinessTolerance,
                          const SKApolloniusSiteGrid &grid, const std::vector<int> &siteIndices,
                          const std::vector<int3> &siteImages, const std::vector<double3> &centres,
                          const SKApolloniusTangentSphere &sphere, SKApolloniusDiagram &diagram,
                          std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup);

void certifyAndStore(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                     double maximumRadius, double emptyRadiusBound, double emptinessTolerance,
                     bool allowNegativeRadius, const SKApolloniusSiteGrid &grid, const std::vector<int> &quadIndices,
                     const std::vector<int3> &quadImages, const std::vector<CandidateSite> &nearby,
                     SKApolloniusDiagram &diagram,
                     std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup,
                     std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &solvedQuadruples);

void sweepCell(const double3x3 &unitCell, const double3x3 &inverseCell, const double3 &perpendicularWidths,
               const std::vector<double> &radii, double maximumRadius, double emptyRadiusBound,
               double emptinessTolerance, bool allowNegativeRadius, const SKApolloniusSiteGrid &grid,
               SKApolloniusDiagram &diagram,
               std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup,
               std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &solvedQuadruples,
               std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &candidateTriples);

void mergeCotangentVertices(const double3x3 &unitCell, const double3x3 &inverseCell,
                            const double3 &perpendicularWidths, const SKApolloniusSiteGrid &grid,
                            SKApolloniusDiagram &diagram);

void pairEdges(
    const double3x3 &unitCell, const std::vector<double> &radii, const SKApolloniusSiteGrid &grid,
    const std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> &tripleLookup, SKApolloniusDiagram &diagram);

void recoverRings(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                  double maximumRadius, double emptyRadiusBound, bool allowNegativeRadius,
                  const SKApolloniusSiteGrid &grid,
                  const std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &candidateTriples,
                  const std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> &tripleLookup,
                  SKApolloniusDiagram &diagram, std::unordered_map<int, double> &ringTopClearance);

void assembleFacesAndCells(const double3x3 &unitCell, const std::vector<double> &radii,
                           const SKApolloniusSiteGrid &grid, int siteCount,
                           const std::unordered_map<int, double> &ringTopClearance, SKApolloniusDiagram &diagram);

void verifyDiagram(const double3x3 &unitCell, const double3x3 &inverseCell, const double3 &perpendicularWidths,
                   SKApolloniusDiagram &diagram);

SKApolloniusDiagram create(const double3x3 &unitCell, const std::vector<double3> &fractionalPositions,
                           const std::vector<double> &radii, int neighbourRings, SKApolloniusRegion region)
{
  const bool allowNegativeRadius = (region == SKApolloniusRegion::EntireSpace);

  SKApolloniusDiagram diagram;
  const int siteCount = static_cast<int>(fractionalPositions.size());
  if (siteCount == 0 || static_cast<int>(radii.size()) != siteCount)
    return diagram;

  double3x3 mutableCell = unitCell;
  const double3x3 inverseCell = mutableCell.inverse();
  const double volume = std::abs(mutableCell.determinant());

  const double3 a = unitCell[0];
  const double3 b = unitCell[1];
  const double3 c = unitCell[2];
  const double3 perpendicularWidths(volume / double3::cross(b, c).length(), volume / double3::cross(c, a).length(),
                                    volume / double3::cross(a, b).length());

  double maximumRadius = 0.0;
  for (double radius : radii)
    maximumRadius = std::max(maximumRadius, radius);

  const double emptyRadiusBound = 0.5 * (a.length() + b.length() + c.length());

  SKApolloniusSiteGrid grid;
  grid.build(unitCell, inverseCell, perpendicularWidths, fractionalPositions, volume);

  std::vector<std::vector<std::pair<int, int3>>> adjacency(static_cast<size_t>(siteCount));
  try
  {
    const SKVoronoi radical(unitCell, fractionalPositions, radii);
    const std::vector<SKVoronoiCell> radicalCells = radical.computeAllCells();
    for (int i = 0; i < siteCount; ++i)
    {
      for (const SKVoronoiFace &face : radicalCells[static_cast<size_t>(i)].faces)
        adjacency[static_cast<size_t>(i)].emplace_back(face.neighborIndex, face.neighborImage);
    }
  }
  catch (...)
  {
  }

  int ring = 1;
  while (ring < std::max(1, neighbourRings))
  {
    auto widened = adjacency;
    for (int i = 0; i < siteCount; ++i)
    {
      for (const auto &pairJ : adjacency[static_cast<size_t>(i)])
      {
        const int j = pairJ.first;
        const int3 imageJ = pairJ.second;
        for (const auto &pairK : adjacency[static_cast<size_t>(j)])
        {
          const int k = pairK.first;
          const int3 imageK = pairK.second;
          const int3 image(imageJ.x + imageK.x, imageJ.y + imageK.y, imageJ.z + imageK.z);
          if (k == i && image.x == 0 && image.y == 0 && image.z == 0)
            continue;
          widened[static_cast<size_t>(i)].emplace_back(k, image);
        }
      }
      widened[static_cast<size_t>(i)] = skUniquedSiteImages(std::move(widened[static_cast<size_t>(i)]));
    }
    adjacency = std::move(widened);
    ++ring;
  }

  std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> vertexLookup;
  const double emptinessTolerance = 1.0e-7;

  std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> solvedQuadruples;
  auto quadrupleIsNew = [&](const std::vector<int> &indices, const std::vector<int3> &images) -> bool {
    const auto result = skCanonicalTuple(indices, images, 4);
    return solvedQuadruples.insert(result.first).second;
  };

  for (int i = 0; i < siteCount; ++i)
  {
    const auto &neighbours = adjacency[static_cast<size_t>(i)];
    const int count = static_cast<int>(neighbours.size());
    const double3 centreI = grid.positions[static_cast<size_t>(i)];

    if (count < 3)
      continue;
    for (int p = 0; p < count; ++p)
    {
      for (int q = p + 1; q < count; ++q)
      {
        for (int r = q + 1; r < count; ++r)
        {
          const std::vector<int> siteIndices = {i, neighbours[static_cast<size_t>(p)].first,
                                                neighbours[static_cast<size_t>(q)].first,
                                                neighbours[static_cast<size_t>(r)].first};
          const std::vector<int3> siteImages = {int3(), neighbours[static_cast<size_t>(p)].second,
                                                neighbours[static_cast<size_t>(q)].second,
                                                neighbours[static_cast<size_t>(r)].second};

          if (!quadrupleIsNew(siteIndices, siteImages))
            continue;

          std::vector<double3> centres(4);
          std::vector<double> tangentRadii(4, 0.0);
          centres[0] = centreI;
          tangentRadii[0] = radii[static_cast<size_t>(i)];
          for (int s = 1; s < 4; ++s)
          {
            centres[static_cast<size_t>(s)] =
                grid.positions[static_cast<size_t>(siteIndices[static_cast<size_t>(s)])] +
                unitCell * double3(siteImages[static_cast<size_t>(s)]);
            tangentRadii[static_cast<size_t>(s)] = radii[static_cast<size_t>(siteIndices[static_cast<size_t>(s)])];
          }

          for (const SKApolloniusTangentSphere &sphere :
               SKApolloniusAlgebra::tangentSpheresOf(centres, tangentRadii, allowNegativeRadius))
          {
            storeCertifiedVertex(unitCell, inverseCell, radii, maximumRadius, emptyRadiusBound, emptinessTolerance,
                                 grid, siteIndices, siteImages, centres, sphere, diagram, vertexLookup);
          }
        }
      }
    }
  }

  auto buildTripleLookup = [&]() -> std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> {
    std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> lookup;
    for (size_t v = 0; v < diagram.vertices.size(); ++v)
    {
      const SKApolloniusVertex &vertex = diagram.vertices[v];
      for (size_t b = 0; b < vertex.branches.size(); ++b)
      {
        std::vector<int> indices = {0, 0, 0};
        std::vector<int3> images = {int3(), int3(), int3()};
        for (int s = 0; s < 3; ++s)
        {
          const int local = skTripleGet(vertex.branches[b].sites, s);
          indices[static_cast<size_t>(s)] = vertex.siteIndices[static_cast<size_t>(local)];
          images[static_cast<size_t>(s)] = vertex.siteImages[static_cast<size_t>(local)];
        }
        const auto result = skCanonicalTuple(indices, images, 3);
        lookup[result.first].push_back(
            IncidentBranch{static_cast<int>(v), static_cast<int>(b), result.second});
      }
    }
    return lookup;
  };

  std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> candidateTriples;
  sweepCell(unitCell, inverseCell, perpendicularWidths, radii, maximumRadius, emptyRadiusBound, emptinessTolerance,
            allowNegativeRadius, grid, diagram, vertexLookup, solvedQuadruples, candidateTriples);

  mergeCotangentVertices(unitCell, inverseCell, perpendicularWidths, grid, diagram);

  const auto tripleLookup = buildTripleLookup();
  pairEdges(unitCell, radii, grid, tripleLookup, diagram);

  std::unordered_map<int, double> ringTopClearance;
  recoverRings(unitCell, inverseCell, radii, maximumRadius, emptyRadiusBound, allowNegativeRadius, grid,
               candidateTriples, tripleLookup, diagram, ringTopClearance);

  assembleFacesAndCells(unitCell, radii, grid, siteCount, ringTopClearance, diagram);

  verifyDiagram(unitCell, inverseCell, perpendicularWidths, diagram);

  return diagram;
}

void storeCertifiedVertex(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                          double maximumRadius, double emptyRadiusBound, double emptinessTolerance,
                          const SKApolloniusSiteGrid &grid, const std::vector<int> &siteIndices,
                          const std::vector<int3> &siteImages, const std::vector<double3> &centres,
                          const SKApolloniusTangentSphere &sphere, SKApolloniusDiagram &diagram,
                          std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup)
{
  if (sphere.radius > emptyRadiusBound)
    return;

  const double3 wrappedCentre = unitCell * skFract(inverseCell * sphere.centre);
  const double3 latticeShift = wrappedCentre - sphere.centre;
  if (grid.intrudes(wrappedCentre, sphere.radius, radii, maximumRadius, emptinessTolerance))
    return;

  const double3 shiftFractional = inverseCell * latticeShift;
  const int3 shift(static_cast<int>(std::llround(shiftFractional.x)), static_cast<int>(std::llround(shiftFractional.y)),
                   static_cast<int>(std::llround(shiftFractional.z)));
  std::vector<int3> shiftedImages(4);
  for (int s = 0; s < 4; ++s)
  {
    shiftedImages[static_cast<size_t>(s)] =
        int3(siteImages[static_cast<size_t>(s)].x + shift.x, siteImages[static_cast<size_t>(s)].y + shift.y,
             siteImages[static_cast<size_t>(s)].z + shift.z);
  }

  std::array<int, 4> permutation = {0, 0, 0, 0};
  SKApolloniusSiteTuple key = skCanonicalTuple(siteIndices, shiftedImages, 4, &permutation).first;
  std::vector<double3> shiftedCentres(4);
  for (int s = 0; s < 4; ++s)
    shiftedCentres[static_cast<size_t>(s)] = centres[static_cast<size_t>(s)] + latticeShift;
  key.orientation = skTangencyOrientation(wrappedCentre, sphere.radius, shiftedCentres, permutation);
  if (vertexLookup.find(key) != vertexLookup.end())
    return;

  vertexLookup[key] = static_cast<int>(diagram.vertices.size());
  diagram.vertices.emplace_back(wrappedCentre, sphere.radius, siteIndices, shiftedImages,
                                std::vector<SKApolloniusBranch>{});
}

void certifyAndStore(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                     double maximumRadius, double emptyRadiusBound, double emptinessTolerance,
                     bool allowNegativeRadius, const SKApolloniusSiteGrid &grid, const std::vector<int> &quadIndices,
                     const std::vector<int3> &quadImages, const std::vector<CandidateSite> &nearby,
                     SKApolloniusDiagram &diagram,
                     std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup,
                     std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &solvedQuadruples)
{
  const auto result = skCanonicalTuple(quadIndices, quadImages, 4);
  if (!solvedQuadruples.insert(result.first).second)
    return;

  std::vector<double3> quadCentres(4);
  std::vector<double> quadRadii(4, 0.0);
  for (int s = 0; s < 4; ++s)
  {
    quadCentres[static_cast<size_t>(s)] =
        grid.positions[static_cast<size_t>(quadIndices[static_cast<size_t>(s)])] +
        unitCell * double3(quadImages[static_cast<size_t>(s)]);
    quadRadii[static_cast<size_t>(s)] = radii[static_cast<size_t>(quadIndices[static_cast<size_t>(s)])];
  }

  for (const SKApolloniusTangentSphere &sphere :
       SKApolloniusAlgebra::tangentSpheresOf(quadCentres, quadRadii, allowNegativeRadius))
  {
    if (sphere.radius > emptyRadiusBound)
      continue;

    bool spoiled = false;
    for (size_t s = 0; s < nearby.size() && !spoiled; ++s)
    {
      spoiled = (sphere.centre - nearby[s].position).length() <
                radii[static_cast<size_t>(nearby[s].index)] + sphere.radius - emptinessTolerance;
    }
    if (spoiled)
      continue;

    storeCertifiedVertex(unitCell, inverseCell, radii, maximumRadius, emptyRadiusBound, emptinessTolerance, grid,
                         quadIndices, quadImages, quadCentres, sphere, diagram, vertexLookup);
  }
}

void sweepCell(const double3x3 &unitCell, const double3x3 &inverseCell, const double3 &perpendicularWidths,
               const std::vector<double> &radii, double maximumRadius, double emptyRadiusBound,
               double emptinessTolerance, bool allowNegativeRadius, const SKApolloniusSiteGrid &grid,
               SKApolloniusDiagram &diagram,
               std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> &vertexLookup,
               std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &solvedQuadruples,
               std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &candidateTriples)
{
  auto circumradius = [&](const double3 &sizeFractional) -> double {
    double largest = 0.0;
    for (double sx : {-0.5, 0.5})
    {
      for (double sy : {-0.5, 0.5})
      {
        for (double sz : {-0.5, 0.5})
        {
          largest = std::max(largest, (unitCell * double3(sx * sizeFractional.x, sy * sizeFractional.y,
                                                          sz * sizeFractional.z))
                                          .length());
        }
      }
    }
    return largest;
  };

  auto gatherFromGrid = [&](const double3 &centre, double radius, std::vector<CandidateSite> &out) {
    out.clear();
    double searchRadius = maximumRadius + 4.0 * radius + 1.0;
    double smallest = 0.0;
    bool found = false;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
      found = false;
      smallest = std::numeric_limits<double>::max();
      grid.forEachNear(centre, searchRadius, [&](int j, const double3 &image, const int3 &) {
        found = true;
        smallest = std::min(smallest, (centre - image).length() - radii[static_cast<size_t>(j)]);
      });
      if (found && searchRadius >= smallest + 4.0 * radius + maximumRadius)
        break;
      searchRadius = found ? (smallest + 4.0 * radius + maximumRadius + 1.0e-6) : (2.0 * searchRadius);
    }
    if (!found)
      return;

    const double threshold = smallest + 4.0 * radius;
    grid.forEachNear(centre, searchRadius, [&](int j, const double3 &image, const int3 &imageOffset) {
      if ((centre - image).length() - radii[static_cast<size_t>(j)] <= threshold)
        out.push_back(CandidateSite{j, imageOffset, image});
    });
  };

  const double smallestBoxCircumradius = 1.0e-3;
  const double initialBoxEdge = 3.0;
  const int3 initialDivisions(std::max(1, static_cast<int>(std::llround(perpendicularWidths.x / initialBoxEdge))),
                              std::max(1, static_cast<int>(std::llround(perpendicularWidths.y / initialBoxEdge))),
                              std::max(1, static_cast<int>(std::llround(perpendicularWidths.z / initialBoxEdge))));
  const double3 initialSize(1.0 / double(initialDivisions.x), 1.0 / double(initialDivisions.y),
                            1.0 / double(initialDivisions.z));

  std::vector<Cube> stack;
  std::vector<std::vector<CandidateSite>> levelReachable(1);
  std::vector<double3> levelSize = {initialSize};
  std::vector<double> levelRadius = {circumradius(initialSize)};

  auto reachLevel = [&](int level) {
    while (static_cast<int>(levelSize.size()) <= level)
    {
      levelSize.push_back(0.5 * levelSize.back());
      levelRadius.push_back(0.5 * levelRadius.back());
    }
    if (static_cast<int>(levelReachable.size()) <= level)
      levelReachable.resize(static_cast<size_t>(level) + 1);
  };

  for (int32_t ix = 0; ix < initialDivisions.x; ++ix)
  {
    for (int32_t iy = 0; iy < initialDivisions.y; ++iy)
    {
      for (int32_t iz = 0; iz < initialDivisions.z; ++iz)
      {
        stack.push_back(Cube{double3(double(ix) * initialSize.x, double(iy) * initialSize.y, double(iz) * initialSize.z),
                             0, std::numeric_limits<int>::max()});
      }
    }
  }

  std::vector<double> clearances;
  std::vector<std::pair<int, int3>> nearestSites;
  std::unordered_set<std::vector<int64_t>, Int64VectorHash> finishedSets;

  while (!stack.empty())
  {
    const Cube cube = stack.back();
    stack.pop_back();

    reachLevel(cube.level + 1);

    const double3 centre = unitCell * (cube.originFractional + 0.5 * levelSize[static_cast<size_t>(cube.level)]);
    const double radius = levelRadius[static_cast<size_t>(cube.level)];

    if (cube.level == 0)
      gatherFromGrid(centre, radius, levelReachable[0]);
    const std::vector<CandidateSite> &reachable = levelReachable[static_cast<size_t>(cube.level)];

    clearances.assign(reachable.size(), 0.0);
    double smallest = std::numeric_limits<double>::max();
    for (size_t s = 0; s < reachable.size(); ++s)
    {
      clearances[s] = (centre - reachable[s].position).length() - radii[static_cast<size_t>(reachable[s].index)];
      smallest = std::min(smallest, clearances[s]);
    }
    if (reachable.empty())
      continue;

    if (!allowNegativeRadius && smallest + radius < 0.0)
      continue;

    nearestSites.clear();
    for (size_t s = 0; s < reachable.size(); ++s)
    {
      if (clearances[s] < smallest + 2.0 * radius)
        nearestSites.emplace_back(reachable[s].index, reachable[s].image);
    }

    const int fewSites = 8;
    const int affordableSites = 16;
    const bool splittingIsHelping = static_cast<int>(nearestSites.size()) < cube.parentNearestCount;
    if (static_cast<int>(nearestSites.size()) <= fewSites || radius < smallestBoxCircumradius || !splittingIsHelping)
    {
      if (static_cast<int>(nearestSites.size()) > fewSites)
        ++diagram.verification.degenerateSweepBoxes;

      if (nearestSites.size() >= 3)
      {
        std::sort(nearestSites.begin(), nearestSites.end(), skSiteImageLess);
        const int3 base = nearestSites[0].second;
        std::vector<int64_t> finishedKey;
        finishedKey.reserve(nearestSites.size() * 4);
        for (const auto &pair : nearestSites)
        {
          finishedKey.push_back(static_cast<int64_t>(pair.first));
          finishedKey.push_back(static_cast<int64_t>(pair.second.x - base.x));
          finishedKey.push_back(static_cast<int64_t>(pair.second.y - base.y));
          finishedKey.push_back(static_cast<int64_t>(pair.second.z - base.z));
        }
        if (!finishedSets.insert(finishedKey).second)
          continue;

        const auto sites = nearestSites;
        for (size_t i = 0; i < sites.size(); ++i)
        {
          for (size_t j = i + 1; j < sites.size(); ++j)
          {
            for (size_t k = j + 1; k < sites.size(); ++k)
            {
              const std::vector<int> tripleIndices = {sites[i].first, sites[j].first, sites[k].first};
              const std::vector<int3> tripleImages = {sites[i].second, sites[j].second, sites[k].second};
              candidateTriples.insert(skCanonicalTuple(tripleIndices, tripleImages, 3).first);

              if (static_cast<int>(sites.size()) <= affordableSites)
              {
                for (size_t l = k + 1; l < sites.size(); ++l)
                {
                  certifyAndStore(unitCell, inverseCell, radii, maximumRadius, emptyRadiusBound, emptinessTolerance,
                                  allowNegativeRadius, grid,
                                  {sites[i].first, sites[j].first, sites[k].first, sites[l].first},
                                  {sites[i].second, sites[j].second, sites[k].second, sites[l].second}, reachable,
                                  diagram, vertexLookup, solvedQuadruples);
                }
              }
            }
          }
        }
      }
      continue;
    }

    std::vector<CandidateSite> childReachable;
    for (size_t s = 0; s < reachable.size(); ++s)
    {
      if (clearances[s] <= smallest + 4.0 * radius)
        childReachable.push_back(reachable[s]);
    }
    levelReachable[static_cast<size_t>(cube.level + 1)] = childReachable;

    const double3 half = levelSize[static_cast<size_t>(cube.level + 1)];
    for (int ix = 0; ix < 2; ++ix)
    {
      for (int iy = 0; iy < 2; ++iy)
      {
        for (int iz = 0; iz < 2; ++iz)
        {
          stack.push_back(Cube{cube.originFractional + double3(double(ix) * half.x, double(iy) * half.y, double(iz) * half.z),
                               cube.level + 1, static_cast<int>(nearestSites.size())});
        }
      }
    }
  }
}

void mergeCotangentVertices(const double3x3 &unitCell, const double3x3 &inverseCell,
                            const double3 &perpendicularWidths, const SKApolloniusSiteGrid &grid,
                            SKApolloniusDiagram &diagram)
{
  const double coincidenceTolerance = 1.0e-6;
  const int rawCount = static_cast<int>(diagram.vertices.size());
  std::vector<int> parent(static_cast<size_t>(rawCount), 0);
  for (int v = 0; v < rawCount; ++v)
    parent[static_cast<size_t>(v)] = v;

  std::vector<double3> rawPositions;
  rawPositions.reserve(static_cast<size_t>(rawCount));
  for (const SKApolloniusVertex &vertex : diagram.vertices)
    rawPositions.push_back(vertex.position);

  const std::vector<int> groupOf =
      skCoincidentGroups(unitCell, inverseCell, perpendicularWidths, rawPositions, coincidenceTolerance);
  for (int v = 0; v < rawCount; ++v)
  {
    const int left = skFindParent(parent, v);
    const int right = skFindParent(parent, groupOf[static_cast<size_t>(v)]);
    if (left != right)
      parent[static_cast<size_t>(left)] = right;
  }

  std::map<int, std::vector<int>> groups;
  for (int v = 0; v < rawCount; ++v)
    groups[skFindParent(parent, v)].push_back(v);

  std::vector<SKApolloniusVertex> merged;
  merged.reserve(groups.size());
  for (const auto &entry : groups)
  {
    const std::vector<int> &members = entry.second;
    const SKApolloniusVertex &leader = diagram.vertices[static_cast<size_t>(members[0])];
    std::vector<std::pair<int, int3>> cotangent;
    for (int member : members)
    {
      const SKApolloniusVertex &copy = diagram.vertices[static_cast<size_t>(member)];
      const double3 delta = inverseCell * (leader.position - copy.position);
      const int3 shift(static_cast<int>(std::llround(delta.x)), static_cast<int>(std::llround(delta.y)),
                       static_cast<int>(std::llround(delta.z)));
      for (size_t s = 0; s < copy.siteIndices.size(); ++s)
      {
        cotangent.emplace_back(copy.siteIndices[s],
                               int3(copy.siteImages[s].x + shift.x, copy.siteImages[s].y + shift.y,
                                    copy.siteImages[s].z + shift.z));
      }
    }
    cotangent = skUniquedSiteImages(std::move(cotangent));

    SKApolloniusVertex vertex(leader.position, leader.radius, {}, {}, {});
    std::vector<double3> siteCentres;
    for (const auto &pair : cotangent)
    {
      vertex.siteIndices.push_back(pair.first);
      vertex.siteImages.push_back(pair.second);
      siteCentres.push_back(grid.positions[static_cast<size_t>(pair.first)] + unitCell * double3(pair.second));
    }
    vertex.branches = skVertexBranches(vertex.position, siteCentres, diagram.verification.ambiguousBranches);
    if (vertex.siteIndices.size() > 4)
      ++diagram.verification.degenerateVertices;
    merged.push_back(std::move(vertex));
  }
  diagram.vertices = std::move(merged);
}

void pairEdges(const double3x3 &unitCell, const std::vector<double> &radii, const SKApolloniusSiteGrid &grid,
               const std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> &tripleLookup,
               SKApolloniusDiagram &diagram)
{
  enum class Shape
  {
    Line,
    Ellipse,
    Branch
  };

  struct OrderedVertex
  {
    int vertex = 0;
    int3 offset{};
    double along = 0.0;
    bool forward = false;
  };

  for (const auto &entry : tripleLookup)
  {
    const SKApolloniusSiteTuple &key = entry.first;
    const std::vector<IncidentBranch> &incident = entry.second;
    if (incident.size() < 2)
    {
      ++diagram.verification.truncatedTriples;
      continue;
    }

    std::array<int, 3> baseIndices = {0, 0, 0};
    std::array<int3, 3> baseImages{};
    std::array<double3, 3> baseCentres{};
    std::array<double, 3> tangentRadii = {0.0, 0.0, 0.0};
    for (int s = 0; s < 3; ++s)
    {
      baseIndices[static_cast<size_t>(s)] = static_cast<int>(key.data[static_cast<size_t>(4 * s + 0)]);
      baseImages[static_cast<size_t>(s)] =
          int3(static_cast<int>(key.data[static_cast<size_t>(4 * s + 1)]),
               static_cast<int>(key.data[static_cast<size_t>(4 * s + 2)]),
               static_cast<int>(key.data[static_cast<size_t>(4 * s + 3)]));
      baseCentres[static_cast<size_t>(s)] =
          grid.positions[static_cast<size_t>(baseIndices[static_cast<size_t>(s)])] +
          unitCell * double3(baseImages[static_cast<size_t>(s)]);
      tangentRadii[static_cast<size_t>(s)] = radii[static_cast<size_t>(baseIndices[static_cast<size_t>(s)])];
    }

    std::array<double3, 2> difference{};
    std::array<double, 2> radiusDifference = {0.0, 0.0};
    std::array<double, 2> constants = {0.0, 0.0};
    for (int i = 0; i < 2; ++i)
    {
      difference[static_cast<size_t>(i)] =
          2.0 * (baseCentres[static_cast<size_t>(i + 1)] - baseCentres[0]);
      radiusDifference[static_cast<size_t>(i)] =
          2.0 * (tangentRadii[static_cast<size_t>(i + 1)] - tangentRadii[0]);
      constants[static_cast<size_t>(i)] =
          double3::dot(baseCentres[static_cast<size_t>(i + 1)], baseCentres[static_cast<size_t>(i + 1)]) -
          double3::dot(baseCentres[0], baseCentres[0]) -
          tangentRadii[static_cast<size_t>(i + 1)] * tangentRadii[static_cast<size_t>(i + 1)] +
          tangentRadii[0] * tangentRadii[0];
    }
    if (std::abs(radiusDifference[1]) > std::abs(radiusDifference[0]))
    {
      std::swap(difference[0], difference[1]);
      std::swap(radiusDifference[0], radiusDifference[1]);
      std::swap(constants[0], constants[1]);
    }

    Shape shape = Shape::Line;
    double3 lineDirection{};
    double3 planeOrigin{};
    double3 firstAxis{};
    double3 secondAxis{};
    double centreOffset = 0.0;
    double firstSemiAxis = 0.0;
    double secondSemiAxis = 0.0;
    double rate = 0.0;
    double originClearance = 0.0;
    double reach = 0.0;
    double flattening = 0.0;
    double extent = 0.0;
    double heightSquared = 0.0;

    const double radiusScale =
        std::max(std::max(tangentRadii[0], tangentRadii[1]), std::max(tangentRadii[2], 1.0));
    if (std::abs(radiusDifference[0]) < 1.0e-14 * radiusScale)
    {
      lineDirection = double3::cross(difference[0], difference[1]);
      const double straightness = difference[0].length() * difference[1].length();
      if (lineDirection.length() < 1.0e-12 * straightness)
      {
        ++diagram.verification.unpairedTriples;
        continue;
      }
      lineDirection = lineDirection / lineDirection.length();
    }
    else
    {
      double3 planeNormal = radiusDifference[0] * difference[1] - radiusDifference[1] * difference[0];
      const double planeScale = std::abs(radiusDifference[0]) * difference[1].length() +
                                std::abs(radiusDifference[1]) * difference[0].length();
      if (planeNormal.length() < 1.0e-12 * planeScale)
      {
        ++diagram.verification.unpairedTriples;
        continue;
      }
      const double planeConstant =
          (radiusDifference[0] * constants[1] - radiusDifference[1] * constants[0]) / planeNormal.length();
      planeNormal = planeNormal / planeNormal.length();

      planeOrigin = baseCentres[0] + (planeConstant - double3::dot(planeNormal, baseCentres[0])) * planeNormal;
      const double3 fromSite = planeOrigin - baseCentres[0];

      const double3 gradient =
          -(difference[0] - double3::dot(difference[0], planeNormal) * planeNormal) / radiusDifference[0];
      rate = gradient.length();
      if (rate > 0.0)
        firstAxis = gradient / rate;
      else
        firstAxis = double3::cross(planeNormal, (std::abs(planeNormal.x) < 0.9) ? double3(1.0, 0.0, 0.0)
                                                                                : double3(0.0, 1.0, 0.0));
      firstAxis = firstAxis / firstAxis.length();
      secondAxis = double3::cross(planeNormal, firstAxis);

      originClearance = (constants[0] - double3::dot(difference[0], planeOrigin)) / radiusDifference[0];
      reach = tangentRadii[0] + originClearance;
      heightSquared = double3::dot(fromSite, fromSite);
      flattening = 1.0 - rate * rate;

      if (rate < 1.0)
      {
        shape = Shape::Ellipse;
        centreOffset = reach * rate / flattening;
        extent = reach * reach / flattening - heightSquared;
        if (extent <= 0.0)
        {
          ++diagram.verification.unpairedTriples;
          continue;
        }
        secondSemiAxis = std::sqrt(extent);
        firstSemiAxis = std::sqrt(extent / flattening);
      }
      else
      {
        shape = Shape::Branch;
        if (std::abs(flattening) > 1.0e-12)
        {
          centreOffset = reach * rate / flattening;
          extent = reach * reach / flattening - heightSquared;
        }
      }
    }

    std::vector<OrderedVertex> ordered;
    ordered.reserve(incident.size());
    for (const IncidentBranch &branchEntry : incident)
    {
      const SKApolloniusVertex &incidentVertex = diagram.vertices[static_cast<size_t>(branchEntry.vertex)];
      const double3 position = incidentVertex.position - unitCell * double3(branchEntry.offset);
      const double3 outgoing = incidentVertex.branches[static_cast<size_t>(branchEntry.branch)].direction;

      double along = 0.0;
      bool forward = false;
      if (shape == Shape::Line)
      {
        along = double3::dot(position, lineDirection);
        forward = double3::dot(outgoing, lineDirection) > 0.0;
      }
      else
      {
        const double first = double3::dot(position - planeOrigin, firstAxis) - centreOffset;
        const double second = double3::dot(position - planeOrigin, secondAxis);
        const double firstStep = double3::dot(outgoing, firstAxis);
        const double secondStep = double3::dot(outgoing, secondAxis);
        if (shape == Shape::Ellipse)
        {
          along = std::atan2(second / secondSemiAxis, first / firstSemiAxis);
          forward = secondSemiAxis * secondSemiAxis * first * secondStep -
                        firstSemiAxis * firstSemiAxis * second * firstStep >
                    0.0;
        }
        else
        {
          along = second;
          forward = secondStep > 0.0;
        }
      }
      ordered.push_back(OrderedVertex{branchEntry.vertex, branchEntry.offset, along, forward});
    }
    if (ordered.size() < 2)
    {
      ++diagram.verification.unpairedTriples;
      continue;
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const OrderedVertex &lhs, const OrderedVertex &rhs) { return lhs.along < rhs.along; });

    double3 lineBase{};
    double branchSign = 1.0;
    const OrderedVertex &reference = ordered[0];
    const double3 referencePosition =
        diagram.vertices[static_cast<size_t>(reference.vertex)].position - unitCell * double3(reference.offset);
    if (shape == Shape::Line)
      lineBase = referencePosition - reference.along * lineDirection;
    else if (shape == Shape::Branch && std::abs(flattening) > 1.0e-12)
    {
      const double first = double3::dot(referencePosition - planeOrigin, firstAxis) - centreOffset;
      branchSign = (first < 0.0) ? -1.0 : 1.0;
    }

    auto curvePoint = [&](double alongParam) -> std::optional<std::pair<double3, double>> {
      if (shape == Shape::Line)
      {
        const double3 position = lineBase + alongParam * lineDirection;
        return std::make_pair(position, (position - baseCentres[0]).length() - tangentRadii[0]);
      }

      double first = 0.0;
      double second = 0.0;
      if (shape == Shape::Ellipse)
      {
        first = firstSemiAxis * std::cos(alongParam);
        second = secondSemiAxis * std::sin(alongParam);
      }
      else
      {
        second = alongParam;
        if (std::abs(flattening) > 1.0e-12)
        {
          const double squared = (extent - second * second) / flattening;
          if (squared < 0.0)
            return std::nullopt;
          first = branchSign * std::sqrt(squared);
        }
        else
        {
          if (std::abs(reach) < 1.0e-12)
            return std::nullopt;
          first = (second * second + heightSquared - reach * reach) / (2.0 * reach);
        }
      }

      const double alongFirstAxis = centreOffset + first;
      return std::make_pair(planeOrigin + alongFirstAxis * firstAxis + second * secondAxis,
                            originClearance + rate * alongFirstAxis);
    };

    std::array<double, 3> turningPoints = {0.0, 0.0, 0.0};
    int turningCount = 0;
    if (shape == Shape::Line)
    {
      turningPoints[static_cast<size_t>(turningCount++)] = double3::dot(baseCentres[0] - lineBase, lineDirection);
    }
    else if (shape == Shape::Ellipse)
    {
      if (rate > 0.0)
      {
        turningPoints[static_cast<size_t>(turningCount++)] = -kPi;
        turningPoints[static_cast<size_t>(turningCount++)] = kPi;
        turningPoints[static_cast<size_t>(turningCount++)] = 3.0 * kPi;
      }
    }
    else
    {
      turningPoints[static_cast<size_t>(turningCount++)] = 0.0;
    }

    for (size_t p = 0; p < ordered.size(); ++p)
    {
      const size_t q = (p + 1) % ordered.size();
      if (q == p)
        continue;

      if (!ordered[p].forward)
        continue;
      if (ordered[q].forward)
        continue;

      const int fromVertex = ordered[p].vertex;
      const int toVertex = ordered[q].vertex;
      const int3 fromOffset = ordered[p].offset;
      const int3 toOffset = ordered[q].offset;

      if (fromVertex == toVertex && fromOffset.x == toOffset.x && fromOffset.y == toOffset.y &&
          fromOffset.z == toOffset.z)
        continue;

      const int3 toImage(fromOffset.x - toOffset.x, fromOffset.y - toOffset.y, fromOffset.z - toOffset.z);

      const std::array<int, 3> indices = {baseIndices[0], baseIndices[1], baseIndices[2]};
      std::array<int3, 3> images{};
      std::array<double3, 3> centres{};
      for (int s = 0; s < 3; ++s)
      {
        images[static_cast<size_t>(s)] =
            int3(baseImages[static_cast<size_t>(s)].x + fromOffset.x,
                 baseImages[static_cast<size_t>(s)].y + fromOffset.y,
                 baseImages[static_cast<size_t>(s)].z + fromOffset.z);
        centres[static_cast<size_t>(s)] =
            grid.positions[static_cast<size_t>(baseIndices[static_cast<size_t>(s)])] +
            unitCell * double3(images[static_cast<size_t>(s)]);
      }

      const double3 from = diagram.vertices[static_cast<size_t>(fromVertex)].position;
      const double3 to = diagram.vertices[static_cast<size_t>(toVertex)].position + unitCell * double3(toImage);

      const int sampleCount = 16;
      std::vector<double3> curve(static_cast<size_t>(sampleCount + 1 + 3));
      std::vector<double> clearance(static_cast<size_t>(sampleCount + 1 + 3), 0.0);

      const double alongFrom = ordered[p].along;
      double alongTo = ordered[q].along;
      bool spanned = true;
      if (alongTo < alongFrom)
      {
        if (shape == Shape::Ellipse)
          alongTo += 2.0 * kPi;
        else
          spanned = false;
      }

      const double3 toArcFrame = unitCell * double3(fromOffset);

      curve[0] = from;
      clearance[0] = diagram.vertices[static_cast<size_t>(fromVertex)].radius;
      curve[static_cast<size_t>(sampleCount)] = to;
      clearance[static_cast<size_t>(sampleCount)] = diagram.vertices[static_cast<size_t>(toVertex)].radius;

      double length = 0.0;
      bool sampled = spanned;
      for (int s = 1; s <= sampleCount; ++s)
      {
        const double parameter = double(s) / double(sampleCount);
        std::optional<std::pair<double3, double>> point;
        if (spanned)
          point = curvePoint(alongFrom + parameter * (alongTo - alongFrom));
        if (point && s < sampleCount)
        {
          curve[static_cast<size_t>(s)] = point->first + toArcFrame;
          clearance[static_cast<size_t>(s)] = point->second;
        }
        else if (s < sampleCount)
        {
          curve[static_cast<size_t>(s)] = from + parameter * (to - from);
          clearance[static_cast<size_t>(s)] = std::numeric_limits<double>::max();
          sampled = false;
        }
        length += (curve[static_cast<size_t>(s)] - curve[static_cast<size_t>(s - 1)]).length();
      }

      int sampleTotal = sampleCount + 1;
      std::array<double3, 3> turningChord{};
      if (spanned)
      {
        for (int k = 0; k < turningCount; ++k)
        {
          const double turn = turningPoints[static_cast<size_t>(k)];
          if (turn <= alongFrom || turn >= alongTo)
            continue;
          const auto point = curvePoint(turn);
          if (!point)
          {
            sampled = false;
            continue;
          }
          const double step = 1.0e-4 * std::max(std::abs(alongTo - alongFrom), 1.0e-6);
          const auto before = curvePoint(std::max(turn - step, alongFrom));
          const auto after = curvePoint(std::min(turn + step, alongTo));
          turningChord[static_cast<size_t>(sampleTotal - (sampleCount + 1))] =
              (before && after) ? (after->first - before->first) : (to - from);
          curve[static_cast<size_t>(sampleTotal)] = point->first + toArcFrame;
          clearance[static_cast<size_t>(sampleTotal)] = point->second;
          ++sampleTotal;
        }
      }
      if (!sampled)
        ++diagram.verification.unsampledArcs;

      int narrowest = 0;
      for (int s = 1; s < sampleTotal; ++s)
      {
        if (clearance[static_cast<size_t>(s)] < clearance[static_cast<size_t>(narrowest)])
          narrowest = s;
      }

      double3 chord;
      if (narrowest > sampleCount)
        chord = turningChord[static_cast<size_t>(narrowest - (sampleCount + 1))];
      else
        chord = curve[static_cast<size_t>(std::min(narrowest + 1, sampleCount))] -
                curve[static_cast<size_t>(narrowest > 0 ? narrowest - 1 : 0)];

      std::array<double3, 3> fromSite{};
      for (int s = 0; s < 3; ++s)
      {
        const double3 offset = curve[static_cast<size_t>(narrowest)] - centres[static_cast<size_t>(s)];
        const double offsetLength = offset.length();
        fromSite[static_cast<size_t>(s)] = offsetLength > 0.0 ? offset / offsetLength : double3();
      }
      double3 tangent = double3::cross(fromSite[0] - fromSite[1], fromSite[0] - fromSite[2]);
      if (tangent.length() < 1.0e-10)
        tangent = chord;
      if (double3::dot(tangent, chord) < 0.0)
        tangent = -tangent;

      const double tangentLength = tangent.length();
      const double3 direction = tangentLength > 0.0 ? tangent / tangentLength : double3();

      diagram.edges.emplace_back(fromVertex, toVertex, toImage, indices, images, clearance[static_cast<size_t>(narrowest)],
                                 curve[static_cast<size_t>(narrowest)], direction, length, false);
    }
  }
}

void recoverRings(const double3x3 &unitCell, const double3x3 &inverseCell, const std::vector<double> &radii,
                  double maximumRadius, double emptyRadiusBound, bool allowNegativeRadius,
                  const SKApolloniusSiteGrid &grid,
                  const std::unordered_set<SKApolloniusSiteTuple, SKApolloniusSiteTupleHash> &candidateTriples,
                  const std::map<SKApolloniusSiteTuple, std::vector<IncidentBranch>> &tripleLookup,
                  SKApolloniusDiagram &diagram, std::unordered_map<int, double> &ringTopClearance)
{
  double smallestRadius = std::numeric_limits<double>::max();
  for (double radius : radii)
    smallestRadius = std::min(smallestRadius, radius);
  const double lowestClearance = allowNegativeRadius ? -smallestRadius : 0.0;

  const int bracketSamples = 192;
  const int traverseSamples = 96;
  const int bisectionSteps = 40;
  const double intrusionTolerance = 1.0e-7;

  std::vector<SKApolloniusSiteTuple> ringCandidates(candidateTriples.begin(), candidateTriples.end());
  std::sort(ringCandidates.begin(), ringCandidates.end());

  for (const SKApolloniusSiteTuple &key : ringCandidates)
  {
    if (tripleLookup.find(key) != tripleLookup.end())
      continue;

    std::vector<int> indices = {0, 0, 0};
    std::vector<int3> images = {int3(), int3(), int3()};
    std::vector<double3> centres = {double3(), double3(), double3()};
    std::vector<double> tripleRadii = {0.0, 0.0, 0.0};
    for (int s = 0; s < 3; ++s)
    {
      indices[static_cast<size_t>(s)] = static_cast<int>(key.data[static_cast<size_t>(4 * s + 0)]);
      images[static_cast<size_t>(s)] =
          int3(static_cast<int>(key.data[static_cast<size_t>(4 * s + 1)]),
               static_cast<int>(key.data[static_cast<size_t>(4 * s + 2)]),
               static_cast<int>(key.data[static_cast<size_t>(4 * s + 3)]));
      centres[static_cast<size_t>(s)] =
          grid.positions[static_cast<size_t>(indices[static_cast<size_t>(s)])] +
          unitCell * double3(images[static_cast<size_t>(s)]);
      tripleRadii[static_cast<size_t>(s)] = radii[static_cast<size_t>(indices[static_cast<size_t>(s)])];
    }

    if (!SKApolloniusAlgebra::trisectorPointsAtClearance(centres, tripleRadii, emptyRadiusBound).empty())
      continue;

    double firstPresent = 0.0;
    double lastPresent = 0.0;
    bool anyPresent = false;
    for (int sample = 0; sample <= bracketSamples; ++sample)
    {
      const double clearance =
          lowestClearance + (emptyRadiusBound - lowestClearance) * double(sample) / double(bracketSamples);
      if (SKApolloniusAlgebra::trisectorPointsAtClearance(centres, tripleRadii, clearance).empty())
        continue;
      if (!anyPresent)
        firstPresent = clearance;
      lastPresent = clearance;
      anyPresent = true;
    }
    if (!anyPresent)
      continue;

    double clearanceBelow =
        std::max(lowestClearance, firstPresent - (emptyRadiusBound - lowestClearance) / double(bracketSamples));
    double minimumClearance = firstPresent;
    for (int step = 0; step < bisectionSteps; ++step)
    {
      const double middle = 0.5 * (clearanceBelow + minimumClearance);
      if (SKApolloniusAlgebra::trisectorPointsAtClearance(centres, tripleRadii, middle).empty())
        clearanceBelow = middle;
      else
        minimumClearance = middle;
    }
    double clearanceAbove = lastPresent + (emptyRadiusBound - lowestClearance) / double(bracketSamples);
    double maximumClearance = lastPresent;
    for (int step = 0; step < bisectionSteps; ++step)
    {
      const double middle = 0.5 * (maximumClearance + clearanceAbove);
      if (SKApolloniusAlgebra::trisectorPointsAtClearance(centres, tripleRadii, middle).empty())
        clearanceAbove = middle;
      else
        maximumClearance = middle;
    }
    if (maximumClearance <= minimumClearance)
      continue;

    bool intruded = false;
    double length = 0.0;
    std::array<std::optional<double3>, 2> previousOnBranch = {std::nullopt, std::nullopt};
    std::array<double3, 2> firstOnBranch = {double3(), double3()};
    std::array<std::optional<double3>, 2> secondOnBranch = {std::nullopt, std::nullopt};
    int sample = 0;
    while (sample <= traverseSamples && !intruded)
    {
      const double clearance =
          minimumClearance + (maximumClearance - minimumClearance) * double(sample) / double(traverseSamples);
      const std::vector<double3> points =
          SKApolloniusAlgebra::trisectorPointsAtClearance(centres, tripleRadii, clearance);
      size_t branch = 0;
      while (branch < points.size() && !intruded)
      {
        const double3 wrapped = unitCell * skFract(inverseCell * points[branch]);
        grid.forEachNear(wrapped, std::max(0.0, clearance + maximumRadius),
                         [&](int j, const double3 &image, const int3 &) {
                           if ((wrapped - image).length() - radii[static_cast<size_t>(j)] <
                               clearance - intrusionTolerance)
                             intruded = true;
                         });

        if (previousOnBranch[branch])
        {
          length += (points[branch] - *previousOnBranch[branch]).length();
          if (!secondOnBranch[branch])
            secondOnBranch[branch] = points[branch];
        }
        else
        {
          firstOnBranch[branch] = points[branch];
        }
        previousOnBranch[branch] = points[branch];
        ++branch;
      }
      ++sample;
    }
    if (intruded)
      continue;

    if (previousOnBranch[0] && previousOnBranch[1])
    {
      length += (*previousOnBranch[0] - *previousOnBranch[1]).length();
      length += (firstOnBranch[0] - firstOnBranch[1]).length();
    }

    double3 pinch = previousOnBranch[0] ? firstOnBranch[0] : firstOnBranch[1];
    double3 chord{};
    if (previousOnBranch[0] && previousOnBranch[1])
    {
      pinch = 0.5 * (firstOnBranch[0] + firstOnBranch[1]);
      if (secondOnBranch[0] && secondOnBranch[1])
        chord = *secondOnBranch[0] - *secondOnBranch[1];
    }
    const double chordLength = chord.length();

    ringTopClearance[static_cast<int>(diagram.edges.size())] = maximumClearance;
    diagram.edges.emplace_back(
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), int3(),
        std::array<int, 3>{{indices[0], indices[1], indices[2]}},
        std::array<int3, 3>{{images[0], images[1], images[2]}}, minimumClearance, pinch,
        chordLength > 0.0 ? chord / chordLength : double3(), length, true);
    ++diagram.verification.vertexlessLoops;
  }
}

void assembleFacesAndCells(const double3x3 &unitCell, const std::vector<double> &radii,
                           const SKApolloniusSiteGrid &grid, int siteCount,
                           const std::unordered_map<int, double> &ringTopClearance, SKApolloniusDiagram &diagram)
{
  auto faceKey = [](int owner, int other, const int3 &relativeImage) -> SKApolloniusSiteTuple {
    SKApolloniusSiteTuple key;
    key.data[0] = static_cast<int64_t>(owner);
    key.data[4] = static_cast<int64_t>(other);
    key.data[5] = static_cast<int64_t>(relativeImage.x);
    key.data[6] = static_cast<int64_t>(relativeImage.y);
    key.data[7] = static_cast<int64_t>(relativeImage.z);
    return key;
  };

  std::vector<PrimitivePatch> patches;
  std::unordered_map<SKApolloniusSiteTuple, int, SKApolloniusSiteTupleHash> patchLookup;

  for (size_t e = 0; e < diagram.edges.size(); ++e)
  {
    const SKApolloniusEdge &edge = diagram.edges[e];
    for (int s = 0; s < 3; ++s)
    {
      for (int o = 0; o < 3; ++o)
      {
        if (s == o)
          continue;
        const int3 relative(skTripleGet(edge.siteImages, o).x - skTripleGet(edge.siteImages, s).x,
                            skTripleGet(edge.siteImages, o).y - skTripleGet(edge.siteImages, s).y,
                            skTripleGet(edge.siteImages, o).z - skTripleGet(edge.siteImages, s).z);
        const SKApolloniusSiteTuple key =
            faceKey(skTripleGet(edge.siteIndices, s), skTripleGet(edge.siteIndices, o), relative);
        const auto existing = patchLookup.find(key);
        if (existing != patchLookup.end())
        {
          patches[static_cast<size_t>(existing->second)].edges.push_back(
              PatchEdge{static_cast<int>(e), s, o});
        }
        else
        {
          patchLookup[key] = static_cast<int>(patches.size());
          patches.push_back(PrimitivePatch{skTripleGet(edge.siteIndices, s), skTripleGet(edge.siteIndices, o),
                                           relative, {PatchEdge{static_cast<int>(e), s, o}}});
        }
      }
    }
  }

  auto ringBoundsOwnFace = [&](const PatchEdge &patchEdge) -> bool {
    const SKApolloniusEdge &ring = diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)];
    const auto topIt = ringTopClearance.find(patchEdge.edgeIndex);
    if (topIt == ringTopClearance.end())
      return true;
    const double top = topIt->second;

    const int thirdLocal = 3 - patchEdge.ownerLocal - patchEdge.otherLocal;
    auto centreOf = [&](int local) -> double3 {
      return grid.positions[static_cast<size_t>(skTripleGet(ring.siteIndices, local))] +
             unitCell * double3(skTripleGet(ring.siteImages, local));
    };
    const double3 thirdCentre = centreOf(thirdLocal);
    const double thirdRadius = radii[static_cast<size_t>(skTripleGet(ring.siteIndices, thirdLocal))];

    const double margin = std::max(1.0e-6, 1.0e-3 * (top - ring.bottleneckRadius));
    for (double clearance : {ring.bottleneckRadius - margin, top + margin})
    {
      const auto point = SKApolloniusAlgebra::bisectorSheetPoint(
          centreOf(patchEdge.ownerLocal),
          radii[static_cast<size_t>(skTripleGet(ring.siteIndices, patchEdge.ownerLocal))],
          centreOf(patchEdge.otherLocal),
          radii[static_cast<size_t>(skTripleGet(ring.siteIndices, patchEdge.otherLocal))], clearance, 0.0);
      if (!point)
        continue;
      if ((*point - thirdCentre).length() - thirdRadius > clearance)
        return false;
    }
    return true;
  };

  for (const PrimitivePatch &patch : patches)
  {
    std::unordered_map<int, int> parent;
    for (const PatchEdge &patchEdge : patch.edges)
    {
      if (diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].isLoop)
        continue;
      const int from = diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].from;
      const int to = diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].to;
      if (parent.find(from) == parent.end())
        parent[from] = from;
      if (parent.find(to) == parent.end())
        parent[to] = to;
    }
    for (const PatchEdge &patchEdge : patch.edges)
    {
      if (diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].isLoop)
        continue;
      const int left = skFindMapParent(parent, diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].from);
      const int right = skFindMapParent(parent, diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].to);
      if (left != right)
        parent[left] = right;
    }

    std::unordered_map<int, int> componentFace;
    for (const PatchEdge &patchEdge : patch.edges)
    {
      if (diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].isLoop)
        continue;
      const int root = skFindMapParent(parent, diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].from);
      const auto existing = componentFace.find(root);
      if (existing != componentFace.end())
      {
        diagram.faces[static_cast<size_t>(existing->second)].edgeIndices.push_back(patchEdge.edgeIndex);
      }
      else
      {
        componentFace[root] = static_cast<int>(diagram.faces.size());
        diagram.faces.emplace_back(patch.owner, patch.other, patch.relativeImage,
                                   std::vector<int>{patchEdge.edgeIndex}, false);
      }
    }

    for (const PatchEdge &patchEdge : patch.edges)
    {
      if (!diagram.edges[static_cast<size_t>(patchEdge.edgeIndex)].isLoop)
        continue;

      if (!componentFace.empty() && !ringBoundsOwnFace(patchEdge))
      {
        if (componentFace.size() > 1)
          ++diagram.verification.ringsOfUncertainFace;
        int host = componentFace.begin()->second;
        int hostKey = componentFace.begin()->first;
        for (const auto &entry : componentFace)
        {
          if (entry.first < hostKey)
          {
            hostKey = entry.first;
            host = entry.second;
          }
        }
        diagram.faces[static_cast<size_t>(host)].edgeIndices.push_back(patchEdge.edgeIndex);
        continue;
      }

      diagram.faces.emplace_back(patch.owner, patch.other, patch.relativeImage,
                                 std::vector<int>{patchEdge.edgeIndex}, true);
    }
  }

  for (size_t f = 0; f < diagram.faces.size(); ++f)
  {
    std::unordered_map<int, int> incidence;
    for (int e : diagram.faces[f].edgeIndices)
    {
      if (diagram.edges[static_cast<size_t>(e)].isLoop)
        continue;
      ++incidence[diagram.edges[static_cast<size_t>(e)].from];
      ++incidence[diagram.edges[static_cast<size_t>(e)].to];
    }
    diagram.faces[f].isClosed = !diagram.faces[f].edgeIndices.empty();
    for (const auto &entry : incidence)
    {
      if (entry.second != 2)
        diagram.faces[f].isClosed = false;
    }
    if (!diagram.faces[f].isClosed)
      ++diagram.verification.unclosedFaces;
  }

  diagram.cells.clear();
  diagram.cells.reserve(static_cast<size_t>(siteCount));
  for (int i = 0; i < siteCount; ++i)
    diagram.cells.emplace_back(i, std::vector<int>{}, std::vector<int>{}, true);

  for (size_t f = 0; f < diagram.faces.size(); ++f)
  {
    diagram.cells[static_cast<size_t>(diagram.faces[f].site1)].faceIndices.push_back(static_cast<int>(f));
    diagram.cells[static_cast<size_t>(diagram.faces[f].site1)].isEmpty = false;
  }
  for (size_t v = 0; v < diagram.vertices.size(); ++v)
  {
    for (int site : diagram.vertices[v].siteIndices)
      diagram.cells[static_cast<size_t>(site)].vertexIndices.push_back(static_cast<int>(v));
  }
  for (size_t i = 0; i < diagram.cells.size(); ++i)
  {
    std::sort(diagram.cells[i].vertexIndices.begin(), diagram.cells[i].vertexIndices.end());
    std::vector<int> unique;
    for (int vertex : diagram.cells[i].vertexIndices)
    {
      if (unique.empty() || unique.back() != vertex)
        unique.push_back(vertex);
    }
    diagram.cells[i].vertexIndices = std::move(unique);
  }
}

void verifyDiagram(const double3x3 &unitCell, const double3x3 &inverseCell, const double3 &perpendicularWidths,
                   SKApolloniusDiagram &diagram)
{
  std::vector<int> valence(diagram.vertices.size(), 0);
  for (const SKApolloniusEdge &edge : diagram.edges)
  {
    if (edge.isLoop)
      continue;
    ++valence[static_cast<size_t>(edge.from)];
    ++valence[static_cast<size_t>(edge.to)];
  }
  diagram.verification.vertexCount = static_cast<int>(diagram.vertices.size());
  for (size_t v = 0; v < diagram.vertices.size(); ++v)
  {
    if (valence[v] == static_cast<int>(diagram.vertices[v].branches.size()))
      ++diagram.verification.verticesOfFullValence;
  }

  std::vector<double3> mergedPositions;
  mergedPositions.reserve(diagram.vertices.size());
  for (const SKApolloniusVertex &vertex : diagram.vertices)
    mergedPositions.push_back(vertex.position);
  const std::vector<int> coincidentWith =
      skCoincidentGroups(unitCell, inverseCell, perpendicularWidths, mergedPositions, 1.0e-6);
  for (size_t v = 0; v < coincidentWith.size(); ++v)
  {
    if (coincidentWith[v] != static_cast<int>(v))
      ++diagram.verification.coincidentVertices;
  }
}
} // namespace SKApolloniusBuild
} // namespace

SKApolloniusDiagram SKApolloniusDiagram::create(const double3x3 &unitCell,
                                                const std::vector<double3> &fractionalPositions,
                                                const std::vector<double> &radii, int neighbourRings,
                                                SKApolloniusRegion region)
{
  return SKApolloniusBuild::create(unitCell, fractionalPositions, radii, neighbourRings, region);
}

std::vector<SKApolloniusTangentSphere> skApolloniusTangentSpheres(const std::array<double3, 4> &centres,
                                                                  const std::array<double, 4> &radii,
                                                                  bool allowNegativeRadius)
{
  const std::vector<double3> centreArray = {centres[0], centres[1], centres[2], centres[3]};
  const std::vector<double> radiusArray = {radii[0], radii[1], radii[2], radii[3]};
  return SKApolloniusAlgebra::tangentSpheresOf(centreArray, radiusArray, allowNegativeRadius);
}

std::optional<SKApolloniusTangentSphere> skApolloniusTrisectorPoint(const std::array<double3, 3> &centres,
                                                                    const std::array<double, 3> &radii,
                                                                    const double3 &from, const double3 &to, double t,
                                                                    bool allowNegativeRadius)
{
  const double3 chord = to - from;
  const double chordLength = chord.length();
  if (chordLength < 1.0e-12)
    return std::nullopt;
  const double3 normal = chord / chordLength;
  const double3 planePoint = from + t * chord;

  std::array<double3, 3> rows{};
  double3 timeArray{};
  double3 constantArray{};
  const std::array<double3, 3> centreArray = centres;
  const std::array<double, 3> radiusArray = radii;
  for (int i = 0; i < 2; ++i)
  {
    SKApolloniusAlgebra::tangencyRow(centreArray[static_cast<size_t>(i + 1)], radiusArray[static_cast<size_t>(i + 1)],
                                     centreArray[0], radiusArray[0], rows[static_cast<size_t>(i)], timeArray[i],
                                     constantArray[i]);
  }
  rows[2] = normal;
  timeArray[2] = 0.0;
  constantArray[2] = double3::dot(normal, planePoint);

  const std::vector<SKApolloniusTangentSphere> solutions = SKApolloniusAlgebra::solveTangencyRows(
      rows, timeArray, constantArray, centreArray[0], radiusArray[0], allowNegativeRadius);

  std::optional<SKApolloniusTangentSphere> best;
  double bestDistance = std::numeric_limits<double>::max();
  for (const SKApolloniusTangentSphere &sphere : solutions)
  {
    bool tangent = true;
    for (int i = 0; i < 3 && tangent; ++i)
    {
      const double tangentDistance = radiusArray[static_cast<size_t>(i)] + sphere.radius;
      if (tangentDistance < 0.0 ||
          std::abs((sphere.centre - centreArray[static_cast<size_t>(i)]).length() - tangentDistance) >
              1.0e-6 * std::max(1.0, tangentDistance))
      {
        tangent = false;
      }
    }
    if (!tangent)
      continue;

    const double distance = (sphere.centre - planePoint).length();
    if (distance < bestDistance)
    {
      bestDistance = distance;
      best = sphere;
    }
  }
  return best;
}
