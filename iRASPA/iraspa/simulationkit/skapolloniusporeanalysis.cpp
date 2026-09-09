/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skapolloniusporeanalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "skapolloniusdiagram.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kInfinity = std::numeric_limits<double>::max();

int3 latticeAdd(const int3 &a, const int3 &b)
{
  return int3(a.x + b.x, a.y + b.y, a.z + b.z);
}

int3 latticeSub(const int3 &a, const int3 &b)
{
  return int3(a.x - b.x, a.y - b.y, a.z - b.z);
}

int3 latticeNegate(const int3 &a)
{
  return int3(-a.x, -a.y, -a.z);
}

bool latticeIsZero(const int3 &a)
{
  return a.x == 0 && a.y == 0 && a.z == 0;
}

int latticeVectorRank(const std::vector<int3> &vectors)
{
  using Vector = std::array<int64_t, 3>;
  auto cross = [](const Vector &u, const Vector &v) -> Vector {
    return {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
  };
  auto nonZero = [](const Vector &u) { return u[0] != 0 || u[1] != 0 || u[2] != 0; };

  Vector along{{0, 0, 0}};
  Vector normal{{0, 0, 0}};
  int rank = 0;
  for (const int3 &v : vectors)
  {
    const Vector row{{static_cast<int64_t>(v.x), static_cast<int64_t>(v.y), static_cast<int64_t>(v.z)}};
    if (!nonZero(row))
      continue;
    if (rank == 0)
    {
      along = row;
      rank = 1;
    }
    else if (rank == 1)
    {
      const Vector off = cross(along, row);
      if (nonZero(off))
      {
        normal = off;
        rank = 2;
      }
    }
    else if (normal[0] * row[0] + normal[1] * row[1] + normal[2] * row[2] != 0)
    {
      return 3;
    }
  }
  return rank;
}

struct SKPoreNode
{
  double3 position{};
  double3 fractional{};
  double radius = 0.0;
  double maximalRadius = 0.0;
};

struct SKPoreEdge
{
  int from = 0;
  int to = 0;
  int3 delta{};
  double radius = 0.0;
  double3 bottleneckPosition{};
  double3 bottleneckDirection{};
  bool hasBottleneckGeometry = false;
};

struct SKApolloniusPoreNetwork
{
  std::vector<SKPoreNode> nodes;
  std::vector<SKPoreEdge> edges;
  int numberOfRings = 0;
  double3x3 unitCell{};
  std::vector<double3> atomPositionsFractional;
  std::vector<double> atomRadii;

  double largestIncludedSphereDiameter() const
  {
    double maximum = 0.0;
    for (const SKPoreNode &node : nodes)
      maximum = std::max(maximum, node.maximalRadius);
    return 2.0 * maximum;
  }

  static SKApolloniusPoreNetwork create(const double3x3 &unitCell, const std::vector<double3> &fractionalPositions,
                                        const std::vector<double> &radii);
};

struct SKPercolatingPath
{
  bool percolates = false;
  double radius = 0.0;
  int limitingEdge = 0;
  std::vector<int> componentNodes;
};

struct SKPeriodicUnionFind
{
  std::vector<int> parent;
  std::vector<int3> offset;

  explicit SKPeriodicUnionFind(int count) : parent(static_cast<size_t>(count)), offset(static_cast<size_t>(count))
  {
    for (int i = 0; i < count; ++i)
      parent[static_cast<size_t>(i)] = i;
  }

  std::pair<int, int3> find(int x)
  {
    int node = x;
    int3 accumulated{};
    while (parent[static_cast<size_t>(node)] != node)
    {
      accumulated = latticeAdd(accumulated, offset[static_cast<size_t>(node)]);
      node = parent[static_cast<size_t>(node)];
    }
    return {node, accumulated};
  }
};

struct SKPoreComponent
{
  bool isChannel = false;
  int dimensionality = 0;
  std::vector<int> nodeIndices;
};

struct SKPoreWindow
{
  bool measured = false;
  bool clipped = false;
  int boundingAtoms = 0;
  double smallestFreeWidth = 0.0;
  double largestFreeWidth = 0.0;

  static SKPoreWindow measure(const SKApolloniusPoreNetwork &network, const double3 &position, const double3 &normal);
  static SKPoreWindow measureBest(const SKApolloniusPoreNetwork &network, const double3 &position, const double3 &normal,
                                  double bottleneckRadius);
  static double atlasScore(const SKPoreWindow &window);
  static SKPoreWindow freeSphere(const SKApolloniusPoreNetwork &network);
};

struct SKFrameworkRing
{
  int atomCount = 0;
  double3 centroid{};
  double3 normal{};
  double inscribedRadius = 0.0;
  double smallestFreeWidth = 0.0;
  double largestFreeWidth = 0.0;
};

struct SKLocalBond
{
  int other = 0;
  int3 delta{};
};

struct SKLocalTTEdge
{
  int to = 0;
  int3 delta{};
  int oxygen = 0;
  int3 oxygenDelta{};
};

double includedRadius(const SKApolloniusPoreNetwork &network, const std::vector<int> &nodes)
{
  double maximum = 0.0;
  for (int node : nodes)
    maximum = std::max(maximum, network.nodes[static_cast<size_t>(node)].maximalRadius);
  return maximum;
}

std::vector<SKPercolatingPath> widestIndependentPercolatingPaths(const SKApolloniusPoreNetwork &network, int maxCount)
{
  std::vector<SKPercolatingPath> paths;
  if (network.nodes.empty() || network.edges.empty() || maxCount <= 0)
    return paths;

  std::vector<int> order(network.edges.size());
  for (size_t i = 0; i < network.edges.size(); ++i)
    order[i] = static_cast<int>(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return network.edges[static_cast<size_t>(a)].radius > network.edges[static_cast<size_t>(b)].radius;
  });

  SKPeriodicUnionFind unionFind(static_cast<int>(network.nodes.size()));
  std::vector<int3> loopVectors;
  std::unordered_set<int> percolatedRoots;

  for (int index : order)
  {
    const SKPoreEdge &edge = network.edges[static_cast<size_t>(index)];
    const auto from = unionFind.find(edge.from);
    const auto to = unionFind.find(edge.to);
    const int rootFrom = from.first;
    const int rootTo = to.first;
    const int3 &accFrom = from.second;
    const int3 &accTo = to.second;

    if (rootFrom == rootTo)
    {
      const int3 net = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
      if (latticeIsZero(net))
        continue;
      std::vector<int3> trial = loopVectors;
      trial.push_back(net);
      const bool newDirection = latticeVectorRank(trial) > static_cast<int>(loopVectors.size());
      const bool newComponent = percolatedRoots.find(rootFrom) == percolatedRoots.end();
      if (!newDirection && !newComponent)
        continue;

      SKPercolatingPath path;
      path.percolates = true;
      path.radius = edge.radius;
      path.limitingEdge = index;
      for (size_t node = 0; node < network.nodes.size(); ++node)
      {
        if (unionFind.find(static_cast<int>(node)).first == rootFrom)
          path.componentNodes.push_back(static_cast<int>(node));
      }
      paths.push_back(path);
      percolatedRoots.insert(rootFrom);
      if (newDirection)
        loopVectors.push_back(net);
      if (static_cast<int>(paths.size()) == maxCount)
        break;
    }
    else
    {
      unionFind.parent[static_cast<size_t>(rootTo)] = rootFrom;
      unionFind.offset[static_cast<size_t>(rootTo)] = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
      if (percolatedRoots.count(rootFrom) || percolatedRoots.count(rootTo))
        percolatedRoots.insert(rootFrom);
    }
  }
  return paths;
}

