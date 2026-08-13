/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "proteinnucleicacidmesh.h"
#include "sknucleotidebase.h"
#include "mathkit.h"
#include <cmath>
#include <cstdint>
#include <optional>

namespace
{
  double3 safeNormalize(const double3 &vector, const double3 &fallback)
  {
    if (vector.length_squared() < 1.0e-12) return fallback;
    return double3::normalize(vector);
  }

  RKRibbonVertex makeVertex(const double3 &position, const double3 &normal, int residuePickIndex, float structureType = 0.0f)
  {
    RKRibbonVertex vertex;
    vertex.position = float4(static_cast<float>(position.x),
                             static_cast<float>(position.y),
                             static_cast<float>(position.z),
                             1.0f);
    vertex.normal = float4(static_cast<float>(normal.x),
                           static_cast<float>(normal.y),
                           static_cast<float>(normal.z),
                           0.0f);
    vertex.st = float2(0.5f, 0.5f);
    vertex.pad = float2(structureType, static_cast<float>(residuePickIndex));
    vertex.stripeST = float2(0.0f, 0.0f);
    return vertex;
  }

  void pushTriangle(std::vector<uint32_t> &indices, uint32_t a, uint32_t b, uint32_t c)
  {
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
  }

  std::optional<std::pair<double3, double3>> ringPlane(const std::vector<double3> &ringPoints)
  {
    if (ringPoints.size() < 3) return std::nullopt;
    double3 center(0.0, 0.0, 0.0);
    for (const double3 &point : ringPoints) center += point;
    center = center / static_cast<double>(ringPoints.size());

    double3 accumulatedNormal(0.0, 0.0, 0.0);
    for (size_t index = 0; index < ringPoints.size(); ++index)
    {
      const double3 v0 = ringPoints[index] - center;
      const double3 v1 = ringPoints[(index + 1) % ringPoints.size()] - center;
      accumulatedNormal += double3::cross(v0, v1);
    }
    const double3 normal = safeNormalize(accumulatedNormal, double3(0.0, 0.0, 1.0));
    return std::make_pair(center, normal);
  }

  void appendFilledRingPlane(std::vector<RKRibbonVertex> &vertices,
                             std::vector<uint32_t> &indices,
                             const std::vector<double3> &ringPoints,
                             double halfThickness,
                             int residuePickIndex,
                             float structureType)
  {
    const std::optional<std::pair<double3, double3>> plane = ringPlane(ringPoints);
    if (!plane.has_value()) return;
    const double3 center = plane->first;
    const double3 normal = plane->second;
    const double3 topOffset = normal * halfThickness;
    const double3 bottomOffset = normal * -halfThickness;
    const double3 downNormal = normal * -1.0;

    const size_t count = ringPoints.size();
    const uint32_t topCenterIndex = static_cast<uint32_t>(vertices.size());
    vertices.push_back(makeVertex(center + topOffset, normal, residuePickIndex, structureType));
    const uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size());
    vertices.push_back(makeVertex(center + bottomOffset, downNormal, residuePickIndex, structureType));

