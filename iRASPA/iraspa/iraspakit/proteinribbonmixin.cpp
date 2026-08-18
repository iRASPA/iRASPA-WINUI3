/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit Protein.swift ribbon state (MIT License, 2014-2022).
 ********************************************************************************************************************/

#include "proteinribbonmixin.h"
#include "proteinribbonmesh.h"
#include "proteinribbonmeshparameters.h"
#include "proteinribbonsegmentsupport.h"
#include <skasymmetricatom.h>
#include <map>

ProteinRibbonMixin::ProteinRibbonMixin() = default;

ProteinRibbonMixin::ProteinRibbonMixin(const ProteinRibbonMixin &other)
{
  cloneRibbonStateFrom(other);
}

void ProteinRibbonMixin::cloneRibbonStateFrom(const ProteinRibbonMixin &other)
{
  _backbone = other._backbone;
  _ribbonMesh = other._ribbonMesh;
  _drawRibbon = other._drawRibbon;
  _ribbonScaleFactor = other._ribbonScaleFactor;
  _ribbonColorSet = other._ribbonColorSet;
  _ribbonRepresentationStyle = other._ribbonRepresentationStyle;
  _ribbonSecondaryStructureMethod = other._ribbonSecondaryStructureMethod;
  _ribbonSplineType = other._ribbonSplineType;
  _ribbonSubdivisionsPerSegment = other._ribbonSubdivisionsPerSegment;
  _ribbonCrossSectionRingResolution = other._ribbonCrossSectionRingResolution;
  _ribbonCoilRadiusScale = other._ribbonCoilRadiusScale;
  _ribbonWidthClamp = other._ribbonWidthClamp;
  _ribbonSheetArrowLengthExtent = other._ribbonSheetArrowLengthExtent;
  _ribbonSheetArrowWingPosition = other._ribbonSheetArrowWingPosition;
  _ribbonSheetArrowPeakWidthFactor = other._ribbonSheetArrowPeakWidthFactor;
  _ribbonNormalSmoothingRadius = other._ribbonNormalSmoothingRadius;
  _ribbonHDR = other._ribbonHDR;
  _ribbonHDRExposure = other._ribbonHDRExposure;
  _ribbonHue = other._ribbonHue;
  _ribbonSaturation = other._ribbonSaturation;
  _ribbonValue = other._ribbonValue;
  _ribbonAmbientOcclusion = other._ribbonAmbientOcclusion;
  _ribbonAmbientColor = other._ribbonAmbientColor;
  _ribbonDiffuseColor = other._ribbonDiffuseColor;
  _ribbonSpecularColor = other._ribbonSpecularColor;
  _ribbonAmbientIntensity = other._ribbonAmbientIntensity;
  _ribbonDiffuseIntensity = other._ribbonDiffuseIntensity;
  _ribbonSpecularIntensity = other._ribbonSpecularIntensity;
  _ribbonShininess = other._ribbonShininess;

  // The copied mesh names ranges by tag, and this object's tree is not the one they were resolved
  // against.
  invalidateRibbonTreeNodeCache();
}

