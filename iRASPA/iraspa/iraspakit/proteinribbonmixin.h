/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit Protein.swift ribbon state (MIT License, 2014-2022).
 ********************************************************************************************************************/

#pragma once

#include "rkcolor.h"
#include "binaryarchive.h"
#include <vector>
#include "ribbonstructureeditor.h"
#include "rkrenderkitprotocols.h"
#include "proteinbackbone.h"
#include "proteinribboncolorset.h"
#include "proteinribbonrepresentationstyle.h"
#include "proteinribbonsecondarystructuremethod.h"
#include "proteinribbonsplinetype.h"
#include "rkribbonmesh.h"
#include "skatomtreecontroller.h"

class ProteinRibbonMixin: public ProteinRibbonStructureEditor, public RKRenderRibbonSource
{
public:
  ProteinRibbonMixin();
  ProteinRibbonMixin(const ProteinRibbonMixin &other);

  void cloneRibbonStateFrom(const ProteinRibbonMixin &other);

  const ProteinBackbone &backbone() const { return _backbone; }

  // A protein is read to be looked at as a ribbon, and every atom drawn at once buries it. The atoms
  // are all still there carrying their own visibility; this only settles what the first look shows.
  void hideAtomsBehindRibbon(SKAtomTreeController &controller);

  // ProteinRibbonStructureEditor
  bool drawRibbon() const override;
  void setDrawRibbon(bool value) override;
  double ribbonScaleFactor() const override;
  void setRibbonScaleFactor(double value) override;
  ProteinRibbonColorSet ribbonColorSet() const override;
  void setRibbonColorSet(ProteinRibbonColorSet value) override;
  ProteinRibbonRepresentationStyle ribbonRepresentationStyle() const override;
  void setRibbonRepresentationStyle(ProteinRibbonRepresentationStyle value) override;
  ProteinRibbonSecondaryStructureMethod ribbonSecondaryStructureMethod() const override;
  void setRibbonSecondaryStructureMethod(ProteinRibbonSecondaryStructureMethod value) override;
  RKEdgeCueing ribbonEdgeCueing() const override {return _ribbonEdgeCueing;}
  void setRibbonEdgeCueing(RKEdgeCueing value) override {_ribbonEdgeCueing = value;}
  ProteinRibbonSplineType ribbonSplineType() const override;
  void setRibbonSplineType(ProteinRibbonSplineType value) override;
  int ribbonSubdivisionsPerSegment() const override;
  void setRibbonSubdivisionsPerSegment(int value) override;
  int ribbonCrossSectionRingResolution() const override;
  void setRibbonCrossSectionRingResolution(int value) override;
  double ribbonCoilRadiusScale() const override;
  void setRibbonCoilRadiusScale(double value) override;
  double ribbonWidthClamp() const override;
  void setRibbonWidthClamp(double value) override;
  double ribbonSheetArrowLengthExtent() const override;
  void setRibbonSheetArrowLengthExtent(double value) override;
  double ribbonSheetArrowWingPosition() const override;
  void setRibbonSheetArrowWingPosition(double value) override;
  double ribbonSheetArrowPeakWidthFactor() const override;
  void setRibbonSheetArrowPeakWidthFactor(double value) override;
  int ribbonNormalSmoothingRadius() const override;
  void setRibbonNormalSmoothingRadius(int value) override;

  void rebuildRibbonMesh() override;
  void rebuildBackbone() override;
  void rebuildBackboneStructure() override;

  bool ribbonHDR() const override;
  void setRibbonHDR(bool value) override;
  double ribbonHDRExposure() const override;
  void setRibbonHDRExposure(double value) override;
  double ribbonHue() const override;
  void setRibbonHue(double value) override;
  double ribbonSaturation() const override;
  void setRibbonSaturation(double value) override;
  double ribbonValue() const override;
  void setRibbonValue(double value) override;

  bool ribbonAmbientOcclusion() const override;
  void setRibbonAmbientOcclusion(bool value) override;
  RKColor ribbonAmbientColor() const override;
  void setRibbonAmbientColor(const RKColor &value) override;
  RKColor ribbonDiffuseColor() const override;
  void setRibbonDiffuseColor(const RKColor &value) override;
  RKColor ribbonSpecularColor() const override;
  void setRibbonSpecularColor(const RKColor &value) override;
  double ribbonAmbientIntensity() const override;
  void setRibbonAmbientIntensity(double value) override;
  double ribbonDiffuseIntensity() const override;
  void setRibbonDiffuseIntensity(double value) override;
  double ribbonSpecularIntensity() const override;
  void setRibbonSpecularIntensity(double value) override;
  double ribbonShininess() const override;
  void setRibbonShininess(double value) override;

  void applyFancyRibbonAppearance() override;
  void recheckRibbonRepresentationStyle() override;

  // RKRenderRibbonSource
  const std::vector<RKRibbonVertex> &renderRibbonVertices() const override;
  const std::vector<uint32_t> &renderRibbonIndices() const override;
  int ribbonNumberOfVertices() const override;
  int ribbonNumberOfIndices() const override;
  std::vector<RKRibbonChainDrawRange> ribbonChainDrawRanges() const override;
  std::vector<RKRibbonChainDrawRange> ribbonSegmentDrawRanges() const override;
  std::vector<RKRibbonChainDrawRange> ribbonResidueDrawRanges() const override;
  int ribbonNumberOfChains() const override;
  int ribbonNumberOfRings() const override;
  int ribbonMaxSplineSampleCount() const override;
  bool ribbonUsesResidueVisibility() const override;
  bool ribbonUsesSegmentVisibility() const override;
  bool isRibbonResidueDrawRangeVisible(int rangeIndex) const override;
  bool isRibbonSegmentDrawRangeVisible(int rangeIndex) const override;
  const std::vector<RKRibbonChainDrawRange> &ribbonDrawRangesForEncoding() const override;
  std::set<int> renderSelectedRibbonSegmentDrawRangeIndices() const override;
  std::set<int> renderSelectedRibbonResidueDrawRangeIndices() const override;
  float3 ribbonCoilColor() const override;
  float3 ribbonHelixColor() const override;
  float3 ribbonSheetColor() const override;