    std::vector<uint32_t> topRim;
    std::vector<uint32_t> bottomRim;
    topRim.reserve(count);
    bottomRim.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
      topRim.push_back(static_cast<uint32_t>(vertices.size()));
      vertices.push_back(makeVertex(ringPoints[index] + topOffset, normal, residuePickIndex, structureType));
      bottomRim.push_back(static_cast<uint32_t>(vertices.size()));
      vertices.push_back(makeVertex(ringPoints[index] + bottomOffset, downNormal, residuePickIndex, structureType));
    }

    for (size_t index = 0; index < count; ++index)
    {
      const size_t next = (index + 1) % count;
      pushTriangle(indices, topCenterIndex, topRim[index], topRim[next]);
      pushTriangle(indices, bottomCenterIndex, bottomRim[next], bottomRim[index]);

      const double3 sideNormal = safeNormalize(double3::cross((ringPoints[next] + topOffset) - (ringPoints[index] + topOffset), normal),
                                               normal);
      const uint32_t side0 = static_cast<uint32_t>(vertices.size());
      vertices.push_back(makeVertex(ringPoints[index] + topOffset, sideNormal, residuePickIndex, structureType));
      const uint32_t side1 = static_cast<uint32_t>(vertices.size());
      vertices.push_back(makeVertex(ringPoints[next] + topOffset, sideNormal, residuePickIndex, structureType));
      const uint32_t side2 = static_cast<uint32_t>(vertices.size());
      vertices.push_back(makeVertex(ringPoints[index] + bottomOffset, sideNormal, residuePickIndex, structureType));
      const uint32_t side3 = static_cast<uint32_t>(vertices.size());
      vertices.push_back(makeVertex(ringPoints[next] + bottomOffset, sideNormal, residuePickIndex, structureType));
      pushTriangle(indices, side0, side1, side3);
      pushTriangle(indices, side0, side3, side2);
    }
  }

  void appendCylinder(std::vector<RKRibbonVertex> &vertices,
                      std::vector<uint32_t> &indices,
                      const double3 &start,
                      const double3 &end,
                      double cylinderRadius,
                      int segments,
                      int residuePickIndex,
                      float structureType)
  {
    const int segmentCount = segments < 3 ? 8 : segments;
    const double3 axis = end - start;
    if (axis.length_squared() < 1.0e-12) return;
    const double3 tangent = double3::normalize(axis);
    const double3 reference = std::abs(tangent.z) < 0.9 ? double3(0.0, 0.0, 1.0) : double3(0.0, 1.0, 0.0);
    const double3 bitangent = safeNormalize(double3::cross(tangent, reference), double3(1.0, 0.0, 0.0));
    const double3 normalAxis = safeNormalize(double3::cross(tangent, bitangent), bitangent);

    const uint32_t startBase = static_cast<uint32_t>(vertices.size());
    for (int segment = 0; segment < segmentCount; ++segment)
    {
      const double angle = 2.0 * M_PI * static_cast<double>(segment) / static_cast<double>(segmentCount);
      const double3 radial = bitangent * std::cos(angle) + normalAxis * std::sin(angle);
      vertices.push_back(makeVertex(start + radial * cylinderRadius, radial, residuePickIndex, structureType));
    }
    const uint32_t endBase = static_cast<uint32_t>(vertices.size());
    for (int segment = 0; segment < segmentCount; ++segment)
    {
      const double angle = 2.0 * M_PI * static_cast<double>(segment) / static_cast<double>(segmentCount);
      const double3 radial = bitangent * std::cos(angle) + normalAxis * std::sin(angle);
      vertices.push_back(makeVertex(end + radial * cylinderRadius, radial, residuePickIndex, structureType));
    }

    for (int segment = 0; segment < segmentCount; ++segment)
    {
      const uint32_t next = static_cast<uint32_t>((segment + 1) % segmentCount);
      const uint32_t s0 = startBase + static_cast<uint32_t>(segment);
      const uint32_t s1 = startBase + next;
      const uint32_t e0 = endBase + static_cast<uint32_t>(segment);
      const uint32_t e1 = endBase + next;
      pushTriangle(indices, s0, s1, e1);
      pushTriangle(indices, s0, e1, e0);
    }
  }
}

