/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skvoronoi.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{

struct SKVoronoiPolyFace
{
  std::vector<int> vertexIndices;
  bool hasSource = false;
  int neighborIndex = 0;
  int3 neighborImage{};
  double rsq = 0.0;
  double3 delta{};
};

struct SKVoronoiCandidate
{
  double rsq = 0.0;
  double3 delta{};
  int index = 0;
  int3 image{};
};

struct SKVoronoiWorkspace
{
  struct EdgeCut
  {
    int a = 0;
    int b = 0;
    int index = 0;
  };

  std::vector<double> distance;
  std::vector<int8_t> side;
  std::vector<int> clipped;
  std::vector<EdgeCut> edgeCuts;
  std::vector<std::pair<double, int>> loop;
  std::vector<int> remap;
  std::vector<double3> compactedVertices;
  std::vector<SKVoronoiCandidate> shellCandidates;
  std::vector<std::pair<double, int>> shellOrder;
};

struct SKVoronoiPolyhedron
{
  std::vector<double3> vertices;
  std::vector<SKVoronoiPolyFace> faces;

  static SKVoronoiPolyhedron makeInitialCube(double halfWidth)
  {
    SKVoronoiPolyhedron poly;
    const double h = halfWidth;
    poly.vertices = {
        double3(-h, -h, -h), double3(h, -h, -h),
        double3(h, h, -h),   double3(-h, h, -h),
        double3(-h, -h, h),  double3(h, -h, h),
        double3(h, h, h),    double3(-h, h, h)};
    poly.faces = {
        SKVoronoiPolyFace{{0, 3, 2, 1}},
        SKVoronoiPolyFace{{4, 5, 6, 7}},
        SKVoronoiPolyFace{{0, 1, 5, 4}},
        SKVoronoiPolyFace{{2, 3, 7, 6}},
        SKVoronoiPolyFace{{0, 4, 7, 3}},
        SKVoronoiPolyFace{{1, 2, 6, 5}}};
    return poly;
  }

  double maximumRadiusSquared() const
  {
    double maximum = 0.0;
    for (const double3 &x : vertices)
      maximum = std::max(maximum, x.length_squared());
    return maximum;
  }

  void compactVertices(SKVoronoiWorkspace &workspace)
  {
    workspace.remap.assign(vertices.size(), std::numeric_limits<int>::max());
    workspace.compactedVertices.clear();
    for (size_t i = 0; i < faces.size(); ++i)
    {
      for (size_t j = 0; j < faces[i].vertexIndices.size(); ++j)
      {
        const int index = faces[i].vertexIndices[j];
        if (workspace.remap[static_cast<size_t>(index)] == std::numeric_limits<int>::max())
        {
          workspace.remap[static_cast<size_t>(index)] = static_cast<int>(workspace.compactedVertices.size());
          workspace.compactedVertices.push_back(vertices[static_cast<size_t>(index)]);
        }
        faces[i].vertexIndices[j] = workspace.remap[static_cast<size_t>(index)];
      }
    }
    vertices.swap(workspace.compactedVertices);
  }

  bool cutByPlane(const double3 &n, double b, const SKVoronoiPolyFace &source, SKVoronoiWorkspace &workspace)
  {
    const double tolerance = 1.0e-10 * std::max(1.0, std::abs(b));
    const int oldVertexCount = static_cast<int>(vertices.size());

    workspace.distance.resize(vertices.size());
    workspace.side.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
      workspace.distance[i] = double3::dot(vertices[i], n) - b;
      if (workspace.distance[i] > tolerance)
        workspace.side[i] = 1;
      else if (workspace.distance[i] < -tolerance)
        workspace.side[i] = -1;
      else
        workspace.side[i] = 0;
    }

    bool anyOutside = false;
    bool anyInside = false;
    for (int8_t s : workspace.side)
    {
      if (s > 0)
        anyOutside = true;
      if (s < 0)
        anyInside = true;
    }
    if (!anyOutside)
      return false;
    if (!anyInside)
    {
      vertices.clear();
      faces.clear();
      return true;
    }

    workspace.edgeCuts.clear();
    for (size_t i = 0; i < faces.size(); ++i)
    {
      const int count = static_cast<int>(faces[i].vertexIndices.size());
      bool touched = false;
      for (int k = 0; k < count; ++k)
      {
        if (workspace.side[static_cast<size_t>(faces[i].vertexIndices[static_cast<size_t>(k)])] >= 0)
        {
          touched = true;
          break;
        }
      }
      if (!touched)
        continue;

      workspace.clipped.clear();
      for (int k = 0; k < count; ++k)
      {
        const int a = faces[i].vertexIndices[static_cast<size_t>(k)];
        const int b2 = faces[i].vertexIndices[static_cast<size_t>((k + 1) % count)];
        if (workspace.side[static_cast<size_t>(a)] <= 0)
          workspace.clipped.push_back(a);
        if (static_cast<int>(workspace.side[static_cast<size_t>(a)]) *
                static_cast<int>(workspace.side[static_cast<size_t>(b2)]) <
            0)
        {
          const int lo = std::min(a, b2);
          const int hi = std::max(a, b2);
          int newIndex = std::numeric_limits<int>::max();
          for (const SKVoronoiWorkspace::EdgeCut &cut : workspace.edgeCuts)
          {
            if (cut.a == lo && cut.b == hi)
            {
              newIndex = cut.index;
              break;
            }
          }
          if (newIndex == std::numeric_limits<int>::max())
          {
            const double t = workspace.distance[static_cast<size_t>(a)] /
                             (workspace.distance[static_cast<size_t>(a)] -
                              workspace.distance[static_cast<size_t>(b2)]);
            vertices.push_back(vertices[static_cast<size_t>(a)] +
                               t * (vertices[static_cast<size_t>(b2)] - vertices[static_cast<size_t>(a)]));
            newIndex = static_cast<int>(vertices.size()) - 1;
            workspace.edgeCuts.push_back(SKVoronoiWorkspace::EdgeCut{lo, hi, newIndex});
          }
          workspace.clipped.push_back(newIndex);
        }
      }
      faces[i].vertexIndices = workspace.clipped;
    }

    faces.erase(std::remove_if(faces.begin(), faces.end(),
                               [](const SKVoronoiPolyFace &face) { return face.vertexIndices.size() < 3; }),
                faces.end());

    workspace.loop.clear();
    for (const SKVoronoiWorkspace::EdgeCut &cut : workspace.edgeCuts)
      workspace.loop.emplace_back(0.0, cut.index);
    for (int i = 0; i < oldVertexCount; ++i)
    {
      if (workspace.side[static_cast<size_t>(i)] == 0)
        workspace.loop.emplace_back(0.0, i);
    }

    if (workspace.loop.size() >= 3)
    {
      const double3 axis = double3::normalize(n);
      const double3 helper = std::abs(axis.x) < 0.9 ? double3(1.0, 0.0, 0.0) : double3(0.0, 1.0, 0.0);
      const double3 u = double3::normalize(double3::cross(helper, axis));
      const double3 v = double3::cross(axis, u);

      double3 center{};
      for (const auto &entry : workspace.loop)
        center += vertices[static_cast<size_t>(entry.second)];
      center = center / static_cast<double>(workspace.loop.size());

      for (size_t i = 0; i < workspace.loop.size(); ++i)
      {
        const double3 p = vertices[static_cast<size_t>(workspace.loop[i].second)] - center;
        const double x = double3::dot(p, u);
        const double y = double3::dot(p, v);
        const double r = std::abs(x) + std::abs(y);
        const double q = r > 0.0 ? y / r : 0.0;
        const double angle = x >= 0.0 ? q : (q >= 0.0 ? 2.0 - q : -2.0 - q);
        workspace.loop[i].first = angle;
      }
      std::sort(workspace.loop.begin(), workspace.loop.end(),
                [](const std::pair<double, int> &lhs, const std::pair<double, int> &rhs) {
                  return lhs.first < rhs.first;
                });

      SKVoronoiPolyFace closing = source;
      closing.vertexIndices.clear();
      closing.vertexIndices.reserve(workspace.loop.size());
      for (const auto &entry : workspace.loop)
        closing.vertexIndices.push_back(entry.second);
      faces.push_back(std::move(closing));
    }

    compactVertices(workspace);
    return true;
  }
};