SKPercolatingPath widestPercolatingPath(const SKApolloniusPoreNetwork &network, const std::vector<int> &nodes)
{
  SKPercolatingPath path;
  if (network.nodes.empty() || network.edges.empty() || nodes.empty())
    return path;

  std::vector<char> inSubset(network.nodes.size(), 0);
  for (int node : nodes)
    inSubset[static_cast<size_t>(node)] = 1;

  std::vector<int> order;
  order.reserve(network.edges.size());
  for (size_t i = 0; i < network.edges.size(); ++i)
  {
    const SKPoreEdge &edge = network.edges[i];
    if (inSubset[static_cast<size_t>(edge.from)] && inSubset[static_cast<size_t>(edge.to)])
      order.push_back(static_cast<int>(i));
  }
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return network.edges[static_cast<size_t>(a)].radius > network.edges[static_cast<size_t>(b)].radius;
  });

  SKPeriodicUnionFind unionFind(static_cast<int>(network.nodes.size()));
  int percolatingRoot = 0;

  for (int index : order)
  {
    const SKPoreEdge &edge = network.edges[static_cast<size_t>(index)];
    const auto from = unionFind.find(edge.from);
    const auto to = unionFind.find(edge.to);
    const int rootFrom = from.first;
    const int rootTo = to.first;
    const int3 &accFrom = from.second;
    const int3 &accTo = to.second;

    if (rootFrom == rootTo)
    {
      const int3 net = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
      if (!latticeIsZero(net))
      {
        path.percolates = true;
        path.radius = edge.radius;
        path.limitingEdge = index;
        percolatingRoot = rootFrom;
        break;
      }
    }
    else
    {
      unionFind.parent[static_cast<size_t>(rootTo)] = rootFrom;
      unionFind.offset[static_cast<size_t>(rootTo)] = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
    }
  }

  if (!path.percolates)
    return path;
  for (int node : nodes)
  {
    if (unionFind.find(node).first == percolatingRoot)
      path.componentNodes.push_back(node);
  }
  return path;
}

SKPoreDiameters computePoreDiameters(const SKApolloniusPoreNetwork &network)
{
  SKPoreDiameters diameters;
  diameters.includedSphereDiameter = network.largestIncludedSphereDiameter();
  if (network.nodes.empty() || network.edges.empty())
    return diameters;

  const std::vector<SKPercolatingPath> paths = widestIndependentPercolatingPaths(network, 2);
  if (paths.size() >= 1)
  {
    diameters.freeSphereDiameter = 2.0 * paths[0].radius;
    diameters.includedAlongFreePathDiameter = 2.0 * includedRadius(network, paths[0].componentNodes);
  }
  if (paths.size() >= 2)
  {
    diameters.freeSphereDiameter2 = 2.0 * paths[1].radius;
    diameters.includedAlongFreePathDiameter2 = 2.0 * includedRadius(network, paths[1].componentNodes);
  }
  return diameters;
}

std::vector<SKPoreComponent> poreComponents(const SKApolloniusPoreNetwork &network, double probeRadius)
{
  std::vector<SKPoreComponent> pores;
  const int numberOfNodes = static_cast<int>(network.nodes.size());
  if (numberOfNodes == 0)
    return pores;

  std::vector<char> nodeActive(static_cast<size_t>(numberOfNodes), 0);
  for (int i = 0; i < numberOfNodes; ++i)
    nodeActive[static_cast<size_t>(i)] = network.nodes[static_cast<size_t>(i)].radius > probeRadius ? 1 : 0;

  std::vector<std::vector<std::pair<int, int3>>> adjacency(static_cast<size_t>(numberOfNodes));
  for (const SKPoreEdge &edge : network.edges)
  {
    if (edge.radius <= probeRadius)
      continue;
    if (!nodeActive[static_cast<size_t>(edge.from)] || !nodeActive[static_cast<size_t>(edge.to)])
      continue;
    adjacency[static_cast<size_t>(edge.from)].emplace_back(edge.to, edge.delta);
  }

  std::vector<char> visited(static_cast<size_t>(numberOfNodes), 0);

  for (int start = 0; start < numberOfNodes; ++start)
  {
    if (!nodeActive[static_cast<size_t>(start)] || visited[static_cast<size_t>(start)])
      continue;

    std::unordered_map<int, int3> displacement;
    displacement.emplace(start, int3());
    std::vector<int> componentNodes;
    std::vector<int3> loopVectors;
    std::vector<std::pair<int, int3>> stack;
    stack.emplace_back(start, int3());
    visited[static_cast<size_t>(start)] = 1;

    while (!stack.empty())
    {
      const auto current = stack.back();
      stack.pop_back();
      const int node = current.first;
      const int3 disp = current.second;
      componentNodes.push_back(node);
      for (const auto &neighborEntry : adjacency[static_cast<size_t>(node)])
      {
        const int neighbor = neighborEntry.first;
        const int3 delta = neighborEntry.second;
        const int3 newDisp = latticeAdd(disp, delta);
        const auto previousIt = displacement.find(neighbor);
        if (previousIt != displacement.end())
        {
          const int3 loop = latticeSub(newDisp, previousIt->second);
          if (!latticeIsZero(loop))
            loopVectors.push_back(loop);
        }
        else
        {
          displacement.emplace(neighbor, newDisp);
          visited[static_cast<size_t>(neighbor)] = 1;
          stack.emplace_back(neighbor, newDisp);
        }
      }
    }

    const int dimensionality = latticeVectorRank(loopVectors);
    pores.push_back(SKPoreComponent{dimensionality > 0, dimensionality, componentNodes});
  }
  return pores;
}

SKChannelAnalysis channelAnalysisFrom(const std::vector<SKPoreComponent> &pores)
{
  SKChannelAnalysis analysis;
  for (const SKPoreComponent &pore : pores)
  {
    if (pore.isChannel)
    {
      analysis.numberOfChannels += 1;
      analysis.dimensionality = std::max(analysis.dimensionality, pore.dimensionality);
    }
    else
    {
      analysis.numberOfPockets += 1;
    }
  }
  return analysis;
}