bool ProteinRibbonMixin::drawRibbon() const { return _drawRibbon; }
void ProteinRibbonMixin::setDrawRibbon(bool value) { _drawRibbon = value; }
double ProteinRibbonMixin::ribbonScaleFactor() const { return _ribbonScaleFactor; }
void ProteinRibbonMixin::setRibbonScaleFactor(double value) { _ribbonScaleFactor = value; }
ProteinRibbonColorSet ProteinRibbonMixin::ribbonColorSet() const { return _ribbonColorSet; }
void ProteinRibbonMixin::setRibbonColorSet(ProteinRibbonColorSet value) { _ribbonColorSet = value; }
ProteinRibbonRepresentationStyle ProteinRibbonMixin::ribbonRepresentationStyle() const { return _ribbonRepresentationStyle; }
void ProteinRibbonMixin::setRibbonRepresentationStyle(ProteinRibbonRepresentationStyle value) { _ribbonRepresentationStyle = value; }
ProteinRibbonSecondaryStructureMethod ProteinRibbonMixin::ribbonSecondaryStructureMethod() const { return _ribbonSecondaryStructureMethod; }
void ProteinRibbonMixin::setRibbonSecondaryStructureMethod(ProteinRibbonSecondaryStructureMethod value) { _ribbonSecondaryStructureMethod = value; }
ProteinRibbonSplineType ProteinRibbonMixin::ribbonSplineType() const { return _ribbonSplineType; }
void ProteinRibbonMixin::setRibbonSplineType(ProteinRibbonSplineType value) { _ribbonSplineType = value; }
int ProteinRibbonMixin::ribbonSubdivisionsPerSegment() const { return _ribbonSubdivisionsPerSegment; }
void ProteinRibbonMixin::setRibbonSubdivisionsPerSegment(int value) { _ribbonSubdivisionsPerSegment = value; }
int ProteinRibbonMixin::ribbonCrossSectionRingResolution() const { return _ribbonCrossSectionRingResolution; }
void ProteinRibbonMixin::setRibbonCrossSectionRingResolution(int value) { _ribbonCrossSectionRingResolution = value; }
double ProteinRibbonMixin::ribbonCoilRadiusScale() const { return _ribbonCoilRadiusScale; }
void ProteinRibbonMixin::setRibbonCoilRadiusScale(double value) { _ribbonCoilRadiusScale = value; }
double ProteinRibbonMixin::ribbonWidthClamp() const { return _ribbonWidthClamp; }
void ProteinRibbonMixin::setRibbonWidthClamp(double value) { _ribbonWidthClamp = value; }
double ProteinRibbonMixin::ribbonSheetArrowLengthExtent() const { return _ribbonSheetArrowLengthExtent; }
void ProteinRibbonMixin::setRibbonSheetArrowLengthExtent(double value) { _ribbonSheetArrowLengthExtent = value; }
double ProteinRibbonMixin::ribbonSheetArrowWingPosition() const { return _ribbonSheetArrowWingPosition; }
void ProteinRibbonMixin::setRibbonSheetArrowWingPosition(double value) { _ribbonSheetArrowWingPosition = value; }
double ProteinRibbonMixin::ribbonSheetArrowPeakWidthFactor() const { return _ribbonSheetArrowPeakWidthFactor; }
void ProteinRibbonMixin::setRibbonSheetArrowPeakWidthFactor(double value) { _ribbonSheetArrowPeakWidthFactor = value; }
int ProteinRibbonMixin::ribbonNormalSmoothingRadius() const { return _ribbonNormalSmoothingRadius; }
void ProteinRibbonMixin::setRibbonNormalSmoothingRadius(int value) { _ribbonNormalSmoothingRadius = value; }

bool ProteinRibbonMixin::ribbonHDR() const { return _ribbonHDR; }
void ProteinRibbonMixin::setRibbonHDR(bool value) { _ribbonHDR = value; }
double ProteinRibbonMixin::ribbonHDRExposure() const { return _ribbonHDRExposure; }
void ProteinRibbonMixin::setRibbonHDRExposure(double value) { _ribbonHDRExposure = value; }
double ProteinRibbonMixin::ribbonHue() const { return _ribbonHue; }
void ProteinRibbonMixin::setRibbonHue(double value) { _ribbonHue = value; }
double ProteinRibbonMixin::ribbonSaturation() const { return _ribbonSaturation; }
void ProteinRibbonMixin::setRibbonSaturation(double value) { _ribbonSaturation = value; }
double ProteinRibbonMixin::ribbonValue() const { return _ribbonValue; }
void ProteinRibbonMixin::setRibbonValue(double value) { _ribbonValue = value; }

