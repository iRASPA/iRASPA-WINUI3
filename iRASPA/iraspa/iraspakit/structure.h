/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
    D.Dubbeldam@uva.nl            https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html
    S.Calero@tue.nl               https://www.tue.nl/en/research/researchers/sofia-calero/
    t.j.h.vlugt@tudelft.nl        http://homepage.tudelft.nl/v9k6y

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include "rkcolor.h"
#include "rkstring.h"
#include "rkcolor.h"
#include "rkdate.h"
#include "binaryarchive.h"
#include <utility>
#include <type_traits>
#include <mathkit.h>
#include <symmetrykit.h>
#include <simulationkit.h>
#include <renderkit.h>
#include "iraspakitprotocols.h"
#include "displayable.h"
#include "object.h"
#include "infoviewer.h"
#include "unitcellviewer.h"
#include "atomviewer.h"
#include "bondviewer.h"
#include "primitivestructureviewer.h"
#include "atomstructureviewer.h"
#include "bondstructureviewer.h"
#include "volumetricdataviewer.h"
#include "annotationviewer.h"
#include "primitive.h"
#include "gridvolume.h"

struct enum_hash
{
  template <typename T>
  inline
  typename std::enable_if<std::is_enum<T>::value, std::size_t>::type
  operator ()(T const value) const
  {
    return static_cast<std::size_t>(value);
  }
};