double3 perpendicularWidths(const double3x3 &cell)
{
  const double3 a = cell[0];
  const double3 b = cell[1];
  const double3 c = cell[2];
  const double volume = std::abs(double3::dot(a, double3::cross(b, c)));
  return double3(volume / double3::cross(b, c).length(), volume / double3::cross(c, a).length(),
                 volume / double3::cross(a, b).length());
}

std::optional<double3> smallestEigenvector(double xx, double yy, double zz, double xy, double xz, double yz)
{
  double a[3][3] = {{xx, xy, xz}, {xy, yy, yz}, {xz, yz, zz}};
  double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int iter = 0; iter < 16; ++iter)
  {
    int p = 0;
    int q = 1;
    double maxOff = std::abs(a[0][1]);
    if (std::abs(a[0][2]) > maxOff)
    {
      p = 0;
      q = 2;
      maxOff = std::abs(a[0][2]);
    }
    if (std::abs(a[1][2]) > maxOff)
    {
      p = 1;
      q = 2;
      maxOff = std::abs(a[1][2]);
    }
    if (maxOff < 1.0e-14)
      break;
    const double app = a[p][p];
    const double aqq = a[q][q];
    const double apq = a[p][q];
    const double tau = (aqq - app) / (2.0 * apq);
    const double t = (tau >= 0.0 ? 1.0 : -1.0) / (std::abs(tau) + std::sqrt(1.0 + tau * tau));
    const double c = 1.0 / std::sqrt(1.0 + t * t);
    const double s = t * c;
    for (int i = 0; i < 3; ++i)
    {
      if (i == p || i == q)
        continue;
      const double aip = a[i][p];
      const double aiq = a[i][q];
      a[i][p] = c * aip - s * aiq;
      a[p][i] = a[i][p];
      a[i][q] = s * aip + c * aiq;
      a[q][i] = a[i][q];
    }
    a[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
    a[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
    a[p][q] = 0.0;
    a[q][p] = 0.0;
    for (int i = 0; i < 3; ++i)
    {
      const double vip = v[i][p];
      const double viq = v[i][q];
      v[i][p] = c * vip - s * viq;
      v[i][q] = s * vip + c * viq;
    }
  }
  int best = 0;
  if (a[1][1] < a[best][best])
    best = 1;
  if (a[2][2] < a[best][best])
    best = 2;
  const double3 normal(v[0][best], v[1][best], v[2][best]);
  const double length = normal.length();
  if (length <= 1.0e-10)
    return std::nullopt;
  return normal / length;
}

std::optional<double3> planeNormal(const std::vector<double3> &points)
{
  if (points.size() < 3)
    return std::nullopt;
  double3 centroid{};
  for (const double3 &point : points)
    centroid += point;
  centroid = centroid / static_cast<double>(points.size());
  double xx = 0.0;
  double yy = 0.0;
  double zz = 0.0;
  double xy = 0.0;
  double xz = 0.0;
  double yz = 0.0;
  for (const double3 &point : points)
  {
    const double3 d = point - centroid;
    xx += d.x * d.x;
    yy += d.y * d.y;
    zz += d.z * d.z;
    xy += d.x * d.y;
    xz += d.x * d.z;
    yz += d.y * d.z;
  }
  return smallestEigenvector(xx, yy, zz, xy, xz, yz);
}

std::vector<double3> nearbyOxygenDeltas(const SKApolloniusPoreNetwork &network, const double3 &position,
                                        double maxClearance)
{
  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);
  const double3 positionFractional = inverseCell * position;
  std::vector<double3> deltas;
  for (size_t j = 0; j < network.atomPositionsFractional.size(); ++j)
  {
    const double radius = network.atomRadii[j];
    if (radius <= 0.0)
      continue;
    const double3 relative = network.atomPositionsFractional[j] - positionFractional;
    const double3 wrapped(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                          relative.z - std::round(relative.z));
    const double3 delta = cell * wrapped;
    if (delta.length() - radius <= maxClearance)
      deltas.push_back(delta);
  }
  return deltas;
}

std::vector<double3> oxygenDeltasSorted(const SKApolloniusPoreNetwork &network, const double3 &position)
{
  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);
  const double3 positionFractional = inverseCell * position;
  std::vector<std::pair<double, double3>> nearest;
  for (size_t j = 0; j < network.atomPositionsFractional.size(); ++j)
  {
    const double radius = network.atomRadii[j];
    if (radius <= 0.0)
      continue;
    const double3 relative = network.atomPositionsFractional[j] - positionFractional;
    const double3 wrapped(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                          relative.z - std::round(relative.z));
    const double3 delta = cell * wrapped;
    nearest.emplace_back(delta.length() - radius, delta);
  }
  std::sort(nearest.begin(), nearest.end(),
            [](const std::pair<double, double3> &a, const std::pair<double, double3> &b) {
              return a.first < b.first;
            });
  std::vector<double3> result;
  result.reserve(nearest.size());
  for (const auto &entry : nearest)
    result.push_back(entry.second);
  return result;
}

std::optional<double3> ringPlaneNormal(const SKApolloniusPoreNetwork &network, const double3 &position,
                                       int atomCount = 10)
{
  const std::vector<double3> deltas = oxygenDeltasSorted(network, position);
  if (deltas.size() < 3)
    return std::nullopt;
  const size_t count = std::min(static_cast<size_t>(atomCount), deltas.size());
  return planeNormal(std::vector<double3>(deltas.begin(), deltas.begin() + static_cast<std::ptrdiff_t>(count)));
}