std::pair<int, int> binAndImage(int coordinate, int gridExtent)
{
  int image = 0;
  if (coordinate >= 0)
    image = coordinate / gridExtent;
  else
    image = -((-coordinate + gridExtent - 1) / gridExtent);
  return {coordinate - image * gridExtent, image};
}

} // namespace

SKVoronoi::SKVoronoi(const double3x3 &unitCell, const std::vector<double3> &fractionalPositions,
                     const std::vector<double> &radii)
{
  double3x3 cell = unitCell;
  const double determinant = cell.determinant();
  if (std::abs(determinant) < 1.0e-12)
    throwSKVoronoiError(SKVoronoiError::SingularUnitCell);
  if (fractionalPositions.empty())
    throwSKVoronoiError(SKVoronoiError::NoPositions);
  if (!radii.empty() && radii.size() != fractionalPositions.size())
    throwSKVoronoiError(SKVoronoiError::RadiiSizeMismatch);

  _unitCell = cell;
  _inverseUnitCell = cell.inverse();
  _cellVolume = std::abs(determinant);

  _radiiSquared.assign(fractionalPositions.size(), 0.0);
  _maximumRadiusSquared = 0.0;
  if (!radii.empty())
  {
    for (size_t i = 0; i < radii.size(); ++i)
    {
      _radiiSquared[i] = radii[i] * radii[i];
      _maximumRadiusSquared = std::max(_maximumRadiusSquared, _radiiSquared[i]);
    }
  }

  const double3 a = _unitCell[0];
  const double3 b = _unitCell[1];
  const double3 c = _unitCell[2];
  _perpendicularWidths = double3(_cellVolume / double3::cross(b, c).length(),
                                 _cellVolume / double3::cross(c, a).length(),
                                 _cellVolume / double3::cross(a, b).length());

  _positions.clear();
  _cartesianPositions.clear();
  _positions.reserve(fractionalPositions.size());
  _cartesianPositions.reserve(fractionalPositions.size());
  for (const double3 &position : fractionalPositions)
  {
    const double3 fractional(position.x - std::floor(position.x), position.y - std::floor(position.y),
                             position.z - std::floor(position.z));
    _positions.push_back(fractional);
    _cartesianPositions.push_back(_unitCell * fractional);
  }

  const double targetBinSize =
      std::cbrt(_cellVolume / std::max(1.0, static_cast<double>(_positions.size()) / 4.0));
  _gridSize = int3(std::max(1, static_cast<int>(_perpendicularWidths.x / targetBinSize)),
                   std::max(1, static_cast<int>(_perpendicularWidths.y / targetBinSize)),
                   std::max(1, static_cast<int>(_perpendicularWidths.z / targetBinSize)));
  _minimumBinWidth = std::min({_perpendicularWidths.x / static_cast<double>(_gridSize.x),
                               _perpendicularWidths.y / static_cast<double>(_gridSize.y),
                               _perpendicularWidths.z / static_cast<double>(_gridSize.z)});

  _bins.assign(static_cast<size_t>(_gridSize.x) * static_cast<size_t>(_gridSize.y) *
                   static_cast<size_t>(_gridSize.z),
               {});
  for (size_t i = 0; i < _positions.size(); ++i)
  {
    const int bx = std::min(_gridSize.x - 1, static_cast<int>(_positions[i].x * static_cast<double>(_gridSize.x)));
    const int by = std::min(_gridSize.y - 1, static_cast<int>(_positions[i].y * static_cast<double>(_gridSize.y)));
    const int bz = std::min(_gridSize.z - 1, static_cast<int>(_positions[i].z * static_cast<double>(_gridSize.z)));
    _bins[static_cast<size_t>((bz * _gridSize.y + by) * _gridSize.x + bx)].push_back(static_cast<int>(i));
  }

  double maximumBodyDiagonal = 0.0;
  for (double sx : {-1.0, 1.0})
  {
    for (double sy : {-1.0, 1.0})
      maximumBodyDiagonal = std::max(maximumBodyDiagonal, (_unitCell * double3(sx, sy, 1.0)).length());
  }
  _initialHalfWidth = 0.5 * maximumBodyDiagonal + 1.0 + std::sqrt(_maximumRadiusSquared);
}