bool ProteinRibbonMixin::ribbonAmbientOcclusion() const { return _ribbonAmbientOcclusion; }
void ProteinRibbonMixin::setRibbonAmbientOcclusion(bool value) { _ribbonAmbientOcclusion = value; }
RKColor ProteinRibbonMixin::ribbonAmbientColor() const { return _ribbonAmbientColor; }
void ProteinRibbonMixin::setRibbonAmbientColor(const RKColor &value) { _ribbonAmbientColor = value; }
RKColor ProteinRibbonMixin::ribbonDiffuseColor() const { return _ribbonDiffuseColor; }
void ProteinRibbonMixin::setRibbonDiffuseColor(const RKColor &value) { _ribbonDiffuseColor = value; }
RKColor ProteinRibbonMixin::ribbonSpecularColor() const { return _ribbonSpecularColor; }
void ProteinRibbonMixin::setRibbonSpecularColor(const RKColor &value) { _ribbonSpecularColor = value; }
double ProteinRibbonMixin::ribbonAmbientIntensity() const { return _ribbonAmbientIntensity; }
void ProteinRibbonMixin::setRibbonAmbientIntensity(double value) { _ribbonAmbientIntensity = value; }
double ProteinRibbonMixin::ribbonDiffuseIntensity() const { return _ribbonDiffuseIntensity; }
void ProteinRibbonMixin::setRibbonDiffuseIntensity(double value) { _ribbonDiffuseIntensity = value; }
double ProteinRibbonMixin::ribbonSpecularIntensity() const { return _ribbonSpecularIntensity; }
void ProteinRibbonMixin::setRibbonSpecularIntensity(double value) { _ribbonSpecularIntensity = value; }
double ProteinRibbonMixin::ribbonShininess() const { return _ribbonShininess; }
void ProteinRibbonMixin::setRibbonShininess(double value) { _ribbonShininess = value; }

void ProteinRibbonMixin::applyFancyRibbonAppearance()
{
  applyFancyRibbonAppearanceDefault(*this);
  recheckRibbonRepresentationStyle();
}

void ProteinRibbonMixin::recheckRibbonRepresentationStyle()
{
  ::recheckRibbonRepresentationStyle(*this);
}

void ProteinRibbonMixin::rebuildBackboneStructure()
{
  // Leaves only: the group nodes of the protein hierarchy carry placeholder atoms that are no part
  // of the backbone.
  std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms;
  for (const std::shared_ptr<SKAtomTreeNode> &node : ribbonAtomTreeController().flattenedLeafNodes())
  {
    if (const std::shared_ptr<SKAsymmetricAtom> atom = node->representedObject()) atoms.push_back(atom);
  }
  _backbone = ProteinBackbone::build(atoms);
}

void ProteinRibbonMixin::hideAtomsBehindRibbon(SKAtomTreeController &controller)
{
  // A file typed as a protein need not have a backbone to sweep, and hiding the atoms of one that
  // has none would leave nothing on screen at all.
  if (!_drawRibbon || _backbone.alphaCarbonResidueCount() <= 0) return;

  // Leaves only. The group placeholders carry the visibility of the ribbon itself, so hiding those
  // would hide the very thing this is clearing the way for.
  for (const std::shared_ptr<SKAtomTreeNode> &node : controller.flattenedLeafNodes())
  {
    if (const std::shared_ptr<SKAsymmetricAtom> atom = node->representedObject()) atom->setVisibility(false);
  }
}

void ProteinRibbonMixin::rebuildBackbone()
{
  if (!_drawRibbon) { return; }
  rebuildBackboneStructure();
  rebuildRibbonMesh();
}

void ProteinRibbonMixin::rebuildRibbonMesh()
{
  if (!_drawRibbon) { return; }
  const int atomCount = static_cast<int>(ribbonAtomTreeController().flattenedLeafNodes().size());
  const int residueCount = _backbone.alphaCarbonResidueCount();
  const ProteinRibbonMeshParameters meshParameters =
    ribbonMeshParameters(*this).effectiveForStructure(atomCount, residueCount);
  _ribbonMesh = ProteinRibbonMeshBuilder::build(_backbone,
                                                _ribbonScaleFactor,
                                                ribbonContentShift(),
                                                meshParameters,
                                                _ribbonSecondaryStructureMethod);
  invalidateRibbonTreeNodeCache();
}

const std::vector<RKRibbonVertex> &ProteinRibbonMixin::renderRibbonVertices() const
{
  return _ribbonMesh.vertices;
}

const std::vector<uint32_t> &ProteinRibbonMixin::renderRibbonIndices() const
{
  return _ribbonMesh.indices;
}

int ProteinRibbonMixin::ribbonNumberOfVertices() const
{
  return static_cast<int>(_ribbonMesh.vertices.size());
}