SKPoreWindow SKPoreWindow::measure(const SKApolloniusPoreNetwork &network, const double3 &position,
                                   const double3 &normal)
{
  SKPoreWindow window;
  const double normalLength = normal.length();
  if (normalLength <= 0.0 || network.atomPositionsFractional.empty())
    return window;
  const double3 axisNormal = normal / normalLength;

  double3 helper;
  if (std::abs(axisNormal.x) <= std::abs(axisNormal.y) && std::abs(axisNormal.x) <= std::abs(axisNormal.z))
    helper = double3(1.0, 0.0, 0.0);
  else if (std::abs(axisNormal.y) <= std::abs(axisNormal.z))
    helper = double3(0.0, 1.0, 0.0);
  else
    helper = double3(0.0, 0.0, 1.0);

  double3 firstAxis = double3::cross(axisNormal, helper);
  firstAxis = firstAxis / firstAxis.length();
  const double3 secondAxis = double3::cross(axisNormal, firstAxis);

  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);
  const std::vector<double> &radii = network.atomRadii;
  const double3 widths = perpendicularWidths(cell);
  const double maximumRadius = radii.empty() ? 0.0 : *std::max_element(radii.begin(), radii.end());
  const double3 positionFractional = inverseCell * position;

  auto forEachAtomImage = [&](double searchRadius, const auto &visit) {
    const double extent = searchRadius + maximumRadius;
    const int3 span(static_cast<int>(std::ceil(extent / widths.x)) + 1,
                    static_cast<int>(std::ceil(extent / widths.y)) + 1,
                    static_cast<int>(std::ceil(extent / widths.z)) + 1);
    for (size_t j = 0; j < network.atomPositionsFractional.size(); ++j)
    {
      const double3 relative = network.atomPositionsFractional[j] - positionFractional;
      const double3 nearest(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                            relative.z - std::round(relative.z));
      for (int ox = -span.x; ox <= span.x; ++ox)
      {
        for (int oy = -span.y; oy <= span.y; ++oy)
        {
          for (int oz = -span.z; oz <= span.z; ++oz)
          {
            const double3 delta =
                cell * double3(nearest.x + static_cast<double>(ox), nearest.y + static_cast<double>(oy),
                               nearest.z + static_cast<double>(oz));
            if (delta.length() - radii[j] <= searchRadius)
              visit(static_cast<int>(j), delta);
          }
        }
      }
    }
  };

  const double cellReach = 0.5 * std::min({widths.x, widths.y, widths.z});
  double clearance = kInfinity;
  double searchRadius = cellReach;
  while (clearance == kInfinity && searchRadius < 64.0 * cellReach)
  {
    forEachAtomImage(searchRadius, [&](int j, const double3 &delta) {
      clearance = std::min(clearance, delta.length() - radii[static_cast<size_t>(j)]);
    });
    searchRadius *= 2.0;
  }
  if (clearance == kInfinity)
    return window;
  if (clearance <= 1.0e-9)
    return window;

  const double reach = std::max(cellReach, 3.0 * clearance);

  struct WindowDisc
  {
    double first = 0.0;
    double second = 0.0;
    double radius = 0.0;
    double gap = 0.0;
  };
  std::vector<WindowDisc> discs;
  forEachAtomImage(reach, [&](int j, const double3 &delta) {
    const double standoff = double3::dot(delta, axisNormal);
    const double squared = radii[static_cast<size_t>(j)] * radii[static_cast<size_t>(j)] - standoff * standoff;
    if (squared <= 0.0)
      return;
    WindowDisc disc{double3::dot(delta, firstAxis), double3::dot(delta, secondAxis), std::sqrt(squared), 0.0};
    disc.gap = disc.first * disc.first + disc.second * disc.second - squared;
    if (disc.gap <= 0.0)
      return;
    if (std::hypot(disc.first, disc.second) - disc.radius > reach)
      return;
    discs.push_back(disc);
  });

  constexpr int directionCount = 360;
  std::vector<double> extent(directionCount, 0.0);
  std::vector<char> bounds(discs.size(), 0);
  for (int i = 0; i < directionCount; ++i)
  {
    const double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(directionCount);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    double nearest = reach;
    size_t nearestDisc = discs.size();
    for (size_t d = 0; d < discs.size(); ++d)
    {
      const WindowDisc &disc = discs[d];
      const double along = cosine * disc.first + sine * disc.second;
      if (along <= 0.0)
        continue;
      const double discriminant = along * along - disc.gap;
      if (discriminant <= 0.0)
        continue;
      const double distance = along - std::sqrt(discriminant);
      if (distance < nearest)
      {
        nearest = distance;
        nearestDisc = d;
      }
    }
    extent[static_cast<size_t>(i)] = nearest;
    if (nearestDisc < discs.size())
      bounds[nearestDisc] = 1;
    else
      window.clipped = true;
  }
  window.boundingAtoms = static_cast<int>(std::count(bounds.begin(), bounds.end(), 1));

  window.smallestFreeWidth = kInfinity;
  for (int i = 0; i < directionCount / 2; ++i)
  {
    const double width = extent[static_cast<size_t>(i)] + extent[static_cast<size_t>(i + directionCount / 2)];
    window.smallestFreeWidth = std::min(window.smallestFreeWidth, width);
    window.largestFreeWidth = std::max(window.largestFreeWidth, width);
  }
  window.measured = true;
  return window;
}

double SKPoreWindow::atlasScore(const SKPoreWindow &window)
{
  if (!window.measured)
    return 1.0e9;
  const double span = window.largestFreeWidth - window.smallestFreeWidth;
  const double aspect =
      window.smallestFreeWidth > 1.0e-6 ? window.largestFreeWidth / window.smallestFreeWidth : 10.0;
  const double ringPenalty =
      (window.boundingAtoms >= 8 && window.boundingAtoms <= 12) ? 0.0 : 2.0 + static_cast<double>(std::abs(window.boundingAtoms - 10));
  return (window.clipped ? 20.0 : 0.0) + span + 2.0 * std::max(0.0, aspect - 1.25) + ringPenalty;
}

SKPoreWindow SKPoreWindow::measureBest(const SKApolloniusPoreNetwork &network, const double3 &position,
                                       const double3 &normal, double bottleneckRadius)
{
  std::vector<SKPoreWindow> candidates;
  const SKPoreWindow alongPassage = measure(network, position, normal);
  if (alongPassage.measured)
    candidates.push_back(alongPassage);
  const std::vector<double3> ringAtoms = nearbyOxygenDeltas(network, position, bottleneckRadius + 0.85);
  if (ringAtoms.size() >= 8 && ringAtoms.size() <= 14)
  {
    if (const auto ringNormal = planeNormal(ringAtoms))
    {
      const SKPoreWindow inRing = measure(network, position, *ringNormal);
      if (inRing.measured)
        candidates.push_back(inRing);
    }
  }
  for (int count : {8, 10, 12})
  {
    if (const auto ringNormal = ringPlaneNormal(network, position, count))
    {
      const SKPoreWindow inRing = measure(network, position, *ringNormal);
      if (inRing.measured)
        candidates.push_back(inRing);
    }
  }
  if (candidates.empty())
    return alongPassage;
  return *std::min_element(candidates.begin(), candidates.end(),
                           [](const SKPoreWindow &a, const SKPoreWindow &b) { return atlasScore(a) < atlasScore(b); });
}

SKPoreWindow SKPoreWindow::freeSphere(const SKApolloniusPoreNetwork &network)
{
  std::vector<int> allNodes(network.nodes.size());
  for (size_t i = 0; i < network.nodes.size(); ++i)
    allNodes[i] = static_cast<int>(i);
  const SKPercolatingPath path = widestPercolatingPath(network, allNodes);
  if (!path.percolates || path.limitingEdge >= static_cast<int>(network.edges.size()))
    return SKPoreWindow{};
  const SKPoreEdge &edge = network.edges[static_cast<size_t>(path.limitingEdge)];
  if (!edge.hasBottleneckGeometry)
    return SKPoreWindow{};
  return measureBest(network, edge.bottleneckPosition, edge.bottleneckDirection, edge.radius);
}

double3 wrappedDelta(const double3 &delta, const double3x3 &cell, const double3x3 &inverse)
{
  const double3 fractional = inverse * delta;
  return cell * double3(fractional.x - std::round(fractional.x), fractional.y - std::round(fractional.y),
                        fractional.z - std::round(fractional.z));
}