class Structure: public Object, public InfoEditor,  public AtomViewer, public BondViewer,
                 public AtomStructureEditor, public BondStructureEditor,
                 public AnnotationEditor,
                 public RKRenderAtomSource, public RKRenderBondSource
{
public:
  Structure();
  Structure(std::shared_ptr<SKAtomTreeController> atomTreeController);
  Structure(std::shared_ptr<SKStructure> structure);
  Structure(const Structure &structure);

  virtual ~Structure() {}

  // Object
  // ===============================================================================================
  Structure(const std::shared_ptr<Object> object);
  ObjectType structureType() override {return ObjectType::structure;}
  std::shared_ptr<Object> shallowClone() override;
  SKBoundingBox boundingBox() const override;
  void reComputeBoundingBox() override;
  SKBoundingBox transformedBoundingBox() const;

  enum class StructureType: int64_t
  {
    framework = 0, adsorbate = 1, cation = 2, ionicLiquid = 3, solvent = 4
  };

  // Protocol:  AtomViewer
  // ===============================================================================================
  std::shared_ptr<SKAtomTreeController> &atomsTreeController() override {return _atomsTreeController;}
  const std::shared_ptr<SKAtomTreeController> &atomsTreeController() const {return _atomsTreeController;}
  void expandSymmetry() override;

  // The waters and ions a protein crystal is soaked in arrive as a part of their own, made up of
  // nothing but the HETATM records the parser marked as solvent. Nothing records that the part as a
  // whole is solvent, so it is read back from the atoms, which carry the mark through the archive.
  bool containsOnlySolventAtoms() const;

  void clearSelection() override;
  void setAtomSelection(int asymmetricAtomId) override;
  void addAtomToSelection(int asymmetricAtomId) override;
  void toggleAtomSelection(int asymmetricAtomId) override;
  void setAtomSelection(std::set<int>& atomIds) override;
  void addToAtomSelection(std::set<int>& atomIds) override;

  std::set<int> filterCartesianAtomPositions(std::function<bool(double3)> &) override;

  void recomputeSelectionBodyFixedBasis() override;

  void convertAsymmetricAtomsToCartesian() override;
  void convertAsymmetricAtomsToFractional() override;

  virtual std::vector<std::pair<std::shared_ptr<SKAsymmetricAtom>, double3>> translatedPositionsSelectionCartesian(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms, double3 translation) const override;
  virtual std::vector<std::pair<std::shared_ptr<SKAsymmetricAtom>, double3>> translatedPositionsSelectionBodyFrame(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms, double3 translation) const override;
  virtual std::vector<std::pair<std::shared_ptr<SKAsymmetricAtom>, double3>> rotatedPositionsSelectionCartesian(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms, simd_quatd rotation) const override;
  virtual std::vector<std::pair<std::shared_ptr<SKAsymmetricAtom>, double3>> rotatedPositionsSelectionBodyFrame(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms, simd_quatd rotation) const override;

  // Protocol:  AtomEditor
  // ===============================================================================================
  // Undo paths swap in a whole tree, which changes visibility without touching a single node.
  void setAtomTreeController(std::shared_ptr<SKAtomTreeController> controller) {_atomsTreeController = controller; skInvalidateAtomVisibilityGeneration();}

  // Protocol:  RKRenderAtomSource
  // ===============================================================================================
  bool drawAtoms() const override {return _drawAtoms;}
  // True when the structure is drawing at least one atom. Used with ribbons so ambient occlusion
  // can tell ribbon-only presentation apart from atoms and ribbon together.
  bool hasVisibleAtoms() const;

  RKColor atomAmbientColor() const override {return _atomAmbientColor;}
  RKColor atomDiffuseColor() const override {return _atomDiffuseColor;}
  RKColor atomSpecularColor() const override {return _atomSpecularColor;}
  double atomAmbientIntensity() const override {return _atomAmbientIntensity;}
  double atomDiffuseIntensity() const override {return _atomDiffuseIntensity;}
  double atomSpecularIntensity() const override {return _atomSpecularIntensity;}
  double atomShininess() const override {return _atomShininess;}

  double atomHue() const override {return _atomHue;}
  double atomSaturation() const override {return _atomSaturation;}
  double atomValue() const override {return _atomValue;}

  bool colorAtomsWithBondColor() const override;

  double atomScaleFactor() const override {return _atomScaleFactor;}
  bool atomAmbientOcclusion() const override {return _atomAmbientOcclusion;}
  int atomAmbientOcclusionPatchNumber() const override {return _atomAmbientOcclusionPatchNumber;}
  int atomAmbientOcclusionPatchSize() const override {return _atomAmbientOcclusionPatchSize;}
  int atomAmbientOcclusionTextureSize() const override {return _atomAmbientOcclusionTextureSize;}
  void setAtomAmbientOcclusionPatchNumber(int patchNumber) override  {_atomAmbientOcclusionPatchNumber=patchNumber;}
  void setAtomAmbientOcclusionPatchSize(int patchSize)  override {_atomAmbientOcclusionPatchSize=patchSize;}
  void setAtomAmbientOcclusionTextureSize(int size)  override {_atomAmbientOcclusionTextureSize=size;}

  bool atomHDR() const  override{return _atomHDR;}
  double atomHDRExposure() const override {return _atomHDRExposure;}
  bool clipAtomsAtUnitCell() const override {return false;}
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderAtoms() const override;

  virtual std::vector<RKInPerInstanceAttributesText> atomTextData(RKFontAtlas *fontAtlas) const override;
  std::vector<RKInPerInstanceAttributesText> renderTextData() const override {return std::vector<RKInPerInstanceAttributesText>();}
  RKTextType renderTextType() const override {return _atomTextType;}
  RKString renderTextFont() const override {return _atomTextFont;}
  RKTextAlignment renderTextAlignment() const override {return _atomTextAlignment;}
  RKTextStyle renderTextStyle() const override {return _atomTextStyle;}
  RKColor renderTextColor() const override {return _atomTextColor;}
  double renderTextScaling() const override {return _atomTextScaling;}
  double3 renderTextOffset() const override {return _atomTextOffset;}

  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedAtoms() const override;
  RKSelectionStyle atomSelectionStyle() const override {return _atomSelectionStyle;}
  double atomSelectionStripesDensity() const override {return _atomSelectionStripesDensity;}
  double atomSelectionStripesFrequency() const override {return _atomSelectionStripesFrequency;}
  double atomSelectionWorleyNoise3DFrequency() const override {return _atomSelectionWorleyNoise3DFrequency;}
  double atomSelectionWorleyNoise3DJitter() const override {return _atomSelectionWorleyNoise3DJitter;}
  double atomSelectionScaling() const override {return _atomSelectionScaling;}
  double atomSelectionIntensity() const override {return _atomSelectionIntensity;}

  // Protocol:  AtomStructureViewer (most are already in RKRenderAtomSource)
  // ===============================================================================================

  RepresentationType atomRepresentationType() override {return _atomRepresentationType;}
  RepresentationStyle atomRepresentationStyle() override {return _atomRepresentationStyle;}
  // Read and written so a document keeps what it was given; the renderer does not draw the cues
  // yet, so a quteMol structure is shaded as Fancy until it does.
  RKEdgeCueing atomEdgeCueing() const override {return _atomEdgeCueing;}
  void setAtomEdgeCueing(RKEdgeCueing cueing) override {_atomEdgeCueing = cueing;}
  RKString atomColorSchemeIdentifier() override {return _atomColorSchemeIdentifier;}
  SKColorSet::ColorSchemeOrder colorSchemeOrder() override {return _atomColorSchemeOrder;}
  RKString atomForceFieldIdentifier() override {return _atomForceFieldIdentifier;}
  ForceFieldSet::ForceFieldSchemeOrder forceFieldSchemeOrder() override {return _atomForceFieldOrder;}

  double atomSelectionFrequency() const override;
  double atomSelectionDensity() const override;

  // The radius an atom of this element takes under the current representation
  // type, for code that builds asymmetric atoms from scratch: they start at the
  // SKAsymmetricAtom default of 1.0, which is right for no element in
  // particular. Cocoa's Structure.drawRadius(elementId:).
  double drawRadius(int elementIdentifier) const;

  // Protocol:  AtomStructureEditor
  // ===============================================================================================

  void recheckRepresentationStyle() override;
  void setDrawAtoms(bool drawAtoms) override {_drawAtoms = drawAtoms;}

  void setRepresentationType(RepresentationType) override final;
  void setRepresentationStyle(RepresentationStyle style) override;
  void setRepresentationStyle(RepresentationStyle style, const SKColorSets &colorSets) override;
  void setRepresentationColorSchemeIdentifier(const RKString colorSchemeName, const SKColorSets &colorSets) override;
  void setColorSchemeOrder(SKColorSet::ColorSchemeOrder order) override {_atomColorSchemeOrder = order;}
  void setAtomForceFieldIdentifier(RKString identifier, ForceFieldSets &forceFieldSets) override;
  void updateForceField(ForceFieldSets &forceFieldSets) override;
  void setForceFieldSchemeOrder(ForceFieldSet::ForceFieldSchemeOrder order) override {_atomForceFieldOrder = order;}

  void setAtomHue(double value) override {_atomHue = value;}
  void setAtomSaturation(double value) override {_atomSaturation = value;}
  void setAtomValue(double value) override {_atomValue = value;}
  void setAtomScaleFactor(double size) override;

  void setAtomAmbientOcclusion(bool value) override {_atomAmbientOcclusion = value;}
  void setAtomHDR(bool value) override {_atomHDR = value;}
  void setAtomHDRExposure(double value) override {_atomHDRExposure = value;}

  void setAtomAmbientColor(RKColor color) override {_atomAmbientColor = color;}
  void setAtomDiffuseColor(RKColor color) override {_atomDiffuseColor = color;}
  void setAtomSpecularColor(RKColor color) override {_atomSpecularColor = color;}
  void setAtomAmbientIntensity(double value) override {_atomAmbientIntensity = value;}
  void setAtomDiffuseIntensity(double value) override {_atomDiffuseIntensity = value;}
  void setAtomSpecularIntensity(double value) override {_atomSpecularIntensity = value;}
  void setAtomShininess(double value) override {_atomShininess = value;}

  void setAtomSelectionStyle(RKSelectionStyle style) override {_atomSelectionStyle = style;}
  void setAtomSelectionFrequency(double value) override;
  void setAtomSelectionDensity(double value) override;
  void setAtomSelectionScaling(double scaling) override {_atomSelectionScaling = scaling;}
  void setSelectionIntensity(double scaling) override {_atomSelectionIntensity = scaling;}

  void setAtomSelectionStripesDensity(double value)  {_atomSelectionStripesDensity = value;}
  void setAtomSelectionStripesFrequency(double value)  {_atomSelectionStripesFrequency = value;}
  void setAtomSelectionWorleyNoise3DFrequency(double value)  {_atomSelectionWorleyNoise3DFrequency = value;}
  void setAtomSelectionWorleyNoise3DJitter(double value)  {_atomSelectionWorleyNoise3DJitter = value;}

  // move
  void setRenderTextType(RKTextType type) override {_atomTextType = type;}
  void setRenderTextFont(RKString value) override {_atomTextFont = value;}
  void setRenderTextAlignment(RKTextAlignment alignment) override {_atomTextAlignment = alignment;}
  void setRenderTextStyle(RKTextStyle style) override {_atomTextStyle = style;}
  void setRenderTextColor(RKColor color) override {_atomTextColor = color;}
  void setRenderTextScaling(double scaling) override {_atomTextScaling = scaling;}
  void setRenderTextOffsetX(double value) override {_atomTextOffset.x = value;}
  void setRenderTextOffsetY(double value) override {_atomTextOffset.y = value;}
  void setRenderTextOffsetZ(double value) override {_atomTextOffset.z = value;}

  // Protocol:  BondViewer
  // ===============================================================================================

  std::shared_ptr<SKBondSetController> bondSetController() override {return _bondSetController;}
  void computeBonds() override {;}

  void setBondSelection(int asymmetricBondId) override;
  void addBondToSelection(int asymmetricBondId) override;
  void toggleBondSelection(int asymmetricAtomId) override;

   BondSelectionIndexSet filterCartesianBondPositions(std::function<bool(double3)> &) override;

  // Protocol:  BondEditor
  // ===============================================================================================

   // TODO

  // Protocol:  RKRenderBondSource
  // ===============================================================================================

  bool drawBonds() const override {return _drawBonds;}
  int numberOfInternalBonds() const override {return 0;}
  int numberOfExternalBonds() const override {return 0;}
  virtual std::vector<RKInPerInstanceAttributesBonds> renderInternalBonds() const override;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderExternalBonds() const override;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderSelectedInternalBonds() const override;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderSelectedExternalBonds() const override;

  bool bondAmbientOcclusion() const override {return _bondAmbientOcclusion;}
  RKColor bondAmbientColor() const override {return _bondAmbientColor;}
  RKColor bondDiffuseColor() const override {return _bondDiffuseColor;}
  RKColor bondSpecularColor() const override {return _bondSpecularColor;}
  double bondAmbientIntensity() const override {return _bondAmbientIntensity;}
  double bondDiffuseIntensity() const override {return _bondDiffuseIntensity;}
  double bondSpecularIntensity() const override {return _bondSpecularIntensity;}
  double bondShininess() const override {return _bondShininess;}

  bool isUnity() const override final {return _atomRepresentationType == RepresentationType::unity;}
  bool hasExternalBonds() const override {return true;}

  double bondScaleFactor() const override {return _bondScaleFactor;}
  RKBondColorMode bondColorMode() const override {return _bondColorMode;}

  bool bondHDR() const override {return _bondHDR;}
  double bondHDRExposure() const override {return _bondHDRExposure;}

  bool clipBondsAtUnitCell() const override {return false;}

  double bondHue() const override {return _bondHue;}
  double bondSaturation() const override {return _bondSaturation;}
  double bondValue() const override {return _bondValue;}

  RKSelectionStyle bondSelectionStyle() const override {return _bondSelectionStyle;}
  double bondSelectionStripesDensity() const override {return _bondSelectionStripesDensity;}
  double bondSelectionStripesFrequency() const override  {return _bondSelectionStripesFrequency;}
  double bondSelectionWorleyNoise3DFrequency() const override {return _bondSelectionWorleyNoise3DFrequency;}
  double bondSelectionWorleyNoise3DJitter() const override  {return _bondSelectionWorleyNoise3DJitter;}
  double bondSelectionIntensity() const override {return _bondSelectionIntensity;}
  double bondSelectionScaling() const override {return _bondSelectionScaling;}

  // Protocol:  BondStructureViewer (most are already in RKRenderBondSource)
  // ===============================================================================================

  double bondSelectionFrequency() const override;
  double bondSelectionDensity() const override;

  // Protocol:  BondStructureEditor
  // ===============================================================================================

  void setDrawBonds(bool drawBonds) override {_drawBonds = drawBonds;}
  void setBondScaleFactor(double value) override;
  void setBondColorMode(RKBondColorMode value) override {_bondColorMode = value;}

  void setBondAmbientOcclusion(bool state) override {_bondAmbientOcclusion = state;}

  void setBondHDR(bool value) override {_bondHDR = value;}
  void setBondHDRExposure(double value) override {_bondHDRExposure = value;}

  void setBondHue(double value) override {_bondHue = value;}
  void setBondSaturation(double value) override {_bondSaturation = value;}
  void setBondValue(double value) override {_bondValue = value;}

  void setBondAmbientColor(RKColor color) override {_bondAmbientColor = color;}
  void setBondDiffuseColor(RKColor color) override {_bondDiffuseColor = color;}
  void setBondSpecularColor(RKColor color) override {_bondSpecularColor = color;}
  void setBondAmbientIntensity(double value) override {_bondAmbientIntensity = value;}
  void setBondDiffuseIntensity(double value) override {_bondDiffuseIntensity = value;}
  void setBondSpecularIntensity(double value) override {_bondSpecularIntensity = value;}
  void setBondShininess(double value) override {_bondShininess = value;}

  void setBondSelectionStyle(RKSelectionStyle style) override {_bondSelectionStyle = style;}
  void setBondSelectionFrequency(double value) override;
  void setBondSelectionDensity(double value) override;
  void setBondSelectionIntensity(double value) override {_bondSelectionIntensity = value;}
  void setBondSelectionScaling(double scaling) override {_bondSelectionScaling = scaling;}

  // To be overwritten in subclasses of 'Structure'
  // ===============================================================================================
  bool isFractional() override  {return false;}
  virtual bool hasSymmetry() {return false;}
  virtual std::shared_ptr<Structure> superCell() const {return nullptr;}
  virtual std::shared_ptr<Structure> removeSymmetry() const {return nullptr;}
  virtual std::shared_ptr<Structure> wrapAtomsToCell() const {return nullptr;}
  virtual std::shared_ptr<Structure> flattenHierarchy() const {return nullptr;}
  virtual std::shared_ptr<Structure> appliedCellContentShift() const {return nullptr;}
  virtual std::vector<std::tuple<double3,int,double>> atomSymmetryData() const {return {};}
  virtual void setAtomSymmetryData(double3x3 unitCell, std::vector<std::tuple<double3,int,double>> atomData) {(void)(unitCell); (void)(atomData);};

  virtual bool hasSelectedAtoms() const;

  virtual std::vector<double3> atomPositions() const {return std::vector<double3>();}
  virtual std::vector<double3> bondPositions() const {return std::vector<double3>();}

  virtual std::vector<double2> potentialParameters() const {return std::vector<double2>();}

  virtual std::vector<RKInPerInstanceAttributesAtoms> renderUnitCellSpheres() const override;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderUnitCellCylinders() const override;

  virtual std::optional<std::pair<std::shared_ptr<SKCell>, double3>> cellForFractionalPositions();
  virtual std::optional<std::pair<std::shared_ptr<SKCell>, double3>> cellForCartesianPositions();
  virtual std::vector<std::shared_ptr<SKAsymmetricAtom>> asymmetricAtomsCopiedAndTransformedToFractionalPositions();
  virtual std::vector<std::shared_ptr<SKAsymmetricAtom>> asymmetricAtomsCopiedAndTransformedToCartesianPositions();
  virtual std::vector<std::shared_ptr<SKAsymmetricAtom>> atomsCopiedAndTransformedToFractionalPositions();
  virtual std::vector<std::shared_ptr<SKAsymmetricAtom>> atomsCopiedAndTransformedToCartesianPositions();

  virtual double3 centerOfMassOfSelectionAsymmetricAtoms(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms) const;
  virtual double3x3 matrixOfInertia(std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms) const;

  virtual double bondLength(std::shared_ptr<SKBond> bond) const override;
  virtual double3 bondVector(std::shared_ptr<SKBond> bond) const;
  virtual std::pair<double3, double3> computeChangedBondLength(std::shared_ptr<SKBond>, double) const;

  virtual double3 CartesianPosition(double3 position, int3 replicaPosition) const {(void)(position); (void)(replicaPosition); return double(); }
  virtual double3 FractionalPosition(double3 position, int3 replicaPosition) const {(void)(position); (void)(replicaPosition); return double(); }

  double2 adsorptionSurfaceProbeParameters() const;
  ProbeMolecule adsorptionSurfaceProbeMolecule() const {return _adsorptionSurfaceProbeMolecule;}

  void recomputeDensityProperties();
  double2 frameworkProbeParameters() const;
  void setStructureNitrogenSurfaceArea(double value);

  SKSpaceGroup& legacySpaceGroup() {return _legacySpaceGroup;}

  // ==========================================================================================
  // Legacy for reading primitive from 'Structure' (Remove in the future)
  simd_quatd primitiveOrientation() const {return _primitiveOrientation;}
  void setPrimitiveOrientation(simd_quatd orientation) {_primitiveOrientation = orientation;}
  double primitiveRotationDelta() const {return _primitiveRotationDelta;}
  void setPrimitiveRotationDelta(double angle) {_primitiveRotationDelta = angle;}
  double3x3 transformationMatrix() const {return _primitiveTransformationMatrix;}
  void setTransformationMatrix(double3x3 matrix) {_primitiveTransformationMatrix = matrix;}
  double primitiveOpacity() const  {return _primitiveOpacity;}
  void setPrimitiveOpacity(double opacity) {_primitiveOpacity = opacity;}
  int primitiveNumberOfSides() const {return _primitiveNumberOfSides;}
  void setPrimitiveNumberOfSides(int numberOfSides) {_primitiveNumberOfSides = numberOfSides;}
  bool primitiveIsCapped() const {return _primitiveIsCapped;}
  void setPrimitiveIsCapped(bool isCapped) {_primitiveIsCapped = isCapped;}

  RKSelectionStyle primitiveSelectionStyle() const {return _primitiveSelectionStyle;}
  void setPrimitiveSelectionStyle(RKSelectionStyle style) {_primitiveSelectionStyle = style;}
  double primitiveSelectionScaling() const {return _primitiveSelectionScaling;}
  void setPrimitiveSelectionScaling(double scaling) {_primitiveSelectionScaling = scaling;}
  double primitiveSelectionIntensity() const {return _primitiveSelectionIntensity;}
  void setPrimitiveSelectionIntensity(double value)  {_primitiveSelectionIntensity = value;}

  double primitiveSelectionStripesDensity() const {return _primitiveSelectionStripesDensity;}
  double primitiveSelectionStripesFrequency() const  {return _primitiveSelectionStripesFrequency;}
  double primitiveSelectionWorleyNoise3DFrequency() const {return _primitiveSelectionWorleyNoise3DFrequency;}
  double primitiveSelectionWorleyNoise3DJitter() const {return _primitiveSelectionWorleyNoise3DJitter;}

  double primitiveSelectionFrequency() const ;
  void setPrimitiveSelectionFrequency(double value) ;
  double primitiveSelectionDensity() const ;
  void setPrimitiveSelectionDensity(double value) ;

  double primitiveHue() const  {return _primitiveHue;}
  void setPrimitiveHue(double value)  {_primitiveHue = value;}
  double primitiveSaturation() const  {return _primitiveSaturation;}
  void setPrimitiveSaturation(double value)  {_primitiveSaturation = value;}
  double primitiveValue() const  {return _primitiveValue;}
  void setPrimitiveValue(double value)  {_primitiveValue = value;}

  bool frontPrimitiveHDR() const  {return _primitiveFrontSideHDR;}
  void setFrontPrimitiveHDR(bool isHDR)  {_primitiveFrontSideHDR = isHDR;}
  double frontPrimitiveHDRExposure() const  {return _primitiveFrontSideHDRExposure;}
  void setFrontPrimitiveHDRExposure(double exposure)  {_primitiveFrontSideHDRExposure = exposure;}
  double frontPrimitiveAmbientIntensity() const  {return _primitiveFrontSideAmbientIntensity;}
  void setFrontPrimitiveAmbientIntensity(double intensity)  {_primitiveFrontSideAmbientIntensity = intensity;}
  double frontPrimitiveDiffuseIntensity() const  {return _primitiveFrontSideDiffuseIntensity;}
  void setFrontPrimitiveDiffuseIntensity(double intensity)  {_primitiveFrontSideDiffuseIntensity = intensity;}
  double frontPrimitiveSpecularIntensity() const  {return _primitiveFrontSideSpecularIntensity;}
  void setFrontPrimitiveSpecularIntensity(double intensity)  {_primitiveFrontSideSpecularIntensity = intensity;}
  RKColor frontPrimitiveAmbientColor() const  {return _primitiveFrontSideAmbientColor;}
  void setFrontPrimitiveAmbientColor(RKColor color)  {_primitiveFrontSideAmbientColor = color;}
  RKColor frontPrimitiveDiffuseColor() const  {return _primitiveFrontSideDiffuseColor;}
  void setFrontPrimitiveDiffuseColor(RKColor color)  {_primitiveFrontSideDiffuseColor = color;}
  RKColor frontPrimitiveSpecularColor() const  {return _primitiveFrontSideSpecularColor;}
  void setFrontPrimitiveSpecularColor(RKColor color)  {_primitiveFrontSideSpecularColor = color;}
  double frontPrimitiveShininess() const  {return _primitiveFrontSideShininess;}
  void setFrontPrimitiveShininess(double value)  {_primitiveFrontSideShininess = value;}

  bool backPrimitiveHDR() const  {return _primitiveBackSideHDR;}
  void setBackPrimitiveHDR(bool isHDR)  {_primitiveBackSideHDR = isHDR;}
  double backPrimitiveHDRExposure() const  {return _primitiveBackSideHDRExposure;}
  void setBackPrimitiveHDRExposure(double exposure)  {_primitiveBackSideHDRExposure = exposure;}
  double backPrimitiveAmbientIntensity() const  {return _primitiveBackSideAmbientIntensity;}
  void setBackPrimitiveAmbientIntensity(double intensity)  {_primitiveBackSideAmbientIntensity = intensity;}
  double backPrimitiveDiffuseIntensity() const  {return _primitiveBackSideDiffuseIntensity;}
  void setBackPrimitiveDiffuseIntensity(double intensity)  {_primitiveBackSideDiffuseIntensity = intensity;}
  double backPrimitiveSpecularIntensity() const  {return _primitiveBackSideSpecularIntensity;}
  void setBackPrimitiveSpecularIntensity(double intensity)  {_primitiveBackSideSpecularIntensity = intensity;}
  RKColor backPrimitiveAmbientColor() const  {return _primitiveBackSideAmbientColor;}
  void setBackPrimitiveAmbientColor(RKColor color)  {_primitiveBackSideAmbientColor = color;}
  RKColor backPrimitiveDiffuseColor() const  {return _primitiveBackSideDiffuseColor;}
  void setBackPrimitiveDiffuseColor(RKColor color)  {_primitiveBackSideDiffuseColor = color;}
  RKColor backPrimitiveSpecularColor() const  {return _primitiveBackSideSpecularColor;}
  void setBackPrimitiveSpecularColor(RKColor color)  {_primitiveBackSideSpecularColor = color;}
  double backPrimitiveShininess() const  {return _primitiveBackSideShininess;}
  void setBackPrimitiveShininess(double value)  {_primitiveBackSideShininess = value;}
  // ==========================================================================================

  // info
  //=================================================================================================
  // remove in future version
  RKString authorFirstName()  {return _authorFirstName;}
  void setAuthorFirstName(RKString name) {_authorFirstName = name;}
  RKString authorMiddleName() {return _authorMiddleName;}
  void setAuthorMiddleName(RKString name)  {_authorMiddleName = name;}
  RKString authorLastName() {return _authorLastName;}
  void setAuthorLastName(RKString name)  {_authorLastName = name;}
  RKString authorOrchidID()  {return _authorOrchidID;}
  void setAuthorOrchidID(RKString name)  {_authorOrchidID = name;}
  RKString authorResearcherID()  {return _authorResearcherID;}
  void setAuthorResearcherID(RKString name)  {_authorResearcherID = name;}
  RKString authorAffiliationUniversityName()  {return _authorAffiliationUniversityName;}
  void setAuthorAffiliationUniversityName(RKString name)  {_authorAffiliationUniversityName = name;}
  RKString authorAffiliationFacultyName()  {return _authorAffiliationFacultyName;}
  void setAuthorAffiliationFacultyName(RKString name) {_authorAffiliationFacultyName = name;}
  RKString authorAffiliationInstituteName()  {return _authorAffiliationInstituteName;}
  void setAuthorAffiliationInstituteName(RKString name)  {_authorAffiliationInstituteName = name;}
  RKString authorAffiliationCityName()  {return _authorAffiliationCityName;}
  void setAuthorAffiliationCityName(RKString name)  {_authorAffiliationCityName = name;}
  RKString authorAffiliationCountryName()  {return _authorAffiliationCountryName;}
  void setAuthorAffiliationCountryName(RKString name)  {_authorAffiliationCountryName = name;}

  RKDate creationDate() {return _creationDate;}
  void setCreationDate(RKDate date) {_creationDate = date;}
  //=================================================================================================

  RKString creationTemperature() override final {return _creationTemperature;}
  void setCreationTemperature(RKString name) override final {_creationTemperature = name;}
  TemperatureScale creationTemperatureScale() override final {return _creationTemperatureScale;}
  void setCreationTemperatureScale(TemperatureScale scale) override final {_creationTemperatureScale = scale;}
  RKString creationPressure() override final {return _creationPressure;}
  void setCreationPressure(RKString pressure) override final {_creationPressure = pressure;}
  PressureScale creationPressureScale() override final {return _creationPressureScale;}
  void setCreationPressureScale(PressureScale scale) override final {_creationPressureScale = scale;}
  CreationMethod creationMethod() override final {return _creationMethod;}
  void setCreationMethod(CreationMethod method) override final {_creationMethod = method;}
  UnitCellRelaxationMethod creationUnitCellRelaxationMethod() override final {return _creationUnitCellRelaxationMethod;}
  void setCreationUnitCellRelaxationMethod(UnitCellRelaxationMethod method) override final {_creationUnitCellRelaxationMethod = method;}
  RKString creationAtomicPositionsSoftwarePackage() override final {return _creationAtomicPositionsSoftwarePackage;}
  void setCreationAtomicPositionsSoftwarePackage(RKString name) override final {_creationAtomicPositionsSoftwarePackage = name;}
  IonsRelaxationAlgorithm creationAtomicPositionsIonsRelaxationAlgorithm() override final {return _creationAtomicPositionsIonsRelaxationAlgorithm;}
  void setCreationAtomicPositionsIonsRelaxationAlgorithm(IonsRelaxationAlgorithm algorithm) override final {_creationAtomicPositionsIonsRelaxationAlgorithm = algorithm;}
  IonsRelaxationCheck creationAtomicPositionsIonsRelaxationCheck() override final {return _creationAtomicPositionsIonsRelaxationCheck;}
  void setCreationAtomicPositionsIonsRelaxationCheck(IonsRelaxationCheck check) override final {_creationAtomicPositionsIonsRelaxationCheck = check;}
  RKString creationAtomicPositionsForcefield() override final {return _creationAtomicPositionsForcefield;}
  void setCreationAtomicPositionsForcefield(RKString name) override final {_creationAtomicPositionsForcefield = name;}
  RKString creationAtomicPositionsForcefieldDetails() override final {return _creationAtomicPositionsForcefieldDetails;}
  void setCreationAtomicPositionsForcefieldDetails(RKString name) override final {_creationAtomicPositionsForcefieldDetails = name;}
  RKString creationAtomicChargesSoftwarePackage() override final {return _creationAtomicChargesSoftwarePackage;}
  void setCreationAtomicChargesSoftwarePackage(RKString name) override final {_creationAtomicChargesSoftwarePackage = name;}
  RKString creationAtomicChargesAlgorithms() override final {return _creationAtomicChargesAlgorithms;}
  void setCreationAtomicChargesAlgorithms(RKString name) override final {_creationAtomicChargesAlgorithms = name;}
  RKString creationAtomicChargesForcefield() override final {return _creationAtomicChargesForcefield;}
  void setCreationAtomicChargesForcefield(RKString name) override final {_creationAtomicChargesForcefield = name;}
  RKString creationAtomicChargesForcefieldDetails() override final {return _creationAtomicChargesForcefieldDetails;}
  void setCreationAtomicChargesForcefieldDetails(RKString name) override final {_creationAtomicChargesForcefieldDetails = name;}

  RKString experimentalMeasurementRadiation() override final {return _experimentalMeasurementRadiation;}
  void setExperimentalMeasurementRadiation(RKString name) override final {_experimentalMeasurementRadiation = name;}
  RKString experimentalMeasurementWaveLength() override final {return _experimentalMeasurementWaveLength;}
  void setExperimentalMeasurementWaveLength(RKString name) override final {_experimentalMeasurementWaveLength = name;}
  RKString experimentalMeasurementThetaMin() override final {return _experimentalMeasurementThetaMin;}
  void setExperimentalMeasurementThetaMin(RKString name) override final {_experimentalMeasurementThetaMin = name;}
  RKString experimentalMeasurementThetaMax() override final {return _experimentalMeasurementThetaMax;}
  void setExperimentalMeasurementThetaMax(RKString name) override final {_experimentalMeasurementThetaMax = name;}
  RKString experimentalMeasurementIndexLimitsHmin() override final {return _experimentalMeasurementIndexLimitsHmin;}
  void setExperimentalMeasurementIndexLimitsHmin(RKString name) override final {_experimentalMeasurementIndexLimitsHmin = name;}
  RKString experimentalMeasurementIndexLimitsHmax() override final {return _experimentalMeasurementIndexLimitsHmax;}
  void setExperimentalMeasurementIndexLimitsHmax(RKString name) override final {_experimentalMeasurementIndexLimitsHmax = name;}
  RKString experimentalMeasurementIndexLimitsKmin() override final {return _experimentalMeasurementIndexLimitsKmin;}
  void setExperimentalMeasurementIndexLimitsKmin(RKString name) override final {_experimentalMeasurementIndexLimitsKmin = name;}
  RKString experimentalMeasurementIndexLimitsKmax() override final {return _experimentalMeasurementIndexLimitsKmax;}
  void setExperimentalMeasurementIndexLimitsKmax(RKString name) override final {_experimentalMeasurementIndexLimitsKmax = name;}
  RKString experimentalMeasurementIndexLimitsLmin() override final {return _experimentalMeasurementIndexLimitsLmin;}
  void setExperimentalMeasurementIndexLimitsLmin(RKString name) override final {_experimentalMeasurementIndexLimitsLmin = name;}
  RKString experimentalMeasurementIndexLimitsLmax() override final {return _experimentalMeasurementIndexLimitsLmax;}
  void setExperimentalMeasurementIndexLimitsLmax(RKString name) override final {_experimentalMeasurementIndexLimitsLmax = name;}
  RKString experimentalMeasurementNumberOfSymmetryIndependentReflections() override final {return _experimentalMeasurementNumberOfSymmetryIndependentReflections;}
  void setExperimentalMeasurementNumberOfSymmetryIndependentReflections(RKString name) override final {_experimentalMeasurementNumberOfSymmetryIndependentReflections = name;}
  RKString experimentalMeasurementSoftware() override final {return _experimentalMeasurementSoftware;}
  void setExperimentalMeasurementSoftware(RKString name) override final {_experimentalMeasurementSoftware = name;}
  RKString experimentalMeasurementRefinementDetails() override final {return _experimentalMeasurementRefinementDetails;}
  void setExperimentalMeasurementGoodnessOfFit(RKString goodness) override final {_experimentalMeasurementGoodnessOfFit = goodness;}
  RKString experimentalMeasurementGoodnessOfFit() override final {return _experimentalMeasurementGoodnessOfFit;}
  void setExperimentalMeasurementRefinementDetails(RKString name) override final {_experimentalMeasurementGoodnessOfFit = name;}
  RKString experimentalMeasurementRFactorGt() override final {return _experimentalMeasurementRFactorGt;}
  void setExperimentalMeasurementRFactorGt(RKString name) override final {_experimentalMeasurementRFactorGt = name;}
  RKString experimentalMeasurementRFactorAll() override final {return _experimentalMeasurementRFactorAll;}
  void setExperimentalMeasurementRFactorAll(RKString name) override final {_experimentalMeasurementRFactorAll = name;}

  RKString chemicalFormulaMoiety() override final {return _chemicalFormulaMoiety;}
  void setChemicalFormulaMoiety(RKString name) override final {_chemicalFormulaMoiety = name;}
  RKString chemicalFormulaSum() override final {return _chemicalFormulaSum;}
  void setChemicalFormulaSum(RKString name) override final {_chemicalFormulaSum = name;}
  RKString chemicalNameSystematic() override final {return _chemicalNameSystematic;}
  void setChemicalNameSystematic(RKString name) override final {_chemicalNameSystematic = name;}

  RKString citationArticleTitle() override final {return _citationArticleTitle;}
  void setCitationArticleTitle(RKString name) override final {_citationArticleTitle = name;}
  RKString citationJournalTitle() override final {return _citationJournalTitle;}
  void setCitationJournalTitle(RKString name) override final {_citationJournalTitle = name;}
  RKString citationAuthors() override final {return _citationAuthors;}
  void setCitationAuthors(RKString name) override final {_citationAuthors = name;}
  RKString citationJournalVolume() override final {return _citationJournalVolume;}
  void setCitationJournalVolume(RKString name) override final {_citationJournalVolume = name;}
  RKString citationJournalNumber() override final {return _citationJournalNumber;}
  void setCitationJournalNumber(RKString name) override final {_citationJournalNumber = name;}
  RKString citationJournalPageNumbers() override final {return _citationJournalPageNumbers;}
  void setCitationJournalPageNumbers(RKString name) override final {_citationJournalPageNumbers = name;}
  RKString citationDOI() override final {return _citationDOI;}
  void setCitationDOI(RKString name) override final {_citationDOI = name;}
  RKDate citationPublicationDate() override final {return _citationPublicationDate;}
  void setCitationPublicationDate(RKDate date) override final {_citationPublicationDate = date;}
  RKString citationDatebaseCodes() override final {return _citationDatebaseCodes;}
  void setCitationDatebaseCodes(RKString name) override final {_citationDatebaseCodes = name;}

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<Structure> &);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<Structure> &);
protected:
  int64_t _versionNumber{11};

  std::shared_ptr<SKAtomTreeController> _atomsTreeController;
  std::shared_ptr<SKBondSetController> _bondSetController;

  SKSpaceGroup _legacySpaceGroup = SKSpaceGroup(1);

  // ==========================================================================================
  // Legacy for reading primitive from 'Structure' (Remove in the future)
  double3x3 _primitiveTransformationMatrix = double3x3(1.0,0.0,0.0, 0.0,1.0,0.0, 0.0,0.0,1.0);
  simd_quatd _primitiveOrientation = simd_quatd(0.0,0.0,0.0,1.0);
  double _primitiveRotationDelta = 5.0;

  double _primitiveOpacity = 1.0;
  bool _primitiveIsCapped = false;
  bool _primitiveIsFractional = true;
  int64_t _primitiveNumberOfSides = 6;
  double _primitiveThickness = 0.05;

  double _primitiveHue = 1.0;
  double _primitiveSaturation = 1.0;
  double _primitiveValue = 1.0;

  RKSelectionStyle _primitiveSelectionStyle = RKSelectionStyle::striped;
  double _primitiveSelectionStripesDensity = 0.25;
  double _primitiveSelectionStripesFrequency = 12.0;
  double _primitiveSelectionWorleyNoise3DFrequency = 2.0;
  double _primitiveSelectionWorleyNoise3DJitter = 1.0;
  double _primitiveSelectionScaling = 1.0;
  double _primitiveSelectionIntensity = 1.0;

  bool _primitiveFrontSideHDR = true;
  double _primitiveFrontSideHDRExposure = 2.0;
  RKColor _primitiveFrontSideAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _primitiveFrontSideDiffuseColor = RKColor::fromRgb(255, 255, 0, 255);
  RKColor _primitiveFrontSideSpecularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _primitiveFrontSideAmbientIntensity = 0.1;
  double _primitiveFrontSideDiffuseIntensity = 1.0;
  double _primitiveFrontSideSpecularIntensity = 0.2;
  double _primitiveFrontSideShininess = 4.0;

  bool _primitiveBackSideHDR = true;
  double _primitiveBackSideHDRExposure = 2.0;
  RKColor _primitiveBackSideAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _primitiveBackSideDiffuseColor = RKColor::fromRgb(0, 140, 255, 255);
  RKColor _primitiveBackSideSpecularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _primitiveBackSideAmbientIntensity = 0.1;
  double _primitiveBackSideDiffuseIntensity = 1.0;
  double _primitiveBackSideSpecularIntensity = 0.2;
  double _primitiveBackSideShininess = 4.0;
  // ==========================================================================================

  double _minimumGridEnergyValue = 0.0f;

  bool _drawAtoms =  true;

  RepresentationType _atomRepresentationType = RepresentationType::sticks_and_balls;
  RepresentationStyle _atomRepresentationStyle = RepresentationStyle::defaultStyle;
  RKEdgeCueing _atomEdgeCueing = RKEdgeCueing::off;
  RKString _atomForceFieldIdentifier = RKString("Default");
  ForceFieldSet::ForceFieldSchemeOrder _atomForceFieldOrder = ForceFieldSet::ForceFieldSchemeOrder::elementOnly;
  RKString _atomColorSchemeIdentifier = RKString("Jmol");
  SKColorSet::ColorSchemeOrder _atomColorSchemeOrder = SKColorSet::ColorSchemeOrder::elementOnly;

  RKSelectionStyle _atomSelectionStyle = RKSelectionStyle::WorleyNoise3D;
  double _atomSelectionStripesDensity = 0.25;
  double _atomSelectionStripesFrequency = 12.0;
  double _atomSelectionWorleyNoise3DFrequency = 2.0;
  double _atomSelectionWorleyNoise3DJitter = 1.0;
  double _atomSelectionScaling = 1.2;
  double _selectionIntensity = 1.0;

  double _atomHue = 1.0;
  double _atomSaturation = 1.0;
  double _atomValue = 1.0;
  double _atomScaleFactor = 1.0;

  bool _atomAmbientOcclusion = false;
  int64_t _atomAmbientOcclusionPatchNumber = 256;
  int64_t _atomAmbientOcclusionTextureSize = 1024;
  int64_t _atomAmbientOcclusionPatchSize = 16;
  //  public var _atomCacheAmbientOcclusionTexture: [CUnsignedChar] = [CUnsignedChar]();

  bool _atomHDR = true;
  double _atomHDRExposure = 1.5;
  double _atomSelectionIntensity = 0.5;

  RKColor _atomAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _atomDiffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _atomSpecularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _atomAmbientIntensity= 0.2;
  double _atomDiffuseIntensity = 1.0;
  double _atomSpecularIntensity = 1.0;
  double _atomShininess = 4.0;

  bool _drawBonds = true;

  double _bondScaleFactor = 1.0;
  RKBondColorMode _bondColorMode = RKBondColorMode::uniform;

  RKColor _bondAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _bondDiffuseColor = RKColor::fromRgb(200, 200, 200, 255);
  RKColor _bondSpecularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _bondAmbientIntensity = 0.1;
  double _bondDiffuseIntensity = 1.0;
  double _bondSpecularIntensity = 1.0;
  double _bondShininess = 4.0;

  bool _bondHDR = true;
  double _bondHDRExposure = 1.5;

  double _bondHue = 1.0;
  double _bondSaturation = 1.0;
  double _bondValue = 1.0;

  RKSelectionStyle _bondSelectionStyle = RKSelectionStyle::striped;
  double _bondSelectionScaling = 1.2;
  double _bondSelectionStripesDensity = 0.25;
  double _bondSelectionStripesFrequency = 12.0;
  double _bondSelectionWorleyNoise3DFrequency = 2.0;
  double _bondSelectionWorleyNoise3DJitter = 1.0;
  double _bondSelectionIntensity = 0.5;

  bool _bondAmbientOcclusion = false;

  RKTextType _atomTextType = RKTextType::none;
  RKString _atomTextFont = RKString("Helvetica");
  double _atomTextScaling = 1.0;
  RKColor _atomTextColor = RKColor::fromRgb(0, 0, 0, 255);
  RKColor _atomTextGlowColor = RKColor::fromRgb(0, 255, 0, 255);
  RKTextStyle _atomTextStyle = RKTextStyle::flatBillboard;
  RKTextEffect _atomTextEffect = RKTextEffect::none;
  RKTextAlignment _atomTextAlignment = RKTextAlignment::center;
  double3 _atomTextOffset = double3();

  bool _drawAdsorptionSurface = false;
  double _adsorptionSurfaceOpacity = 1.0;
  double _adsorptionTransparencyThreshold = 0.0;
  double _adsorptionSurfaceIsoValue = 0.0;
  int64_t _encompassingPowerOfTwoCubicGridSize = 7;
  double _adsorptionSurfaceMinimumValue = -1000.0;

  //std::pair<double,double> _range{};
  //int3 _dimensions{128,128,128};
  RKEnergySurfaceType _adsorptionSurfaceRenderingMethod = RKEnergySurfaceType::isoSurface;
  RKPredefinedVolumeRenderingTransferFunction  _adsorptionVolumeTransferFunction = RKPredefinedVolumeRenderingTransferFunction::RASPA_PES;
  double _adsorptionVolumeStepLength = 0.0005;

  int64_t _adsorptionSurfaceSize = 128;
  int64_t _adsorptionSurfaceNumberOfTriangles = 0;

  ProbeMolecule _adsorptionSurfaceProbeMolecule = ProbeMolecule::helium;

  double _adsorptionSurfaceHue = 1.0;
  double _adsorptionSurfaceSaturation = 1.0;
  double _adsorptionSurfaceValue = 1.0;

  bool _adsorptionSurfaceFrontSideHDR = true;
  double _adsorptionSurfaceFrontSideHDRExposure = 2.0;
  RKColor _adsorptionSurfaceFrontSideAmbientColor = RKColor::fromRgb(0, 0, 0, 255);
  RKColor _adsorptionSurfaceFrontSideDiffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  // Cocoa NSColor(red: 0.92, …). RKColor(r,g,b) is 0–1; RKColor(230,…) would be ~230× too bright.
  RKColor _adsorptionSurfaceFrontSideSpecularColor = RKColor::fromRgb(230, 230, 230);
  double _adsorptionSurfaceFrontSideDiffuseIntensity = 1.0;
  double _adsorptionSurfaceFrontSideAmbientIntensity = 0.0;
  double _adsorptionSurfaceFrontSideSpecularIntensity = 0.5;
  double _adsorptionSurfaceFrontSideShininess = 4.0;

  bool _adsorptionSurfaceBackSideHDR = true;
  double _adsorptionSurfaceBackSideHDRExposure = 2.0;
  RKColor _adsorptionSurfaceBackSideAmbientColor = RKColor::fromRgb(0, 0, 0, 255);
  RKColor _adsorptionSurfaceBackSideDiffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _adsorptionSurfaceBackSideSpecularColor = RKColor::fromRgb(230, 230, 230, 255);
  double _adsorptionSurfaceBackSideDiffuseIntensity = 1.0;
  double _adsorptionSurfaceBackSideAmbientIntensity = 0.0;
  double _adsorptionSurfaceBackSideSpecularIntensity = 0.5;
  double _adsorptionSurfaceBackSideShininess = 4.0;

  double3 _selectionCOMTranslation = double3(0.0, 0.0, 0.0);
  int _selectionRotationIndex = 0;
  double3x3 _selectionBodyFixedBasis = double3x3();

  StructureType _structureType = StructureType::framework;
  ProbeMolecule _frameworkProbeMolecule = ProbeMolecule::nitrogen;
  RKString _structureMaterialType = RKString("Unspecified");
  double _structureMass = 0.0;
  double _structureDensity = 0.0;
  double _structureHeliumVoidFraction = 0.0;
  double _structureSpecificVolume = 0.0;
  double _structureAccessiblePoreVolume = 0.0;
  double _structureVolumetricNitrogenSurfaceArea = 0.0;
  double _structureGravimetricNitrogenSurfaceArea = 0.0;
  int64_t _structureNumberOfChannelSystems = 0;
  int64_t _structureNumberOfInaccessiblePockets = 0;
  int64_t _structureDimensionalityOfPoreSystem = 0;
  double _structureLargestCavityDiameter = 0.0;
  double _structureRestrictingPoreLimitingDiameter = 0.0;
  double _structureLargestCavityDiameterAlongAViablePath = 0.0;

  // ================================================================
  // remove in future version
  //RKString _authorFirstName = RKString("");
  //RKString _authorMiddleName = RKString("");
  //RKString _authorLastName = RKString("");
  //RKString _authorOrchidID = RKString("");
  //RKString _authorResearcherID = RKString("");
  //RKString _authorAffiliationUniversityName = RKString("");
  //RKString _authorAffiliationFacultyName = RKString("");
  //RKString _authorAffiliationInstituteName = RKString("");
  //RKString _authorAffiliationCityName = RKString("");
  //RKString _authorAffiliationCountryName = RKString("Netherlands");
  //
  //QDate _creationDate = QDate().currentDate();
  // ================================================================

  RKString _creationTemperature = RKString();
  TemperatureScale _creationTemperatureScale = TemperatureScale::Kelvin;
  RKString _creationPressure = RKString("");
  PressureScale _creationPressureScale = PressureScale::Pascal;
  CreationMethod  _creationMethod = CreationMethod::unknown;
  UnitCellRelaxationMethod _creationUnitCellRelaxationMethod = UnitCellRelaxationMethod::unknown;
  RKString _creationAtomicPositionsSoftwarePackage = RKString("");
  IonsRelaxationAlgorithm _creationAtomicPositionsIonsRelaxationAlgorithm = IonsRelaxationAlgorithm::unknown;
  IonsRelaxationCheck _creationAtomicPositionsIonsRelaxationCheck = IonsRelaxationCheck::unknown;
  RKString _creationAtomicPositionsForcefield = RKString("");
  RKString _creationAtomicPositionsForcefieldDetails = RKString("");
  RKString _creationAtomicChargesSoftwarePackage = RKString("");
  RKString _creationAtomicChargesAlgorithms = RKString("");
  RKString _creationAtomicChargesForcefield = RKString("");
  RKString _creationAtomicChargesForcefieldDetails = RKString();

  RKString _experimentalMeasurementRadiation = RKString("");
  RKString _experimentalMeasurementWaveLength = RKString("");
  RKString _experimentalMeasurementThetaMin = RKString("");
  RKString _experimentalMeasurementThetaMax = RKString("");
  RKString _experimentalMeasurementIndexLimitsHmin = RKString("");
  RKString _experimentalMeasurementIndexLimitsHmax = RKString("");
  RKString _experimentalMeasurementIndexLimitsKmin = RKString("");
  RKString _experimentalMeasurementIndexLimitsKmax = RKString("");
  RKString _experimentalMeasurementIndexLimitsLmin = RKString("");
  RKString _experimentalMeasurementIndexLimitsLmax = RKString("");
  RKString _experimentalMeasurementNumberOfSymmetryIndependentReflections = RKString("");
  RKString _experimentalMeasurementSoftware = RKString("");
  RKString _experimentalMeasurementRefinementDetails = RKString("");
  RKString _experimentalMeasurementGoodnessOfFit = RKString("");
  RKString _experimentalMeasurementRFactorGt = RKString("");
  RKString _experimentalMeasurementRFactorAll = RKString();

  RKString _chemicalFormulaMoiety = RKString("");
  RKString _chemicalFormulaSum = RKString("");
  RKString _chemicalNameSystematic = RKString("");
  int64_t _cellFormulaUnitsZ = 0;

  RKString _citationArticleTitle = RKString("");
  RKString _citationJournalTitle = RKString("");
  RKString _citationAuthors = RKString("");
  RKString _citationJournalVolume = RKString("");
  RKString _citationJournalNumber = RKString("");
  RKString _citationJournalPageNumbers = RKString("");
  RKString _citationDOI = RKString("");
  RKDate _citationPublicationDate = RKDate::currentDate();
  RKString _citationDatebaseCodes = RKString();
};

struct FrameConsumer
{
  virtual void setFrame(std::shared_ptr<Structure> structure) = 0;
  virtual ~FrameConsumer() = 0;
};