int ProteinRibbonMixin::ribbonNumberOfIndices() const
{
  return static_cast<int>(_ribbonMesh.indices.size());
}

std::vector<RKRibbonChainDrawRange> ProteinRibbonMixin::ribbonChainDrawRanges() const
{
  return _ribbonMesh.chainDrawRanges;
}

std::vector<RKRibbonChainDrawRange> ProteinRibbonMixin::ribbonSegmentDrawRanges() const
{
  return _ribbonMesh.segmentDrawRanges;
}

std::vector<RKRibbonChainDrawRange> ProteinRibbonMixin::ribbonResidueDrawRanges() const
{
  return _ribbonMesh.residueDrawRanges;
}

int ProteinRibbonMixin::ribbonNumberOfChains() const
{
  return _ribbonMesh.numberOfChains();
}

int ProteinRibbonMixin::ribbonNumberOfRings() const
{
  return _ribbonMesh.numberOfRings();
}

int ProteinRibbonMixin::ribbonMaxSplineSampleCount() const
{
  return _ribbonMesh.maxSplineSampleCount();
}

// Resolving a tag walks the tree, and the renderer asks about every range of every frame, so the
// answers are resolved once per mesh and kept. They are weak on purpose: an edit that drops a node
// leaves the range unmatched rather than keeping the node alive behind the tree's back.
void ProteinRibbonMixin::rebuildRibbonTreeNodeCache() const
{
  SKAtomTreeController &controller = const_cast<ProteinRibbonMixin *>(this)->ribbonAtomTreeController();

  _ribbonResidueNodes.clear();
  for (const std::shared_ptr<SKAtomTreeNode> &node :
       ProteinRibbonSegmentSupport::residueTreeNodesForAtomTags(controller, _ribbonMesh.residueAlphaCarbonTags))
  {
    _ribbonResidueNodes.push_back(node);
  }

  _ribbonSegmentNodes.clear();
  for (const std::shared_ptr<SKAtomTreeNode> &node :
       ProteinRibbonSegmentSupport::segmentTreeNodesForAtomTags(controller, _ribbonMesh.segmentAlphaCarbonTags))
  {
    _ribbonSegmentNodes.push_back(node);
  }
}

// Reading a node's visibility walks its group ancestors, and there is one node per draw range, so
// the whole mask and the ranges it merges down to are settled in one pass and kept until an atom's
// visibility or the shape of the tree moves the generation on.
void ProteinRibbonMixin::refreshRibbonVisibilityCache() const
{
  const int64_t generation = skAtomVisibilityGeneration();
  if (_ribbonVisibilityGeneration == generation) return;
  _ribbonVisibilityGeneration = generation;

  rebuildRibbonTreeNodeCache();

  // A range whose node is missing from the tree cannot be hidden or picked, and one unmatched range
  // is enough to make the whole level unusable: the renderer would draw it whatever the tree says.
  _ribbonUsesResidueVisibility = !_ribbonResidueNodes.empty() &&
                                 _ribbonResidueNodes.size() == _ribbonMesh.residueDrawRanges.size();
  _ribbonResidueVisibility.assign(_ribbonResidueNodes.size(), false);
  for (size_t index = 0; index < _ribbonResidueNodes.size(); ++index)
  {
    const std::shared_ptr<SKAtomTreeNode> residueNode = _ribbonResidueNodes[index].lock();
    if (!residueNode) { _ribbonUsesResidueVisibility = false; continue; }
    _ribbonResidueVisibility[index] = ProteinRibbonSegmentSupport::isRibbonResidueVisible(residueNode);
  }

  _ribbonUsesSegmentVisibility = !_ribbonSegmentNodes.empty() &&
                                 _ribbonSegmentNodes.size() == _ribbonMesh.segmentDrawRanges.size();
  _ribbonSegmentVisibility.assign(_ribbonSegmentNodes.size(), false);
  for (size_t index = 0; index < _ribbonSegmentNodes.size(); ++index)
  {
    const std::shared_ptr<SKAtomTreeNode> segmentNode = _ribbonSegmentNodes[index].lock();
    if (!segmentNode) { _ribbonUsesSegmentVisibility = false; continue; }
    _ribbonSegmentVisibility[index] = ProteinRibbonSegmentSupport::isRibbonSegmentVisible(segmentNode);
  }

  // Residue ranges win when they drive visibility, and a chain is all there is to hide by when
  // neither level lines up with the tree.
  if (_ribbonUsesResidueVisibility && !_ribbonMesh.residueDrawRanges.empty())
  {
    _ribbonEncodingDrawRanges = RKRibbonMesh::mergedVisibleDrawRanges(_ribbonMesh.residueDrawRanges,
                                                                     _ribbonResidueVisibility);
  }
  else if (_ribbonUsesSegmentVisibility && !_ribbonMesh.segmentDrawRanges.empty())
  {
    _ribbonEncodingDrawRanges = RKRibbonMesh::mergedVisibleDrawRanges(_ribbonMesh.segmentDrawRanges,
                                                                     _ribbonSegmentVisibility);
  }
  else
  {
    _ribbonEncodingDrawRanges = _ribbonMesh.chainDrawRanges;
  }
}