std::optional<SKFrameworkRing> measureLocalRing(const SKApolloniusPoreNetwork &network, const std::vector<int> &oxygens,
                                                const std::vector<int3> &oxygenLattices, double inscribed,
                                                const double3x3 &cell)
{
  const int n = static_cast<int>(oxygens.size());
  if (n < 6 || static_cast<int>(oxygenLattices.size()) != n)
    return std::nullopt;
  std::vector<double3> points;
  std::vector<double> radii;
  points.reserve(static_cast<size_t>(n));
  radii.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i)
  {
    const double3 lattice(static_cast<double>(oxygenLattices[static_cast<size_t>(i)].x),
                          static_cast<double>(oxygenLattices[static_cast<size_t>(i)].y),
                          static_cast<double>(oxygenLattices[static_cast<size_t>(i)].z));
    points.push_back(cell * (network.atomPositionsFractional[static_cast<size_t>(oxygens[static_cast<size_t>(i)])] +
                             lattice));
    radii.push_back(network.atomRadii[static_cast<size_t>(oxygens[static_cast<size_t>(i)])]);
  }
  double3 centroid{};
  for (const double3 &point : points)
    centroid += point;
  centroid = centroid / static_cast<double>(n);

  double ringInscribed = kInfinity;
  for (int i = 0; i < n; ++i)
    ringInscribed = std::min(ringInscribed, (points[static_cast<size_t>(i)] - centroid).length() - radii[static_cast<size_t>(i)]);
  if (ringInscribed < 0.25 || std::abs(ringInscribed - inscribed) > 1.45)
    return std::nullopt;

  const auto normalOpt = planeNormal(points);
  if (!normalOpt)
    return std::nullopt;
  const double3 normal = *normalOpt;
  double planeError = 0.0;
  for (const double3 &point : points)
  {
    const double d = double3::dot(point - centroid, normal);
    planeError += d * d;
  }
  if (std::sqrt(planeError / static_cast<double>(n)) > 0.85)
    return std::nullopt;

  const int half = n / 2;
  std::vector<double> widths;
  for (int i = 0; i < half; ++i)
  {
    const double3 p = points[static_cast<size_t>(i)] - centroid;
    const double3 q = points[static_cast<size_t>(i + half)] - centroid;
    const double3 chord = q - p;
    const double chord2 = double3::dot(chord, chord);
    if (chord2 < 1.0)
      continue;
    const double t = -double3::dot(p, chord) / chord2;
    if (t < 0.28 || t > 0.72)
      continue;
    if ((p + t * chord).length() > 1.35)
      continue;
    const double width = std::sqrt(chord2) - radii[static_cast<size_t>(i)] - radii[static_cast<size_t>(i + half)];
    if (width > 0.5)
      widths.push_back(width);
  }
  if (static_cast<int>(widths.size()) < std::max(2, half - 1))
    return std::nullopt;
  const double smallest = *std::min_element(widths.begin(), widths.end());
  const double largest = *std::max_element(widths.begin(), widths.end());
  if (smallest < 1.0 || largest > 16.0 || largest / smallest > 2.4)
    return std::nullopt;

  return SKFrameworkRing{n, centroid, normal, ringInscribed, smallest, largest};
}

