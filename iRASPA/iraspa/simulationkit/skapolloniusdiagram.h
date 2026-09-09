/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include <mathkit.h>

// The Apollonius diagram (additively weighted Voronoi diagram) of a periodic set of spheres.
//
// Ported from Cocoa StructureKit SKApolloniusDiagram. Space is divided by the additively weighted
// distance (clearance) d_i(x) = |x - x_i| - r_i. Vertices are centres of spheres tangent to four or
// more sites; edges and faces are curved and stored implicitly by the sites whose sheets meet.
// Candidate quadruples come from radical-diagram (SKVoronoi) adjacency and are certified by an
// exhaustive subdivision sweep.

/// One edge leaving a vertex: the triple of the vertex's sites whose trisector it runs along, named by
/// position in the vertex's site list, and the direction it sets off in.
struct SKApolloniusBranch
{
  std::array<int, 3> sites{{0, 0, 0}};
  double3 direction{};

  SKApolloniusBranch() = default;
  SKApolloniusBranch(const std::array<int, 3> &sitesValue, const double3 &directionValue)
      : sites(sitesValue), direction(directionValue)
  {
  }
};

/// A vertex of the diagram: the centre of a sphere tangent to, and outside, four or more sites.
struct SKApolloniusVertex
{
  double3 position{};
  double radius = 0.0;
  std::vector<int> siteIndices;
  std::vector<int3> siteImages;
  std::vector<SKApolloniusBranch> branches;

  SKApolloniusVertex() = default;
  SKApolloniusVertex(const double3 &positionValue, double radiusValue, const std::vector<int> &siteIndicesValue,
                     const std::vector<int3> &siteImagesValue, const std::vector<SKApolloniusBranch> &branchesValue)
      : position(positionValue), radius(radiusValue), siteIndices(siteIndicesValue), siteImages(siteImagesValue),
        branches(branchesValue)
  {
  }
};

/// An edge: the arc of the trisector curve of three sites running between two vertices (or a closed loop).
struct SKApolloniusEdge
{
  int from = 0;
  int to = 0;
  int3 toImage{};
  std::array<int, 3> siteIndices{{0, 0, 0}};
  std::array<int3, 3> siteImages{};
  double bottleneckRadius = 0.0;
  double3 bottleneckPosition{};
  double3 bottleneckDirection{};
  double length = 0.0;
  bool isLoop = false;

  SKApolloniusEdge() = default;
  SKApolloniusEdge(int fromValue, int toValue, const int3 &toImageValue, const std::array<int, 3> &siteIndicesValue,
                   const std::array<int3, 3> &siteImagesValue, double bottleneckRadiusValue,
                   const double3 &bottleneckPositionValue, const double3 &bottleneckDirectionValue, double lengthValue,
                   bool isLoopValue)
      : from(fromValue), to(toValue), toImage(toImageValue), siteIndices(siteIndicesValue), siteImages(siteImagesValue),
        bottleneckRadius(bottleneckRadiusValue), bottleneckPosition(bottleneckPositionValue),
        bottleneckDirection(bottleneckDirectionValue), length(lengthValue), isLoop(isLoopValue)
  {
  }
};

/// A face: a connected region of the clearance bisector sheet between two sites.
struct SKApolloniusFace
{
  int site1 = 0;
  int site2 = 0;
  int3 site2Image{};
  std::vector<int> edgeIndices;
  bool isClosed = false;

  SKApolloniusFace() = default;
  SKApolloniusFace(int site1Value, int site2Value, const int3 &site2ImageValue,
                   const std::vector<int> &edgeIndicesValue, bool isClosedValue)
      : site1(site1Value), site2(site2Value), site2Image(site2ImageValue), edgeIndices(edgeIndicesValue),
        isClosed(isClosedValue)
  {
  }
};

/// A cell: everything incident on one site.
struct SKApolloniusCell
{
  int siteIndex = 0;
  std::vector<int> faceIndices;
  std::vector<int> vertexIndices;
  bool isEmpty = true;