void ProteinRibbonMixin::invalidateRibbonTreeNodeCache()
{
  _ribbonVisibilityGeneration = -1;
  _ribbonResidueNodes.clear();
  _ribbonSegmentNodes.clear();
  _ribbonResidueVisibility.clear();
  _ribbonSegmentVisibility.clear();
  _ribbonEncodingDrawRanges.clear();
  _ribbonUsesResidueVisibility = false;
  _ribbonUsesSegmentVisibility = false;
}

std::shared_ptr<SKAtomTreeNode> ProteinRibbonMixin::ribbonResidueTreeNode(int rangeIndex) const
{
  refreshRibbonVisibilityCache();
  if (rangeIndex < 0 || rangeIndex >= static_cast<int>(_ribbonResidueNodes.size())) return nullptr;
  return _ribbonResidueNodes[static_cast<size_t>(rangeIndex)].lock();
}

std::shared_ptr<SKAtomTreeNode> ProteinRibbonMixin::ribbonSegmentTreeNode(int rangeIndex) const
{
  refreshRibbonVisibilityCache();
  if (rangeIndex < 0 || rangeIndex >= static_cast<int>(_ribbonSegmentNodes.size())) return nullptr;
  return _ribbonSegmentNodes[static_cast<size_t>(rangeIndex)].lock();
}

bool ProteinRibbonMixin::ribbonUsesResidueVisibility() const
{
  refreshRibbonVisibilityCache();
  return _ribbonUsesResidueVisibility;
}

bool ProteinRibbonMixin::ribbonUsesSegmentVisibility() const
{
  refreshRibbonVisibilityCache();
  return _ribbonUsesSegmentVisibility;
}

bool ProteinRibbonMixin::isRibbonResidueDrawRangeVisible(int rangeIndex) const
{
  refreshRibbonVisibilityCache();
  if (rangeIndex < 0 || rangeIndex >= static_cast<int>(_ribbonResidueVisibility.size())) return false;
  return _ribbonResidueVisibility[static_cast<size_t>(rangeIndex)];
}

bool ProteinRibbonMixin::isRibbonSegmentDrawRangeVisible(int rangeIndex) const
{
  refreshRibbonVisibilityCache();
  if (rangeIndex < 0 || rangeIndex >= static_cast<int>(_ribbonSegmentVisibility.size())) return false;
  return _ribbonSegmentVisibility[static_cast<size_t>(rangeIndex)];
}

const std::vector<RKRibbonChainDrawRange> &ProteinRibbonMixin::ribbonDrawRangesForEncoding() const
{
  refreshRibbonVisibilityCache();
  return _ribbonEncodingDrawRanges;
}