std::vector<SKFrameworkRing> localIZARings(const double3 &position, double inscribed, const double3 &passage,
                                           const SKApolloniusPoreNetwork &network)
{
  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);
  const double3 positionFractional = inverseCell * position;
  const int count = static_cast<int>(network.atomPositionsFractional.size());

  std::vector<int> nearAtoms;
  const double centreReach = inscribed + 3.6;
  for (int j = 0; j < count; ++j)
  {
    const double3 relative = network.atomPositionsFractional[static_cast<size_t>(j)] - positionFractional;
    const double3 nearest(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                          relative.z - std::round(relative.z));
    if ((cell * nearest).length() > centreReach)
      continue;
    nearAtoms.push_back(j);
  }
  if (nearAtoms.size() < 12)
    return {};

  std::unordered_map<int, std::vector<SKLocalBond>> bonds;
  for (size_t i = 0; i < nearAtoms.size(); ++i)
  {
    const int a = nearAtoms[i];
    for (size_t j = i + 1; j < nearAtoms.size(); ++j)
    {
      const int b = nearAtoms[j];
      const double3 relative =
          network.atomPositionsFractional[static_cast<size_t>(b)] - network.atomPositionsFractional[static_cast<size_t>(a)];
      const double3 nearest(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                            relative.z - std::round(relative.z));
      const double distance = (cell * nearest).length();
      if (distance < 1.30 || distance > 2.10)
        continue;
      const int3 lattice(static_cast<int>(std::lround(relative.x)), static_cast<int>(std::lround(relative.y)),
                         static_cast<int>(std::lround(relative.z)));
      bonds[a].push_back(SKLocalBond{b, lattice});
      bonds[b].push_back(SKLocalBond{a, latticeNegate(lattice)});
    }
  }

  std::vector<int> tAtoms;
  std::vector<int> oAtoms;
  for (int atom : nearAtoms)
  {
    const int coordination = bonds.count(atom) ? static_cast<int>(bonds[atom].size()) : 0;
    if (coordination == 4 || coordination == 3)
      tAtoms.push_back(atom);
    else if (coordination == 2)
      oAtoms.push_back(atom);
  }
  std::sort(tAtoms.begin(), tAtoms.end());
  if (tAtoms.size() < 6 || oAtoms.size() < 6)
    return {};

  std::vector<int> liningOxygen;
  for (int oxygen : oAtoms)
  {
    const double radius = std::max(network.atomRadii[static_cast<size_t>(oxygen)], 0.0);
    const double3 relative = network.atomPositionsFractional[static_cast<size_t>(oxygen)] - positionFractional;
    const double3 nearest(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                          relative.z - std::round(relative.z));
    const double clearance = (cell * nearest).length() - radius;
    if (clearance < inscribed - 0.45 || clearance > inscribed + 2.20)
      continue;
    liningOxygen.push_back(oxygen);
  }
  if (liningOxygen.size() < 6 || liningOxygen.size() > 28)
    return {};
  const std::unordered_set<int> oxygenSet(liningOxygen.begin(), liningOxygen.end());
  const std::unordered_set<int> tSet(tAtoms.begin(), tAtoms.end());

  std::unordered_map<int, std::vector<SKLocalTTEdge>> adjacency;
  for (int t : tAtoms)
  {
    for (const SKLocalBond &bond : bonds[t])
    {
      if (!oxygenSet.count(bond.other))
        continue;
      const int oxygen = bond.other;
      for (const SKLocalBond &second : bonds[oxygen])
      {
        if (second.other == t)
          continue;
        if (!tSet.count(second.other))
          continue;
        adjacency[t].push_back(
            SKLocalTTEdge{second.other, latticeAdd(bond.delta, second.delta), oxygen, bond.delta});
      }
    }
  }

  std::vector<SKFrameworkRing> bestByOrientation;
  std::set<std::vector<int>> seen;

  for (int start : tAtoms)
  {
    std::vector<int> pathT{start};
    std::vector<int3> pathLat{int3()};
    std::vector<int> pathO;
    std::vector<int3> pathOLat;

    std::function<void()> visit = [&]() {
      const int last = pathT.back();
      const int3 lastLat = pathLat.back();
      const int n = static_cast<int>(pathT.size());
      if (n >= 6 && n <= 14 && n % 2 == 0)
      {
        for (const SKLocalTTEdge &edge : adjacency[last])
        {
          if (edge.to != start)
            continue;
          if (!latticeIsZero(latticeAdd(lastLat, edge.delta)))
            continue;
          std::vector<int> key = pathT;
          std::sort(key.begin(), key.end());
          if (seen.count(key))
            continue;
          std::vector<int> oxygens = pathO;
          std::vector<int3> oxygenLattices = pathOLat;
          oxygens.push_back(edge.oxygen);
          oxygenLattices.push_back(latticeAdd(lastLat, edge.oxygenDelta));
          seen.insert(key);
          const auto ringOpt = measureLocalRing(network, oxygens, oxygenLattices, inscribed, cell);
          if (!ringOpt)
            continue;
          const SKFrameworkRing &ring = *ringOpt;
          const double facing = std::abs(double3::dot(ring.normal, passage));
          SKPoreWindow window;
          window.measured = true;
          window.boundingAtoms = ring.atomCount;
          window.smallestFreeWidth = ring.smallestFreeWidth;
          window.largestFreeWidth = ring.largestFreeWidth;
          const double score = SKPoreWindow::atlasScore(window) +
                               0.15 * wrappedDelta(ring.centroid - position, cell, inverseCell).length() +
                               1.5 * (1.0 - facing) + 0.55 * static_cast<double>(std::max(0, ring.atomCount - 10)) +
                               0.25 * std::abs(ring.inscribedRadius - inscribed);

          auto match = std::find_if(bestByOrientation.begin(), bestByOrientation.end(), [&](const SKFrameworkRing &existing) {
            return std::abs(double3::dot(existing.normal, ring.normal)) > 0.80;
          });
          if (match != bestByOrientation.end())
          {
            const double currentFacing = std::abs(double3::dot(match->normal, passage));
            SKPoreWindow currentWindow;
            currentWindow.measured = true;
            currentWindow.boundingAtoms = match->atomCount;
            currentWindow.smallestFreeWidth = match->smallestFreeWidth;
            currentWindow.largestFreeWidth = match->largestFreeWidth;
            const double currentScore =
                SKPoreWindow::atlasScore(currentWindow) +
                0.15 * wrappedDelta(match->centroid - position, cell, inverseCell).length() +
                1.5 * (1.0 - currentFacing) + 0.55 * static_cast<double>(std::max(0, match->atomCount - 10)) +
                0.25 * std::abs(match->inscribedRadius - inscribed);
            if (score < currentScore)
              *match = ring;
          }
          else
          {
            bestByOrientation.push_back(ring);
          }
        }
      }
      if (n == 14)
        return;
      const std::unordered_set<int> used(pathT.begin(), pathT.end());
      for (const SKLocalTTEdge &edge : adjacency[last])
      {
        if (used.count(edge.to) || edge.to < start)
          continue;
        pathT.push_back(edge.to);
        pathLat.push_back(latticeAdd(lastLat, edge.delta));
        pathO.push_back(edge.oxygen);
        pathOLat.push_back(latticeAdd(lastLat, edge.oxygenDelta));
        visit();
        pathT.pop_back();
        pathLat.pop_back();
        pathO.pop_back();
        pathOLat.pop_back();
      }
    };
    visit();
  }
  return bestByOrientation;
}

std::vector<SKFrameworkRing> frameworkWindowRings(const SKApolloniusPoreNetwork &network)
{
  std::vector<int> allNodes(network.nodes.size());
  for (size_t i = 0; i < network.nodes.size(); ++i)
    allNodes[i] = static_cast<int>(i);
  const SKPercolatingPath path = widestPercolatingPath(network, allNodes);
  if (!path.percolates || path.limitingEdge >= static_cast<int>(network.edges.size()))
    return {};
  const double freeRadius = network.edges[static_cast<size_t>(path.limitingEdge)].radius;
  const double freeDiameter = 2.0 * freeRadius;
  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);

  struct Site
  {
    double3 position{};
    double radius = 0.0;
    double3 direction{};
  };
  std::vector<Site> sites;
  for (const SKPoreEdge &edge : network.edges)
  {
    if (!edge.hasBottleneckGeometry)
      continue;
    if (edge.radius < 0.65 * freeRadius)
      continue;
    double3 passage = edge.bottleneckDirection;
    const double passageLength = passage.length();
    if (passageLength > 1.0e-12)
      passage = passage / passageLength;
    bool isDuplicate = false;
    for (const Site &existing : sites)
    {
      const double3 relative = inverseCell * (edge.bottleneckPosition - existing.position);
      const double3 wrapped =
          cell * double3(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                         relative.z - std::round(relative.z));
      if (wrapped.length() < 0.40 && std::abs(double3::dot(existing.direction, passage)) > 0.85)
      {
        isDuplicate = true;
        break;
      }
    }
    if (isDuplicate)
      continue;
    sites.push_back(Site{edge.bottleneckPosition, edge.radius, passage});
  }

  std::vector<SKFrameworkRing> rings;
  for (const Site &site : sites)
  {
    const auto local = localIZARings(site.position, site.radius, site.direction, network);
    rings.insert(rings.end(), local.begin(), local.end());
  }
  std::vector<SKFrameworkRing> filtered;
  for (const SKFrameworkRing &ring : rings)
  {
    if (ring.smallestFreeWidth >= 0.70 * freeDiameter)
      filtered.push_back(ring);
  }
  return filtered;
}

SKPoreWindow windowFromRing(const SKFrameworkRing &ring)
{
  SKPoreWindow window;
  window.measured = true;
  window.boundingAtoms = ring.atomCount;
  window.smallestFreeWidth = ring.smallestFreeWidth;
  window.largestFreeWidth = ring.largestFreeWidth;
  return window;
}

bool sameChannelAperture(const SKPoreWindow &window, const double3 &normal, const SKFrameworkRing &ring)
{
  if (std::abs(double3::dot(normal, ring.normal)) > 0.80)
    return true;
  return window.boundingAtoms == ring.atomCount && std::abs(window.smallestFreeWidth - ring.smallestFreeWidth) < 0.10 &&
         std::abs(window.largestFreeWidth - ring.largestFreeWidth) < 0.10;
}