  // What a click on the ribbon does with the residue or segment it landed on.
  enum class RibbonPickAction { replaceResidue, toggleResidue, toggleSecondaryStructureSegment };

  // Turns the segment and residue index the picking pass wrote into a selected tree node. False when
  // the indices name nothing, which happens while the tree hierarchy and the mesh disagree.
  bool applyRibbonPick(int segmentIndex, int residueIndex, RibbonPickAction action, bool selectSegment);

  // A drag over the render view selects whole residues of the ribbon rather than the atoms it
  // covers: the ribbon is what is on screen, and its atoms are usually not drawn at all. Which
  // residue a region caught is decided by its alpha carbon, as a click on the ribbon is.
  std::set<std::shared_ptr<SKAtomTreeNode>> ribbonResidueNodesInRegion(
    const std::function<bool(double3)> &filter) const;
  // Returns how many residues ended up selected. Without extending, whatever the ribbon held before
  // is let go, so an empty region clears it.
  size_t selectRibbonResiduesInRegion(const std::function<bool(double3)> &filter, bool extend);

  // The tree node a draw range belongs to, or null when the tree no longer holds it.
  std::shared_ptr<SKAtomTreeNode> ribbonResidueTreeNode(int rangeIndex) const;
  std::shared_ptr<SKAtomTreeNode> ribbonSegmentTreeNode(int rangeIndex) const;
  // Call whenever the mesh is replaced: the draw ranges and tags the cache is keyed on change. An
  // edit to the atom tree needs no call, being caught by the atom visibility generation.
  void invalidateRibbonTreeNodeCache();

  void writeRibbonState(BinaryArchive &stream, int64_t versionNumber) const;
  void readRibbonState(BinaryArchive &stream, int64_t versionNumber);

protected:
  virtual SKAtomTreeController &ribbonAtomTreeController() = 0;
  virtual const SKAtomTreeController &ribbonAtomTreeController() const = 0;
  virtual double3 ribbonContentShift() const = 0;
  // Where a backbone atom of the object ends up in the world, which is what a region drawn over the
  // render view has to be compared against.
  virtual simd_quatd ribbonOrientation() const = 0;
  virtual double3 ribbonBoundingBoxCenter() const = 0;
  virtual double3 ribbonOrigin() const = 0;

  void rebuildRibbonTreeNodeCache() const;
  void refreshRibbonVisibilityCache() const;

  ProteinBackbone _backbone;
  RKRibbonMesh _ribbonMesh;

  // Which tree node each draw range belongs to, resolved from the mesh's alpha-carbon tags, whether
  // that node is currently drawn, and the ranges left to encode once the hidden ones are merged out.
  // Mutable because the renderer asks about all of this through const queries.
  mutable std::vector<std::weak_ptr<SKAtomTreeNode>> _ribbonResidueNodes;
  mutable std::vector<std::weak_ptr<SKAtomTreeNode>> _ribbonSegmentNodes;
  mutable std::vector<bool> _ribbonResidueVisibility;
  mutable std::vector<bool> _ribbonSegmentVisibility;
  mutable std::vector<RKRibbonChainDrawRange> _ribbonEncodingDrawRanges;
  mutable bool _ribbonUsesResidueVisibility = false;
  mutable bool _ribbonUsesSegmentVisibility = false;
  // The atom visibility generation the cache above was resolved at. Negative while it holds nothing,
  // which no generation ever is.
  mutable int64_t _ribbonVisibilityGeneration = -1;

  bool _drawRibbon = true;
  double _ribbonScaleFactor = 1.2;
  ProteinRibbonColorSet _ribbonColorSet = ProteinRibbonColorSet::standardAcademic;
  ProteinRibbonRepresentationStyle _ribbonRepresentationStyle = ProteinRibbonRepresentationStyle::defaultStyle;
  ProteinRibbonSecondaryStructureMethod _ribbonSecondaryStructureMethod = ProteinRibbonSecondaryStructureMethod::stride;
  // As for atoms: kept across a load and save, not yet drawn.
  RKEdgeCueing _ribbonEdgeCueing = RKEdgeCueing::off;
  ProteinRibbonSplineType _ribbonSplineType = ProteinRibbonSplineType::bSpline;
  int _ribbonSubdivisionsPerSegment = 24;
  int _ribbonCrossSectionRingResolution = 32;
  double _ribbonCoilRadiusScale = 0.35;
  double _ribbonWidthClamp = 0.125;
  double _ribbonSheetArrowLengthExtent = 1.5;
  double _ribbonSheetArrowWingPosition = 1.0;
  double _ribbonSheetArrowPeakWidthFactor = 2.5;
  int _ribbonNormalSmoothingRadius = 4;
  // Cocoa Protein defaults = Default ribbon representation style
  bool _ribbonHDR = true;
  double _ribbonHDRExposure = 1.5;
  double _ribbonHue = 1.0;
  double _ribbonSaturation = 1.0;
  double _ribbonValue = 1.0;
  bool _ribbonAmbientOcclusion = false;
  RKColor _ribbonAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _ribbonDiffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _ribbonSpecularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _ribbonAmbientIntensity = 0.2;
  double _ribbonDiffuseIntensity = 1.0;
  double _ribbonSpecularIntensity = 1.0;
  double _ribbonShininess = 6.0;
};