// Which ranges are selected is asked of the same cache the visibility and picking queries use, so a
// residue the sweep skipped shifts nothing: the range and the node are matched by the alpha carbon
// they share rather than by their position in two lists that need not have the same length.
std::set<int> ProteinRibbonMixin::renderSelectedRibbonSegmentDrawRangeIndices() const
{
  refreshRibbonVisibilityCache();
  const std::set<std::shared_ptr<SKAtomTreeNode>> selectedNodes =
    const_cast<ProteinRibbonMixin *>(this)->ribbonAtomTreeController().selectedTreeNodes();

  std::set<int> indices;
  for (size_t index = 0; index < _ribbonSegmentNodes.size(); ++index)
  {
    const std::shared_ptr<SKAtomTreeNode> segmentNode = _ribbonSegmentNodes[index].lock();
    if (!segmentNode) continue;
    // Selecting a chain selects the segments it holds, as selecting a segment selects its residues.
    const std::shared_ptr<SKAtomTreeNode> chainNode = segmentNode->parent();
    if (selectedNodes.count(segmentNode) > 0 || (chainNode && selectedNodes.count(chainNode) > 0))
    {
      indices.insert(static_cast<int>(index));
    }
  }
  return indices;
}

std::set<int> ProteinRibbonMixin::renderSelectedRibbonResidueDrawRangeIndices() const
{
  refreshRibbonVisibilityCache();
  const std::set<std::shared_ptr<SKAtomTreeNode>> selectedNodes =
    const_cast<ProteinRibbonMixin *>(this)->ribbonAtomTreeController().selectedTreeNodes();

  std::set<int> indices;
  for (size_t index = 0; index < _ribbonResidueNodes.size(); ++index)
  {
    const std::shared_ptr<SKAtomTreeNode> residueNode = _ribbonResidueNodes[index].lock();
    if (!residueNode) continue;
    if (selectedNodes.count(residueNode) > 0)
    {
      indices.insert(static_cast<int>(index));
      continue;
    }
    // Selecting an atom in the render view or in the tree lights up the residue it belongs to, which
    // is the only ribbon geometry that atom has.
    for (const std::shared_ptr<SKAtomTreeNode> &atomNode : residueNode->childNodes())
    {
      if (atomNode && selectedNodes.count(atomNode) > 0)
      {
        indices.insert(static_cast<int>(index));
        break;
      }
    }
  }
  return indices;
}

// The residues are matched to their alpha carbon through the tags the mesh recorded, the way the
// visibility and picking queries are, so a residue the sweep skipped shifts nothing.
std::set<std::shared_ptr<SKAtomTreeNode>> ProteinRibbonMixin::ribbonResidueNodesInRegion(
  const std::function<bool(double3)> &filter) const
{
  refreshRibbonVisibilityCache();

  std::map<int64_t, std::shared_ptr<SKAsymmetricAtom>> alphaCarbons;
  for (const ProteinBackboneChain &chain : _backbone.chains)
  {
    for (const ProteinBackboneResidue &residue : chain.residues)
    {
      if (residue.alphaCarbon) alphaCarbons[residue.alphaCarbon->tag()] = residue.alphaCarbon;
    }
  }

  const double4x4 rotationMatrix =
    double4x4::AffinityMatrixToTransformationAroundArbitraryPoint(double4x4(ribbonOrientation()),
                                                                  ribbonBoundingBoxCenter());
  const double3 contentShift = ribbonContentShift();
  const double3 origin = ribbonOrigin();

  std::set<std::shared_ptr<SKAtomTreeNode>> nodes;
  const size_t count = std::min(_ribbonResidueNodes.size(), _ribbonMesh.residueAlphaCarbonTags.size());
  for (size_t index = 0; index < count; ++index)
  {
    const std::shared_ptr<SKAtomTreeNode> residueNode = _ribbonResidueNodes[index].lock();
    if (!residueNode) continue;
    // A residue that is not drawn cannot be caught by a region drawn over the render view.
    if (!ProteinRibbonSegmentSupport::isRibbonResidueVisible(residueNode)) continue;

    const auto alphaCarbon = alphaCarbons.find(_ribbonMesh.residueAlphaCarbonTags[index]);
    if (alphaCarbon == alphaCarbons.end()) continue;

    for (const std::shared_ptr<SKAtomCopy> &copy : alphaCarbon->second->copies())
    {
      if (copy->type() != SKAtomCopy::AtomCopyType::copy) continue;
      const double3 position = copy->position() + contentShift;
      const double4 transformed = rotationMatrix * double4(position.x, position.y, position.z, 1.0);
      if (filter(double3(transformed.x, transformed.y, transformed.z) + origin))
      {
        nodes.insert(residueNode);
        break;
      }
    }
  }
  return nodes;
}