std::vector<SKPoreWindow> directionWindows(const std::vector<SKFrameworkRing> &rings,
                                           const SKApolloniusPoreNetwork &network)
{
  const double3x3 cell = network.unitCell;
  const double3x3 inverseCell = double3x3::inverse(cell);
  std::vector<std::pair<SKPoreWindow, double3>> kept;
  std::vector<int> order(network.edges.size());
  for (size_t i = 0; i < network.edges.size(); ++i)
    order[i] = static_cast<int>(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return network.edges[static_cast<size_t>(a)].radius > network.edges[static_cast<size_t>(b)].radius;
  });
  SKPeriodicUnionFind unionFind(static_cast<int>(network.nodes.size()));
  std::vector<int3> loopVectors;

  for (int index : order)
  {
    const SKPoreEdge &edge = network.edges[static_cast<size_t>(index)];
    const auto from = unionFind.find(edge.from);
    const auto to = unionFind.find(edge.to);
    const int rootFrom = from.first;
    const int rootTo = to.first;
    const int3 &accFrom = from.second;
    const int3 &accTo = to.second;
    if (rootFrom == rootTo)
    {
      const int3 net = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
      if (latticeIsZero(net))
        continue;
      std::vector<int3> trial = loopVectors;
      trial.push_back(net);
      if (latticeVectorRank(trial) <= static_cast<int>(loopVectors.size()))
        continue;
      if (!edge.hasBottleneckGeometry)
        continue;
      double3 channel = cell * double3(static_cast<double>(net.x), static_cast<double>(net.y), static_cast<double>(net.z));
      const double channelLength = channel.length();
      if (channelLength <= 1.0e-12)
        continue;
      channel = channel / channelLength;
      const SKFrameworkRing *best = nullptr;
      double bestScore = kInfinity;
      for (const SKFrameworkRing &ring : rings)
      {
        const double align = std::abs(double3::dot(ring.normal, channel));
        const double3 relative = inverseCell * (edge.bottleneckPosition - ring.centroid);
        const double3 wrapped =
            cell * double3(relative.x - std::round(relative.x), relative.y - std::round(relative.y),
                           relative.z - std::round(relative.z));
        const double distance = wrapped.length();
        const double score = (align > 0.40 ? 0.0 : 4.0) + distance + 0.2 * static_cast<double>(std::max(0, ring.atomCount - 10));
        if (score < bestScore)
        {
          bestScore = score;
          best = &ring;
        }
      }
      if (!best || bestScore >= 8.0)
        continue;
      loopVectors.push_back(net);

      auto match = std::find_if(kept.begin(), kept.end(), [&](const std::pair<SKPoreWindow, double3> &entry) {
        return sameChannelAperture(entry.first, entry.second, *best);
      });
      if (match != kept.end())
      {
        if (best->smallestFreeWidth < match->first.smallestFreeWidth)
          *match = {windowFromRing(*best), best->normal};
        continue;
      }

      kept.emplace_back(windowFromRing(*best), best->normal);
      if (kept.size() == 3)
        break;
    }
    else
    {
      unionFind.parent[static_cast<size_t>(rootTo)] = rootFrom;
      unionFind.offset[static_cast<size_t>(rootTo)] = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
    }
  }

  std::vector<SKPoreWindow> result;
  result.reserve(kept.size());
  for (const auto &entry : kept)
    result.push_back(entry.first);
  return result;
}

std::vector<SKPoreWindow> latticeLoopWindows(const SKApolloniusPoreNetwork &network)
{
  std::vector<SKPoreWindow> windows;
  std::vector<int> order(network.edges.size());
  for (size_t i = 0; i < network.edges.size(); ++i)
    order[i] = static_cast<int>(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return network.edges[static_cast<size_t>(a)].radius > network.edges[static_cast<size_t>(b)].radius;
  });
  SKPeriodicUnionFind unionFind(static_cast<int>(network.nodes.size()));
  std::vector<int3> loopVectors;

  for (int index : order)
  {
    const SKPoreEdge &edge = network.edges[static_cast<size_t>(index)];
    const auto from = unionFind.find(edge.from);
    const auto to = unionFind.find(edge.to);
    const int rootFrom = from.first;
    const int rootTo = to.first;
    const int3 &accFrom = from.second;
    const int3 &accTo = to.second;
    if (rootFrom == rootTo)
    {
      const int3 net = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
      if (latticeIsZero(net))
        continue;
      std::vector<int3> trial = loopVectors;
      trial.push_back(net);
      if (latticeVectorRank(trial) <= static_cast<int>(loopVectors.size()))
        continue;
      if (!edge.hasBottleneckGeometry)
        continue;
      SKPoreWindow window =
          SKPoreWindow::measureBest(network, edge.bottleneckPosition, edge.bottleneckDirection, edge.radius);
      if (!window.measured)
        continue;
      window.boundingAtoms = 0;
      loopVectors.push_back(net);
      windows.push_back(window);
      if (windows.size() == 3)
        break;
    }
    else
    {
      unionFind.parent[static_cast<size_t>(rootTo)] = rootFrom;
      unionFind.offset[static_cast<size_t>(rootTo)] = latticeSub(latticeAdd(accFrom, edge.delta), accTo);
    }
  }
  if (windows.empty())
  {
    SKPoreWindow window = SKPoreWindow::freeSphere(network);
    if (window.measured)
    {
      window.boundingAtoms = 0;
      windows.push_back(window);
    }
  }
  return windows;
}

SKPoreWindowSizes computePoreWindows(const SKApolloniusPoreNetwork &network)
{
  if (network.nodes.empty() || network.edges.empty())
    return SKPoreWindowSizes();

  const std::vector<SKFrameworkRing> rings = frameworkWindowRings(network);
  std::vector<SKPoreWindow> entries;
  if (!rings.empty())
    entries = directionWindows(rings, network);
  if (entries.empty())
    entries = latticeLoopWindows(network);
  std::sort(entries.begin(), entries.end(), [](const SKPoreWindow &a, const SKPoreWindow &b) {
    return a.smallestFreeWidth < b.smallestFreeWidth;
  });

  SKPoreWindowSizes sizes;
  const size_t count = std::min<size_t>(3, entries.size());
  for (size_t i = 0; i < count; ++i)
  {
    sizes.sizes[i] = double2(entries[i].smallestFreeWidth, entries[i].largestFreeWidth);
    sizes.ringAtomCounts[i] = entries[i].boundingAtoms;
  }
  return sizes;
}