  SKApolloniusCell() = default;
  SKApolloniusCell(int siteIndexValue, const std::vector<int> &faceIndicesValue,
                   const std::vector<int> &vertexIndicesValue, bool isEmptyValue)
      : siteIndex(siteIndexValue), faceIndices(faceIndicesValue), vertexIndices(vertexIndicesValue),
        isEmpty(isEmptyValue)
  {
  }
};

/// Which part of space the diagram is built over.
enum class SKApolloniusRegion
{
  /// Only non-negative clearance: the diagram of the space a probe can actually occupy.
  FreeSpace,
  /// Every clearance, negative included: the diagram as a partition of all of space.
  EntireSpace
};

/// The spheres tangent to all four given spheres (classical Apollonius problem in 3D).
struct SKApolloniusTangentSphere
{
  double3 centre{};
  double radius = 0.0;

  SKApolloniusTangentSphere() = default;
  SKApolloniusTangentSphere(const double3 &centreValue, double radiusValue)
      : centre(centreValue), radius(radiusValue)
  {
  }
};

struct SKApolloniusDiagram
{
  /// Whether the complex came out combinatorially consistent.
  struct Verification
  {
    int vertexCount = 0;
    int verticesOfFullValence = 0;
    int unpairedTriples = 0;
    int truncatedTriples = 0;
    int overpairedTriples = 0;
    int coincidentVertices = 0;
    int unclosedFaces = 0;
    int vertexlessLoops = 0;
    int ringsOfUncertainFace = 0;
    int degenerateSweepBoxes = 0;
    int degenerateVertices = 0;
    int ambiguousBranches = 0;
    int unsampledArcs = 0;

    Verification() = default;

    /// True when the complex closes as far as the free region allows.
    bool isComplete() const
    {
      return vertexCount > 0 && coincidentVertices == 0 && ambiguousBranches == 0 && unpairedTriples == 0 &&
             overpairedTriples == 0 && unsampledArcs == 0 &&
             verticesOfFullValence + truncatedTriples >= vertexCount && unclosedFaces <= 3 * truncatedTriples;
    }
  };

  std::vector<SKApolloniusVertex> vertices;
  std::vector<SKApolloniusEdge> edges;
  std::vector<SKApolloniusFace> faces;
  std::vector<SKApolloniusCell> cells;
  Verification verification{};

  SKApolloniusDiagram() = default;
  SKApolloniusDiagram(const std::vector<SKApolloniusVertex> &verticesValue,
                      const std::vector<SKApolloniusEdge> &edgesValue,
                      const std::vector<SKApolloniusFace> &facesValue,
                      const std::vector<SKApolloniusCell> &cellsValue, const Verification &verificationValue)
      : vertices(verticesValue), edges(edgesValue), faces(facesValue), cells(cellsValue),
        verification(verificationValue)
  {
  }

  /// Radius of the largest sphere that fits anywhere (deepest vertex).
  double largestEmptySphereRadius() const
  {
    double maximum = 0.0;
    for (const SKApolloniusVertex &vertex : vertices)
      maximum = std::max(maximum, vertex.radius);
    return maximum;
  }

  double3 largestEmptySpherePosition() const
  {
    double maximum = -1.0;
    double3 position{};
    for (const SKApolloniusVertex &vertex : vertices)
    {
      if (vertex.radius > maximum)
      {
        maximum = vertex.radius;
        position = vertex.position;
      }
    }
    return position;
  }

  /// `neighbourRings` controls how far candidate site quadruples are drawn from radical-diagram
  /// adjacency: 1 uses direct neighbours, 2 also uses neighbours of neighbours.
  static SKApolloniusDiagram create(const double3x3 &unitCell, const std::vector<double3> &fractionalPositions,
                                    const std::vector<double> &radii, int neighbourRings = 1,
                                    SKApolloniusRegion region = SKApolloniusRegion::FreeSpace);
};

std::vector<SKApolloniusTangentSphere> skApolloniusTangentSpheres(
    const std::array<double3, 4> &centres, const std::array<double, 4> &radii, bool allowNegativeRadius = false);

std::optional<SKApolloniusTangentSphere> skApolloniusTrisectorPoint(const std::array<double3, 3> &centres,
                                                                    const std::array<double, 3> &radii,
                                                                    const double3 &from, const double3 &to, double t,
                                                                    bool allowNegativeRadius = false);