void ProteinNucleicAcidMeshBuilder::appendRingAndLadderMeshes(RKRibbonMesh &mesh,
                                                              const DNANucleotideGeometry &geometry,
                                                              const std::vector<DNANucleotideBasePair> &basePairs,
                                                              double3 contentShift,
                                                              double radius,
                                                              const ProteinRibbonMeshParameters &parameters)
{
  const bool drawRings = parameters.nucleicAcidRingMode == NucleicAcidRingMode::filledPlanes;
  const bool drawLadder = parameters.nucleicAcidLadderMode == NucleicAcidLadderMode::rungs;
  if (!drawRings && !drawLadder) return;

  const double ringHalfThickness = std::max(parameters.nucleicAcidRingWidth, 0.01) * radius * 0.5;
  const double ladderRadius = std::max(parameters.nucleicAcidLadderRadius, 0.01) * radius;
  const int cylinderSegments = std::max(parameters.nucleicAcidLadderSegments, 6);

  std::vector<RKRibbonVertex> auxiliaryVertices;
  std::vector<uint32_t> auxiliaryIndices;
  auxiliaryVertices.reserve(geometry.residues.size() * 48);
  auxiliaryIndices.reserve(geometry.residues.size() * 96);

  if (drawRings)
  {
    const float backboneColor = SKNucleotideBase::vertexStructureTypeCode(SKNucleotideBaseKind::unknown, true);
    for (const DNANucleotideResidueGeometry &residue : geometry.residues)
    {
      const int pickIndex = residue.globalResidueIndex;
      const float baseColor = SKNucleotideBase::vertexStructureTypeCode(residue.baseKind, false);
      if (residue.riboseRingAtoms.size() >= 3)
      {
        appendFilledRingPlane(auxiliaryVertices,
                              auxiliaryIndices,
                              residue.riboseRingPositions(contentShift),
                              ringHalfThickness,
                              pickIndex,
                              backboneColor);
      }
      if (residue.baseRingAtoms.size() >= 3)
      {
        appendFilledRingPlane(auxiliaryVertices,
                              auxiliaryIndices,
                              residue.baseRingPositions(contentShift),
                              ringHalfThickness,
                              pickIndex,
                              baseColor);
      }
    }
  }

  if (drawLadder)
  {
    for (const DNANucleotideResidueGeometry &residue : geometry.residues)
    {
      const int pickIndex = residue.globalResidueIndex;
      const float baseColor = SKNucleotideBase::vertexStructureTypeCode(residue.baseKind, false);
      const std::optional<double3> c1 = residue.c1PrimePosition(contentShift);
      const std::optional<double3> base = residue.baseAnchorPosition(contentShift);
      if (c1.has_value() && base.has_value())
      {
        appendCylinder(auxiliaryVertices, auxiliaryIndices, *c1, *base, ladderRadius, cylinderSegments, pickIndex, baseColor);
      }

      const std::optional<double3> phosphate = residue.phosphatePosition(contentShift);
      if (phosphate.has_value() && base.has_value())
      {
        const double3 outer = *phosphate * 0.333333 + *base * 0.666667;
        appendCylinder(auxiliaryVertices, auxiliaryIndices, outer, *base, ladderRadius, cylinderSegments, pickIndex, baseColor);
      }
    }

    for (const DNANucleotideBasePair &pair : basePairs)
    {
      if (pair.residueGeometryIndexA < 0 || pair.residueGeometryIndexB < 0) continue;
      if (pair.residueGeometryIndexA >= static_cast<int>(geometry.residues.size()) ||
          pair.residueGeometryIndexB >= static_cast<int>(geometry.residues.size()))
      {
        continue;
      }
      const DNANucleotideResidueGeometry &residueA = geometry.residues[static_cast<size_t>(pair.residueGeometryIndexA)];
      const DNANucleotideResidueGeometry &residueB = geometry.residues[static_cast<size_t>(pair.residueGeometryIndexB)];
      const std::optional<double3> anchorA = residueA.baseAnchorPosition(contentShift);
      const std::optional<double3> anchorB = residueB.baseAnchorPosition(contentShift);
      if (!anchorA.has_value() || !anchorB.has_value()) continue;
      const float pairColor = SKNucleotideBase::vertexStructureTypeCode(residueA.baseKind, false);
      appendCylinder(auxiliaryVertices,
                     auxiliaryIndices,
                     *anchorA,
                     *anchorB,
                     ladderRadius,
                     cylinderSegments,
                     residueA.globalResidueIndex,
                     pairColor);
    }
  }

  if (auxiliaryIndices.empty()) return;
  const int indexStart = static_cast<int>(mesh.indices.size());
  const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.insert(mesh.vertices.end(), auxiliaryVertices.begin(), auxiliaryVertices.end());
  mesh.indices.reserve(mesh.indices.size() + auxiliaryIndices.size());
  for (const uint32_t localIndex : auxiliaryIndices)
  {
    mesh.indices.push_back(vertexBase + localIndex);
  }
  mesh.chainDrawRanges.emplace_back(indexStart, static_cast<int>(mesh.indices.size()) - indexStart);
}