SKApolloniusPoreNetwork SKApolloniusPoreNetwork::create(const double3x3 &unitCell,
                                                        const std::vector<double3> &fractionalPositions,
                                                        const std::vector<double> &radii)
{
  SKApolloniusPoreNetwork network;
  network.unitCell = unitCell;
  network.atomPositionsFractional = fractionalPositions;
  network.atomRadii = radii;
  const double3x3 inverseCell = double3x3::inverse(unitCell);
  const SKApolloniusDiagram diagram =
      SKApolloniusDiagram::create(unitCell, fractionalPositions, radii, 1, SKApolloniusRegion::FreeSpace);

  network.nodes.reserve(diagram.vertices.size());
  for (const SKApolloniusVertex &vertex : diagram.vertices)
  {
    const double3 fractional = inverseCell * vertex.position;
    const double3 wrapped(fractional.x - std::floor(fractional.x), fractional.y - std::floor(fractional.y),
                          fractional.z - std::floor(fractional.z));
    network.nodes.push_back(SKPoreNode{vertex.position, wrapped, vertex.radius, vertex.radius});
  }

  network.edges.reserve(2 * diagram.edges.size());
  for (const SKApolloniusEdge &edge : diagram.edges)
  {
    if (edge.isLoop)
    {
      network.numberOfRings += 1;
      continue;
    }
    const double3 reverseShift =
        unitCell * double3(static_cast<double>(edge.toImage.x), static_cast<double>(edge.toImage.y),
                           static_cast<double>(edge.toImage.z));
    network.edges.push_back(SKPoreEdge{edge.from, edge.to, edge.toImage, edge.bottleneckRadius, edge.bottleneckPosition,
                                       edge.bottleneckDirection, true});
    network.edges.push_back(SKPoreEdge{edge.to, edge.from, latticeNegate(edge.toImage), edge.bottleneckRadius,
                                       edge.bottleneckPosition - reverseShift, -edge.bottleneckDirection, true});
  }
  return network;
}

std::optional<SKApolloniusPoreNetwork> poreNetwork(const SKFrameworkSnapshot &snapshot)
{
  const size_t count = std::min(snapshot.positions.size(), snapshot.potentialParameters.size());
  if (count == 0)
    return std::nullopt;

  std::vector<double3> fractionalPositions;
  std::vector<double> radii;
  fractionalPositions.reserve(count);
  radii.reserve(count);
  for (size_t i = 0; i < count; ++i)
  {
    const double3 position = snapshot.positions[i];
    fractionalPositions.push_back(double3(position.x - std::floor(position.x), position.y - std::floor(position.y),
                                          position.z - std::floor(position.z)));
    radii.push_back(SKApolloniusPoreAnalysis::atomRadius(snapshot.potentialParameters[i].y));
  }

  return SKApolloniusPoreNetwork::create(snapshot.cell.unitCell(), fractionalPositions, radii);
}

} // namespace

SKPoreWindowSizes::SKPoreWindowSizes(std::vector<double2> sizesValue, std::vector<int> ringAtomCountsValue)
{
  while (sizesValue.size() < 3)
    sizesValue.push_back(double2());
  if (sizesValue.size() > 3)
    sizesValue.resize(3);
  sizes = std::move(sizesValue);

  while (ringAtomCountsValue.size() < 3)
    ringAtomCountsValue.push_back(0);
  if (ringAtomCountsValue.size() > 3)
    ringAtomCountsValue.resize(3);
  ringAtomCounts = std::move(ringAtomCountsValue);
}

double SKApolloniusPoreAnalysis::atomRadius(double sigma)
{
  return 0.5 * sigma;
}

double SKApolloniusPoreAnalysis::probeRadius(double sigma)
{
  return 0.5 * sigma;
}

SKApolloniusPoreAnalysis SKApolloniusPoreAnalysis::compute(const SKFrameworkSnapshot &snapshot)
{
  const auto network = poreNetwork(snapshot);
  if (!network)
    return SKApolloniusPoreAnalysis();
  const std::vector<SKPoreComponent> pores =
      poreComponents(*network, probeRadius(snapshot.probeParameters.y));
  SKApolloniusPoreAnalysis analysis;
  analysis.diameters = computePoreDiameters(*network);
  analysis.channels = channelAnalysisFrom(pores);
  analysis.windows = computePoreWindows(*network);
  return analysis;
}

std::vector<double4> SKApolloniusPoreAnalysis::computeBlockingPockets(const SKFrameworkSnapshot &snapshot)
{
  const auto network = poreNetwork(snapshot);
  if (!network)
    return {};
  const std::vector<SKPoreComponent> pores =
      poreComponents(*network, probeRadius(snapshot.probeParameters.y));
  std::vector<double4> pockets;
  for (const SKPoreComponent &pore : pores)
  {
    if (pore.isChannel)
      continue;
    if (pore.nodeIndices.empty())
      continue;
    int deepest = pore.nodeIndices.front();
    for (int index : pore.nodeIndices)
    {
      if (network->nodes[static_cast<size_t>(index)].radius > network->nodes[static_cast<size_t>(deepest)].radius)
        deepest = index;
    }
    const SKPoreNode &node = network->nodes[static_cast<size_t>(deepest)];
    if (node.radius <= 0.0)
      continue;
    pockets.emplace_back(node.fractional.x, node.fractional.y, node.fractional.z, node.radius);
  }
  return pockets;
}

std::vector<std::vector<double4>>
SKApolloniusPoreAnalysis::computeBlockingPockets(const std::vector<SKFrameworkSnapshot> &structures)
{
  std::vector<std::vector<double4>> result;
  result.reserve(structures.size());
  for (const SKFrameworkSnapshot &snapshot : structures)
    result.push_back(computeBlockingPockets(snapshot));
  return result;
}

std::vector<SKApolloniusPoreAnalysis>
SKApolloniusPoreAnalysis::compute(const std::vector<SKFrameworkSnapshot> &structures)
{
  std::vector<SKApolloniusPoreAnalysis> result;
  result.reserve(structures.size());
  for (const SKFrameworkSnapshot &snapshot : structures)
    result.push_back(compute(snapshot));
  return result;
}

std::vector<SKChannelAnalysis>
SKApolloniusPoreAnalysis::computeChannels(const std::vector<SKFrameworkSnapshot> &structures)
{
  const auto analyses = compute(structures);
  std::vector<SKChannelAnalysis> result;
  result.reserve(analyses.size());
  for (const SKApolloniusPoreAnalysis &analysis : analyses)
    result.push_back(analysis.channels);
  return result;
}

std::vector<SKPoreDiameters>
SKApolloniusPoreAnalysis::computeDiameters(const std::vector<SKFrameworkSnapshot> &structures)
{
  const auto analyses = compute(structures);
  std::vector<SKPoreDiameters> result;
  result.reserve(analyses.size());
  for (const SKApolloniusPoreAnalysis &analysis : analyses)
    result.push_back(analysis.diameters);
  return result;
}

std::vector<SKPoreWindowSizes>
SKApolloniusPoreAnalysis::computeWindows(const std::vector<SKFrameworkSnapshot> &structures)
{
  const auto analyses = compute(structures);
  std::vector<SKPoreWindowSizes> result;
  result.reserve(analyses.size());
  for (const SKApolloniusPoreAnalysis &analysis : analyses)
    result.push_back(analysis.windows);
  return result;
}

int SKLatticeVectorRank(const std::vector<int3> &vectors)
{
  return latticeVectorRank(vectors);
}
