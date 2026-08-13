/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinRibbonSegmentSupport.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <set>
#include <vector>
#include <functional>
#include <memory>
#include <mathkit.h>
#include "proteinbackbone.h"
#include "proteinribbonsecondarystructure.h"
#include "proteinribbonsecondarystructuremethod.h"
#include "skatomtreecontroller.h"

struct ProteinRibbonResidueSegment
{
  char chainIdentifier = ' ';
  ProteinRibbonSecondaryStructure structureType = ProteinRibbonSecondaryStructure::coil;
  int firstResidueIndex = 0;
  int lastResidueIndex = 0;

  ProteinRibbonResidueSegment() = default;
  ProteinRibbonResidueSegment(char chainIdentifier,
                              ProteinRibbonSecondaryStructure structureType,
                              int firstResidueIndex,
                              int lastResidueIndex);
};

struct ProteinRibbonSegmentSupport
{
  static std::vector<ProteinRibbonResidueSegment> residueSegments(const ProteinBackboneChain &chain,
                                                                  double3 contentShift,
                                                                  ProteinRibbonSecondaryStructureMethod secondaryStructureMethod = ProteinRibbonSecondaryStructureMethod::stride);
  static std::vector<ProteinRibbonResidueSegment> residueSegments(const std::vector<ProteinRibbonSecondaryStructure> &assignment,
                                                                  char chainIdentifier);
  static std::vector<ProteinRibbonResidueSegment> residueSegments(const ProteinBackbone &backbone,
                                                                  double3 contentShift,
                                                                  ProteinRibbonSecondaryStructureMethod secondaryStructureMethod = ProteinRibbonSecondaryStructureMethod::stride);

  static bool isChainGroupNode(const std::shared_ptr<SKAtomTreeNode> &node);
  static bool isSecondaryStructureSegmentNode(const std::shared_ptr<SKAtomTreeNode> &node);
  static bool isHetatmGroupNode(const std::shared_ptr<SKAtomTreeNode> &node);
  static bool isResidueGroupNode(const std::shared_ptr<SKAtomTreeNode> &node);
  static bool isProteinHierarchyGroupNode(const std::shared_ptr<SKAtomTreeNode> &node);

  static std::vector<std::shared_ptr<SKAtomTreeNode>> orderedSegmentTreeNodes(SKAtomTreeController &controller);
  static std::vector<std::shared_ptr<SKAtomTreeNode>> orderedResidueTreeNodes(SKAtomTreeController &controller);

  static void setGroupVisibility(const std::shared_ptr<SKAtomTreeNode> &node, bool isVisible);
  static void setGroupRibbonVisibility(const std::shared_ptr<SKAtomTreeNode> &node, bool isVisible);
  static void setGroupAtomsVisibility(const std::shared_ptr<SKAtomTreeNode> &node, bool isVisible);
  static bool isRibbonSegmentVisible(const std::shared_ptr<SKAtomTreeNode> &node);
  static bool isRibbonResidueVisible(const std::shared_ptr<SKAtomTreeNode> &node);

  static constexpr int32_t ribbonPickObjectType = 3;

  // Draw ranges named by the alpha carbon they were swept from rather than by their position, which
  // is what the mesh records. Residues the sweep skipped, waters and lone fragments among them, leave
  // the two orders out of step, and then counting is not enough to say which node a range belongs to.
  // The result is parallel to the tags: an entry is null where the tag names nothing.
  static std::vector<std::shared_ptr<SKAtomTreeNode>> residueTreeNodesForAtomTags(SKAtomTreeController &controller,
                                                                                  const std::vector<int64_t> &alphaCarbonTags);
  static std::vector<std::shared_ptr<SKAtomTreeNode>> segmentTreeNodesForAtomTags(SKAtomTreeController &controller,
                                                                                  const std::vector<int64_t> &alphaCarbonTags);
  static std::shared_ptr<SKAtomTreeNode> enclosingSecondaryStructureSegmentNode(const std::shared_ptr<SKAtomTreeNode> &node);

  static std::set<std::shared_ptr<SKAtomTreeNode>> filterResidueTreeNodes(SKAtomTreeController &controller,
                                                                          const ProteinBackbone &backbone,
                                                                          double3 contentShift,
                                                                          simd_quatd orientation,
                                                                          double3 boundingBoxCenter,
                                                                          double3 origin,
                                                                          const std::function<bool(double3)> &filter);
  static std::set<std::shared_ptr<SKAtomTreeNode>> filterSegmentTreeNodes(SKAtomTreeController &controller,
                                                                          const ProteinBackbone &backbone,
                                                                          double3 contentShift,
                                                                          simd_quatd orientation,
                                                                          double3 boundingBoxCenter,
                                                                          double3 origin,
                                                                          const std::function<bool(double3)> &filter);

  // Selected nodes as draw-range indices, by position. Only sound while every node has a range, so
  // the selection pass should resolve them through the mesh's alpha-carbon tags the way the main and
  // picking passes do.
  static std::set<int> selectedSegmentDrawRangeIndices(SKAtomTreeController &controller);
  static std::set<int> selectedResidueDrawRangeIndices(SKAtomTreeController &controller);
  static std::vector<std::shared_ptr<SKAtomTreeNode>> residueGroupNodes(const std::shared_ptr<SKAtomTreeNode> &segmentNode);
  static bool isSecondaryStructureSegmentSelected(const std::shared_ptr<SKAtomTreeNode> &segmentNode,
                                                  const std::set<std::shared_ptr<SKAtomTreeNode>> &selectedNodes);
  static std::shared_ptr<SKAtomTreeNode> enclosingResidueGroupNode(const std::shared_ptr<SKAtomTreeNode> &leafNode);
};