SKVoronoiCell SKVoronoi::computeCell(int siteIndex) const
{
  const double3 site = _positions[static_cast<size_t>(siteIndex)];
  const double3 siteCartesian = _cartesianPositions[static_cast<size_t>(siteIndex)];
  const int3 siteBin(
      static_cast<int>(std::min(_gridSize.x - 1, static_cast<int>(site.x * static_cast<double>(_gridSize.x)))),
      static_cast<int>(std::min(_gridSize.y - 1, static_cast<int>(site.y * static_cast<double>(_gridSize.y)))),
      static_cast<int>(std::min(_gridSize.z - 1, static_cast<int>(site.z * static_cast<double>(_gridSize.z)))));

  SKVoronoiWorkspace workspace;
  const double siteRadiusSquared = _radiiSquared[static_cast<size_t>(siteIndex)];
  SKVoronoiPolyhedron poly = SKVoronoiPolyhedron::makeInitialCube(_initialHalfWidth);
  double maxRadiusSquared = poly.maximumRadiusSquared();

  const auto searchRadiusSquaredOf = [this, siteRadiusSquared](double circumRadiusSquared) -> double {
    const double c = std::max(0.0, _maximumRadiusSquared - siteRadiusSquared);
    const double s = std::sqrt(circumRadiusSquared) + std::sqrt(circumRadiusSquared + c);
    return s * s;
  };
  double searchRadiusSquared = searchRadiusSquaredOf(maxRadiusSquared);

  int k = 0;
  while (true)
  {
    const double lowerBound = static_cast<double>(k - 1) * _minimumBinWidth;
    if (k > 0 && lowerBound * lowerBound > searchRadiusSquared)
      break;

    workspace.shellCandidates.clear();
    for (int ox = -k; ox <= k; ++ox)
    {
      for (int oy = -k; oy <= k; ++oy)
      {
        for (int oz = -k; oz <= k; ++oz)
        {
          if (std::max({std::abs(ox), std::abs(oy), std::abs(oz)}) != k)
            continue;

          const auto xBin = binAndImage(siteBin.x + ox, _gridSize.x);
          const auto yBin = binAndImage(siteBin.y + oy, _gridSize.y);
          const auto zBin = binAndImage(siteBin.z + oz, _gridSize.z);
          const int bx = xBin.first;
          const int lx = xBin.second;
          const int by = yBin.first;
          const int ly = yBin.second;
          const int bz = zBin.first;
          const int lz = zBin.second;

          const double3 imageShift =
              _unitCell * double3(static_cast<double>(lx), static_cast<double>(ly), static_cast<double>(lz)) -
              siteCartesian;
          const size_t binIndex =
              static_cast<size_t>((bz * _gridSize.y + by) * _gridSize.x + bx);
          for (int j : _bins[binIndex])
          {
            if (j == siteIndex && lx == 0 && ly == 0 && lz == 0)
              continue;
            const double3 delta = _cartesianPositions[static_cast<size_t>(j)] + imageShift;
            const double rsq = delta.length_squared();
            if (rsq < 1.0e-16)
              throwSKVoronoiError(SKVoronoiError::CoincidingSites);
            workspace.shellCandidates.push_back(
                SKVoronoiCandidate{rsq, delta, j, int3(lx, ly, lz)});
          }
        }
      }
    }

    workspace.shellOrder.clear();
    workspace.shellOrder.reserve(workspace.shellCandidates.size());
    for (size_t i = 0; i < workspace.shellCandidates.size(); ++i)
      workspace.shellOrder.emplace_back(workspace.shellCandidates[i].rsq, static_cast<int>(i));
    std::sort(workspace.shellOrder.begin(), workspace.shellOrder.end(),
              [](const std::pair<double, int> &lhs, const std::pair<double, int> &rhs) {
                return lhs.first < rhs.first;
              });

    for (const auto &ordered : workspace.shellOrder)
    {
      const double candidateDistance = ordered.first;
      const int candidateIndex = ordered.second;
      if (candidateDistance > searchRadiusSquared)
        break;
      const SKVoronoiCandidate &candidate = workspace.shellCandidates[static_cast<size_t>(candidateIndex)];

      SKVoronoiPolyFace source;
      source.hasSource = true;
      source.neighborIndex = candidate.index;
      source.neighborImage = candidate.image;
      source.rsq = candidate.rsq;
      source.delta = candidate.delta;

      const double offset =
          0.5 * (candidate.rsq + siteRadiusSquared - _radiiSquared[static_cast<size_t>(candidate.index)]);
      if (poly.cutByPlane(candidate.delta, offset, source, workspace))
      {
        if (poly.faces.empty())
          break;
        maxRadiusSquared = poly.maximumRadiusSquared();
        searchRadiusSquared = searchRadiusSquaredOf(maxRadiusSquared);
      }
    }
    if (poly.faces.empty())
      break;
    ++k;
  }

  if (poly.vertices.empty())
  {
    return SKVoronoiCell{siteIndex, site, {}, {}, {}, 0.0, double3()};
  }

  if (std::any_of(poly.faces.begin(), poly.faces.end(),
                  [](const SKVoronoiPolyFace &face) { return !face.hasSource; }))
  {
    throwSKVoronoiError(SKVoronoiError::ConstructionFailed);
  }

  SKVoronoiCell cell;
  cell.siteIndex = siteIndex;
  cell.sitePositionFractional = site;
  cell.verticesCartesian = poly.vertices;
  cell.verticesFractional.reserve(cell.verticesCartesian.size());
  for (const double3 &x : cell.verticesCartesian)
    cell.verticesFractional.push_back(_inverseUnitCell * x);

  cell.faces.reserve(poly.faces.size());
  for (const SKVoronoiPolyFace &face : poly.faces)
  {
    SKVoronoiFace output;
    output.neighborIndex = face.neighborIndex;
    output.neighborImage = face.neighborImage;
    output.neighborDistance = std::sqrt(face.rsq);
    output.vertexIndices = face.vertexIndices;
    output.area = 0.0;
    output.normalCartesian = double3::normalize(face.delta);

    double3 areaVector{};
    const double3 p0 = cell.verticesCartesian[static_cast<size_t>(output.vertexIndices[0])];
    int vi = 1;
    while (vi + 1 < static_cast<int>(output.vertexIndices.size()))
    {
      const double3 p1 = cell.verticesCartesian[static_cast<size_t>(output.vertexIndices[static_cast<size_t>(vi)])];
      const double3 p2 =
          cell.verticesCartesian[static_cast<size_t>(output.vertexIndices[static_cast<size_t>(vi + 1)])];
      areaVector += double3::cross(p1 - p0, p2 - p0);
      ++vi;
    }
    output.area = 0.5 * areaVector.length();
    cell.faces.push_back(std::move(output));
  }

  double volume = 0.0;
  double3 centroid{};
  for (const SKVoronoiFace &face : cell.faces)
  {
    const double3 p0 = cell.verticesCartesian[static_cast<size_t>(face.vertexIndices[0])];
    int vi = 1;
    while (vi + 1 < static_cast<int>(face.vertexIndices.size()))
    {
      const double3 p1 = cell.verticesCartesian[static_cast<size_t>(face.vertexIndices[static_cast<size_t>(vi)])];
      const double3 p2 =
          cell.verticesCartesian[static_cast<size_t>(face.vertexIndices[static_cast<size_t>(vi + 1)])];
      const double tetrahedronVolume = double3::dot(p0, double3::cross(p1, p2)) / 6.0;
      volume += tetrahedronVolume;
      centroid += tetrahedronVolume * 0.25 * (p0 + p1 + p2);
      ++vi;
    }
  }
  cell.volume = volume;
  cell.centroidCartesian = volume != 0.0 ? centroid / volume : double3();
  return cell;
}

std::vector<SKVoronoiCell> SKVoronoi::computeAllCells() const
{
  std::vector<SKVoronoiCell> cells;
  cells.reserve(_positions.size());
  for (size_t i = 0; i < _positions.size(); ++i)
    cells.push_back(computeCell(static_cast<int>(i)));
  return cells;
}