size_t ProteinRibbonMixin::selectRibbonResiduesInRegion(const std::function<bool(double3)> &filter,
                                                        bool extend)
{
  const std::set<std::shared_ptr<SKAtomTreeNode>> nodes = ribbonResidueNodesInRegion(filter);

  SKAtomTreeController &controller = ribbonAtomTreeController();
  if (!extend) controller.clearSelection();
  for (const std::shared_ptr<SKAtomTreeNode> &node : nodes)
  {
    controller.insertSelectionIndexPath(node->indexPath());
  }
  return nodes.size();
}

bool ProteinRibbonMixin::applyRibbonPick(int segmentIndex, int residueIndex, RibbonPickAction action,
                                         bool selectSegment)
{
  SKAtomTreeController &controller = ribbonAtomTreeController();
  // The indices are draw-range indices, the same ones the picking pass wrote into the vertices, so
  // they are resolved through the mesh's own record of which alpha carbon each range came from.
  const std::shared_ptr<SKAtomTreeNode> treeNode =
    selectSegment ? ribbonSegmentTreeNode(segmentIndex) : ribbonResidueTreeNode(residueIndex);
  if(!treeNode) return false;

  switch(action)
  {
  case RibbonPickAction::replaceResidue:
    controller.clearSelection();
    controller.insertSelectionIndexPath(treeNode->indexPath());
    break;

  case RibbonPickAction::toggleResidue:
  {
    const IndexPath indexPath = treeNode->indexPath();
    if(controller.selectionIndexPathSet().count(indexPath) > 0)
    {
      controller.removeSelectionIndexPath(indexPath);
    }
    else
    {
      controller.insertSelectionIndexPath(indexPath);
    }
    break;
  }

  case RibbonPickAction::toggleSecondaryStructureSegment:
  {
    if(!ProteinRibbonSegmentSupport::isSecondaryStructureSegmentNode(treeNode)) return false;

    // A segment and its residues never stay selected together: selecting the segment subsumes the
    // residues, and deselecting it clears them as well.
    const std::vector<std::shared_ptr<SKAtomTreeNode>> residueNodes =
      ProteinRibbonSegmentSupport::residueGroupNodes(treeNode);
    const bool wasSelected =
      ProteinRibbonSegmentSupport::isSecondaryStructureSegmentSelected(treeNode, controller.selectedTreeNodes());

    for(const std::shared_ptr<SKAtomTreeNode> &residueNode : residueNodes)
    {
      if(residueNode) controller.removeSelectionIndexPath(residueNode->indexPath());
    }
    if(wasSelected)
    {
      controller.removeSelectionIndexPath(treeNode->indexPath());
    }
    else
    {
      controller.insertSelectionIndexPath(treeNode->indexPath());
    }
    break;
  }
  }

  return true;
}

float3 ProteinRibbonMixin::ribbonCoilColor() const
{
  return proteinRibbonColorSetCoilColor(_ribbonColorSet);
}

float3 ProteinRibbonMixin::ribbonHelixColor() const
{
  return proteinRibbonColorSetHelixColor(_ribbonColorSet);
}

float3 ProteinRibbonMixin::ribbonSheetColor() const
{
  return proteinRibbonColorSetSheetColor(_ribbonColorSet);
}

// The wire layout follows Protein.swift, which wrote every shipped document, rather than the Qt port:
// enums travel as their display name, integers as 64-bit, and drawRibbon and ribbonScaleFactor are
// absent because Swift never persisted them.
void ProteinRibbonMixin::writeRibbonState(BinaryArchive &stream, int64_t versionNumber) const
{
  if(versionNumber >= 3)
  {
    stream << proteinRibbonColorSetDisplayName(_ribbonColorSet);
  }

  if(versionNumber >= 4)
  {
    stream << _ribbonHDR;
    stream << _ribbonHDRExposure;
    stream << _ribbonHue;
    stream << _ribbonSaturation;
    stream << _ribbonValue;
    stream << _ribbonAmbientOcclusion;
    stream << _ribbonAmbientColor;
    stream << _ribbonDiffuseColor;
    stream << _ribbonSpecularColor;
    stream << _ribbonAmbientIntensity;
    stream << _ribbonDiffuseIntensity;
    stream << _ribbonSpecularIntensity;
    stream << _ribbonShininess;
  }

  if(versionNumber >= 5)
  {
    stream << proteinRibbonSplineTypeRawValue(_ribbonSplineType);
    stream << static_cast<int64_t>(_ribbonSubdivisionsPerSegment);
    stream << static_cast<int64_t>(_ribbonCrossSectionRingResolution);
    stream << _ribbonCoilRadiusScale;
    stream << _ribbonWidthClamp;
    stream << _ribbonSheetArrowLengthExtent;
    stream << _ribbonSheetArrowWingPosition;
    stream << _ribbonSheetArrowPeakWidthFactor;
    stream << static_cast<int64_t>(_ribbonNormalSmoothingRadius);
  }

  if(versionNumber >= 6)
  {
    stream << proteinRibbonRepresentationStyleDisplayName(_ribbonRepresentationStyle);
  }

  if(versionNumber >= 7)
  {
    stream << proteinRibbonSecondaryStructureMethodDisplayName(_ribbonSecondaryStructureMethod);
  }
}

void ProteinRibbonMixin::readRibbonState(BinaryArchive &stream, int64_t versionNumber)
{
  if(versionNumber >= 3)
  {
    RKString colorSetIdentifier;
    stream >> colorSetIdentifier;
    _ribbonColorSet = proteinRibbonColorSetFromName(colorSetIdentifier);
  }

  if(versionNumber >= 4)
  {
    stream >> _ribbonHDR;
    stream >> _ribbonHDRExposure;
    stream >> _ribbonHue;
    stream >> _ribbonSaturation;
    stream >> _ribbonValue;
    stream >> _ribbonAmbientOcclusion;
    stream >> _ribbonAmbientColor;
    stream >> _ribbonDiffuseColor;
    stream >> _ribbonSpecularColor;
    stream >> _ribbonAmbientIntensity;
    stream >> _ribbonDiffuseIntensity;
    stream >> _ribbonSpecularIntensity;
    stream >> _ribbonShininess;
  }

  if(versionNumber >= 5)
  {
    RKString splineTypeIdentifier;
    stream >> splineTypeIdentifier;
    _ribbonSplineType = proteinRibbonSplineTypeFromName(splineTypeIdentifier);

    int64_t subdivisionsPerSegment = 0;
    int64_t crossSectionRingResolution = 0;
    int64_t normalSmoothingRadius = 0;

    stream >> subdivisionsPerSegment;
    stream >> crossSectionRingResolution;
    stream >> _ribbonCoilRadiusScale;
    stream >> _ribbonWidthClamp;
    stream >> _ribbonSheetArrowLengthExtent;
    stream >> _ribbonSheetArrowWingPosition;
    stream >> _ribbonSheetArrowPeakWidthFactor;
    stream >> normalSmoothingRadius;

    _ribbonSubdivisionsPerSegment = static_cast<int>(subdivisionsPerSegment);
    _ribbonCrossSectionRingResolution = static_cast<int>(crossSectionRingResolution);
    _ribbonNormalSmoothingRadius = static_cast<int>(normalSmoothingRadius);
    migrateLegacySheetArrowDefaultsIfNeeded(*this);
  }

  if(versionNumber >= 6)
  {
    RKString representationStyleIdentifier;
    stream >> representationStyleIdentifier;
    _ribbonRepresentationStyle = proteinRibbonRepresentationStyleFromName(representationStyleIdentifier);
  }
  else if(versionNumber >= 4)
  {
    // Documents predating the stored style are classified by the one setting that distinguished them.
    _ribbonRepresentationStyle = _ribbonAmbientOcclusion ? ProteinRibbonRepresentationStyle::fancy
                                                         : ProteinRibbonRepresentationStyle::defaultStyle;
  }

  if(versionNumber >= 7)
  {
    RKString secondaryStructureMethodIdentifier;
    stream >> secondaryStructureMethodIdentifier;
    _ribbonSecondaryStructureMethod = proteinRibbonSecondaryStructureMethodFromName(secondaryStructureMethodIdentifier);
  }
}
