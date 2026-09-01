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
#include <set>
#include <vector>
#include <mathkit.h>
#include <foundationkit.h>
#include "rkdate.h"
#include "rkimage.h"
#include <symmetrykit.h>
#include "rkfontatlas.h"
#include "rklight.h"
#include "rkcamera.h"
#include "rkglobalaxes.h"
#include "rklocalaxes.h"
#include "rkribbonmesh.h"
#include "rkrendersettings.h"
#include <algorithm>

// forward declaration
struct RKInPerInstanceAttributesAtoms;
struct RKInPerInstanceAttributesBonds;
struct RKInPerInstanceAttributesText;

class RKRenderObject
{
public:
  virtual ~RKRenderObject() = 0;

  virtual RKString displayName() const = 0;
  virtual bool isVisible() const = 0;

  // Ribbon on screen and no atom: ambient occlusion then skips atom occluders so hidden atoms do
  // not shade the ribbon. False when atoms are shown with a ribbon, so those atoms cast again.
  virtual bool isPresentedAsRibbonOnly() const { return false; }

  virtual  simd_quatd orientation() const = 0;
  virtual  double3 origin() const = 0;

  virtual std::shared_ptr<SKCell> cell() const = 0;
};

class RKRenderLocalAxesSource
{
public:
  virtual ~RKRenderLocalAxesSource() = 0;

  virtual RKLocalAxes &renderLocalAxes() = 0;
};

class RKRenderUnitCellSource
{
public:
  virtual ~RKRenderUnitCellSource() = 0;

  virtual bool drawUnitCell() const = 0;

  virtual double unitCellScaleFactor() const = 0;
  virtual RKColor unitCellDiffuseColor() const = 0;
  virtual double unitCellDiffuseIntensity() const = 0;

  virtual std::vector<RKInPerInstanceAttributesAtoms> renderUnitCellSpheres() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderUnitCellCylinders() const = 0;
};

class RKRenderAtomSource
{
public:
  virtual ~RKRenderAtomSource() = 0;

  virtual bool drawAtoms() const = 0;

  /// Which depth cues the atoms and their bonds are drawn with. Not pure, unlike the rest of this:
  /// an object with no notion of them takes none, and there is nothing for it to decide.
  virtual RKEdgeCueing atomEdgeCueing() const { return RKEdgeCueing::off; }

  virtual RKColor atomAmbientColor() const = 0;
  virtual RKColor atomDiffuseColor() const = 0;
  virtual RKColor atomSpecularColor() const = 0;
  virtual double atomAmbientIntensity() const = 0;
  virtual double atomDiffuseIntensity() const = 0;
  virtual double atomSpecularIntensity() const = 0;
  virtual double atomShininess() const = 0;

  virtual double atomHue() const = 0;
  virtual double atomSaturation() const = 0;
  virtual double atomValue() const = 0;

  virtual bool colorAtomsWithBondColor() const = 0;
  virtual double atomScaleFactor() const = 0;
  virtual bool atomAmbientOcclusion() const = 0;
  virtual int atomAmbientOcclusionPatchNumber() const = 0;
  virtual int atomAmbientOcclusionPatchSize() const = 0;
  virtual int atomAmbientOcclusionTextureSize() const = 0;
  virtual void setAtomAmbientOcclusionPatchNumber(int) = 0;  // CHECK
  virtual void setAtomAmbientOcclusionPatchSize(int) = 0;    // CHECK
  virtual void setAtomAmbientOcclusionTextureSize(int) = 0;  // CHECK

  virtual bool atomHDR() const = 0;
  virtual double atomHDRExposure() const = 0;
  virtual bool clipAtomsAtUnitCell() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderAtoms() const = 0;

  virtual std::vector<RKInPerInstanceAttributesText> atomTextData(RKFontAtlas *fontAtlas) const = 0;  // CHECK
  virtual std::vector<RKInPerInstanceAttributesText> renderTextData() const = 0;
  virtual RKTextType renderTextType() const = 0;
  virtual RKString renderTextFont() const = 0;
  virtual RKTextAlignment renderTextAlignment() const = 0;
  virtual RKTextStyle renderTextStyle() const = 0;
  virtual RKColor renderTextColor() const = 0;
  virtual double renderTextScaling() const = 0;
  virtual double3 renderTextOffset() const = 0;

  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedAtoms() const = 0;
  virtual RKSelectionStyle atomSelectionStyle() const = 0;
  virtual double atomSelectionStripesDensity() const = 0;
  virtual double atomSelectionStripesFrequency() const = 0;
  virtual double atomSelectionWorleyNoise3DFrequency() const = 0;
  virtual double atomSelectionWorleyNoise3DJitter() const = 0;
  virtual double atomSelectionIntensity() const = 0;
  virtual double atomSelectionScaling() const = 0;
};

class RKRenderBondSource
{
public:
  virtual ~RKRenderBondSource() = 0;

  virtual bool drawBonds() const = 0;
  virtual int numberOfInternalBonds() const = 0;
  virtual int numberOfExternalBonds() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderInternalBonds() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderExternalBonds() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderSelectedInternalBonds() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderSelectedExternalBonds() const = 0;

  //virtual bool bondAmbientOcclusion() const = 0;
  virtual RKColor bondAmbientColor() const = 0;
  virtual RKColor bondDiffuseColor() const = 0;
  virtual RKColor bondSpecularColor() const = 0;
  virtual double bondAmbientIntensity() const = 0;
  virtual double bondDiffuseIntensity() const = 0;
  virtual double bondSpecularIntensity() const = 0;
  virtual double bondShininess() const = 0;

  virtual bool isUnity() const = 0;
  virtual bool hasExternalBonds() const = 0;

  virtual double bondScaleFactor() const = 0;
  virtual RKBondColorMode bondColorMode() const = 0;

  virtual bool bondHDR() const = 0;
  virtual double bondHDRExposure() const = 0;

  virtual bool clipBondsAtUnitCell() const = 0;

  virtual double bondHue() const = 0;
  virtual double bondSaturation() const = 0;
  virtual double bondValue() const = 0;

  virtual RKSelectionStyle bondSelectionStyle() const = 0;
  virtual double bondSelectionStripesDensity() const = 0;
  virtual double bondSelectionStripesFrequency() const = 0;
  virtual double bondSelectionWorleyNoise3DFrequency() const = 0;
  virtual double bondSelectionWorleyNoise3DJitter() const = 0;
  virtual double bondSelectionIntensity() const = 0;
  virtual double bondSelectionScaling() const = 0;
};

// A structure that can be drawn as a protein or nucleic-acid cartoon. The mesh is built from the
// backbone and held by the structure, so the renderer asks for the vertices and the ranges rather
// than for the residues. Selection, picking and ambient occlusion are not part of this yet; the mesh
// already carries the ranges those will need.
class RKRenderRibbonSource
{
public:
  virtual ~RKRenderRibbonSource() = 0;

  virtual bool drawRibbon() const = 0;
  virtual double ribbonScaleFactor() const = 0;

  /// The ribbon's own cues, kept apart from the atoms' so that a cartoon can be cued over plain
  /// atoms, or the other way about. See atomEdgeCueing for why this is not pure.
  virtual RKEdgeCueing ribbonEdgeCueing() const { return RKEdgeCueing::off; }

  // By reference: a large protein's ribbon runs to hundreds of megabytes, and the renderer only
  // reads it to fill the GPU buffers.
  virtual const std::vector<RKRibbonVertex> &renderRibbonVertices() const = 0;
  virtual const std::vector<uint32_t> &renderRibbonIndices() const = 0;
  virtual int ribbonNumberOfVertices() const = 0;
  virtual int ribbonNumberOfIndices() const = 0;
  virtual std::vector<RKRibbonChainDrawRange> ribbonChainDrawRanges() const = 0;
  virtual std::vector<RKRibbonChainDrawRange> ribbonSegmentDrawRanges() const = 0;
  virtual std::vector<RKRibbonChainDrawRange> ribbonResidueDrawRanges() const = 0;
  virtual int ribbonNumberOfChains() const = 0;
  virtual int ribbonNumberOfRings() const = 0;
  virtual int ribbonMaxSplineSampleCount() const = 0;

  // A protein whose atom tree carries the residue or segment hierarchy is drawn range by range, so
  // hiding a residue or a segment in the tree hides that piece of the ribbon. Without the hierarchy
  // there is nothing finer than a chain to hide, and the whole chain is drawn.
  virtual bool ribbonUsesResidueVisibility() const = 0;
  virtual bool ribbonUsesSegmentVisibility() const = 0;
  virtual bool isRibbonResidueDrawRangeVisible(int rangeIndex) const = 0;
  virtual bool isRibbonSegmentDrawRangeVisible(int rangeIndex) const = 0;

  // The ranges left to draw once the hidden residues or segments are merged out, at whichever level
  // the tree can be hidden by. The structure holds on to the answer until its atoms or its tree
  // change, so a frame that hides nothing new costs nothing to encode.
  virtual const std::vector<RKRibbonChainDrawRange> &ribbonDrawRangesForEncoding() const = 0;

  // The ranges the selection pass draws over: a selected secondary-structure segment answers with a
  // segment range, a selected residue or one of its atoms with a residue range, so a selection made
  // at either level is drawn at that level.
  virtual std::set<int> renderSelectedRibbonSegmentDrawRangeIndices() const = 0;
  virtual std::set<int> renderSelectedRibbonResidueDrawRangeIndices() const = 0;

  virtual float3 ribbonCoilColor() const = 0;
  virtual float3 ribbonHelixColor() const = 0;
  virtual float3 ribbonSheetColor() const = 0;

  virtual bool ribbonHDR() const = 0;
  virtual double ribbonHDRExposure() const = 0;
  virtual double ribbonHue() const = 0;
  virtual double ribbonSaturation() const = 0;
  virtual double ribbonValue() const = 0;
  virtual bool ribbonAmbientOcclusion() const = 0;

  virtual RKColor ribbonAmbientColor() const = 0;
  virtual RKColor ribbonDiffuseColor() const = 0;
  virtual RKColor ribbonSpecularColor() const = 0;
  virtual double ribbonAmbientIntensity() const = 0;
  virtual double ribbonDiffuseIntensity() const = 0;
  virtual double ribbonSpecularIntensity() const = 0;
  virtual double ribbonShininess() const = 0;

  virtual void rebuildBackbone() = 0;
  virtual void rebuildRibbonMesh() = 0;
};

class RKRenderVolumetricDataSource
{
public:
  virtual ~RKRenderVolumetricDataSource() = 0;

  virtual bool drawAdsorptionSurface() const = 0;

  virtual int3 dimensions() const = 0;
  virtual std::vector<float> gridData()  = 0;
  virtual std::vector<float4> gridValueAndGradientData()  = 0;
  virtual bool isImmutable() const = 0;

  virtual RKEnergySurfaceType adsorptionSurfaceRenderingMethod() const = 0;
  virtual RKPredefinedVolumeRenderingTransferFunction adsorptionVolumeTransferFunction() const = 0;
  virtual double adsorptionVolumeStepLength() const = 0;

  virtual double adsorptionSurfaceOpacity() const = 0;
  virtual double adsorptionTransparencyThreshold() const = 0;
  virtual double adsorptionSurfaceIsoValue() const = 0;

  virtual double adsorptionSurfaceHue() const = 0;
  virtual double adsorptionSurfaceSaturation() const = 0;
  virtual double adsorptionSurfaceValue() const = 0;

  virtual bool adsorptionSurfaceFrontSideHDR() const = 0;
  virtual double adsorptionSurfaceFrontSideHDRExposure() const = 0;
  virtual RKColor adsorptionSurfaceFrontSideAmbientColor() const = 0;
  virtual RKColor adsorptionSurfaceFrontSideDiffuseColor() const = 0;
  virtual RKColor adsorptionSurfaceFrontSideSpecularColor() const = 0;
  virtual double adsorptionSurfaceFrontSideDiffuseIntensity() const = 0;
  virtual double adsorptionSurfaceFrontSideAmbientIntensity() const = 0;
  virtual double adsorptionSurfaceFrontSideSpecularIntensity() const = 0;
  virtual double adsorptionSurfaceFrontSideShininess() const = 0;

  virtual bool adsorptionSurfaceBackSideHDR() const = 0;
  virtual double adsorptionSurfaceBackSideHDRExposure() const = 0;
  virtual RKColor adsorptionSurfaceBackSideAmbientColor() const = 0;
  virtual RKColor adsorptionSurfaceBackSideDiffuseColor() const = 0;
  virtual RKColor adsorptionSurfaceBackSideSpecularColor() const = 0;
  virtual double adsorptionSurfaceBackSideDiffuseIntensity() const = 0;
  virtual double adsorptionSurfaceBackSideAmbientIntensity() const = 0;
  virtual double adsorptionSurfaceBackSideSpecularIntensity() const = 0;
  virtual double adsorptionSurfaceBackSideShininess() const = 0;
};

class RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderPrimitiveObjectsSource() = 0;

  virtual bool drawAtoms() const = 0;

  virtual RKSelectionStyle primitiveSelectionStyle() const = 0;
  virtual double primitiveSelectionStripesDensity() const = 0;
  virtual double primitiveSelectionStripesFrequency() const = 0;
  virtual double primitiveSelectionWorleyNoise3DFrequency() const = 0;
  virtual double primitiveSelectionWorleyNoise3DJitter() const = 0;
  virtual double primitiveSelectionIntensity() const = 0;
  virtual double primitiveSelectionScaling() const = 0;

  virtual double3x3 primitiveTransformationMatrix() const = 0;
  virtual simd_quatd primitiveOrientation() const = 0;

  virtual double primitiveOpacity() const = 0;
  virtual bool primitiveIsCapped() const = 0;
  virtual bool primitiveIsFractional() const = 0;
  virtual int primitiveNumberOfSides() const = 0;
  virtual double primitiveThickness() const = 0;

  virtual double primitiveHue() const = 0;
  virtual double primitiveSaturation() const = 0;
  virtual double primitiveValue() const = 0;

  virtual bool primitiveFrontSideHDR() const = 0;
  virtual double primitiveFrontSideHDRExposure() const = 0;
  virtual RKColor primitiveFrontSideAmbientColor() const = 0;
  virtual RKColor primitiveFrontSideDiffuseColor() const = 0;
  virtual RKColor primitiveFrontSideSpecularColor() const = 0;
  virtual double primitiveFrontSideDiffuseIntensity() const = 0;
  virtual double primitiveFrontSideAmbientIntensity() const = 0;
  virtual double primitiveFrontSideSpecularIntensity() const = 0;
  virtual double primitiveFrontSideShininess() const = 0;

  virtual bool primitiveBackSideHDR() const = 0;
  virtual double primitiveBackSideHDRExposure() const = 0;
  virtual RKColor primitiveBackSideAmbientColor() const = 0;
  virtual RKColor primitiveBackSideDiffuseColor() const = 0;
  virtual RKColor primitiveBackSideSpecularColor() const = 0;
  virtual double primitiveBackSideDiffuseIntensity() const = 0;
  virtual double primitiveBackSideAmbientIntensity() const = 0;
  virtual double primitiveBackSideSpecularIntensity() const = 0;
  virtual double primitiveBackSideShininess() const = 0;
};

class RKRenderCrystalPrimitiveEllipsoidObjectsSource //: public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderCrystalPrimitiveEllipsoidObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderCrystalPrimitiveEllipsoidObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedCrystalPrimitiveEllipsoidObjects() const = 0;
};

class RKRenderCrystalPrimitiveCylinderObjectsSource //: public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderCrystalPrimitiveCylinderObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderCrystalPrimitiveCylinderObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedCrystalPrimitiveCylinderObjects() const = 0;
};

class RKRenderCrystalPrimitivePolygonalPrimsObjectsSource //: public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderCrystalPrimitivePolygonalPrimsObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderCrystalPrimitivePolygonalPrismObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedCrystalPrimitivePolygonalPrismObjects() const = 0;
};

class RKRenderPrimitiveEllipsoidObjectsSource //: public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderPrimitiveEllipsoidObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderPrimitiveEllipsoidObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedPrimitiveEllipsoidObjects() const = 0;
};

class RKRenderPrimitiveCylinderObjectsSource // : public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderPrimitiveCylinderObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderPrimitiveCylinderObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedPrimitiveCylinderObjects() const = 0;
};

class RKRenderPrimitivePolygonalPrimsObjectsSource  //: public RKRenderPrimitiveObjectsSource
{
public:
  virtual ~RKRenderPrimitivePolygonalPrimsObjectsSource() = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderPrimitivePolygonalPrismObjects() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderSelectedPrimitivePolygonalPrismObjects() const = 0;
};

class RKRenderDataSource
{
public:
  virtual ~RKRenderDataSource() = 0;
  virtual std::shared_ptr<RKCamera> camera() const = 0;
  virtual std::vector<size_t> numberOfScenes() const = 0;
  virtual int numberOfMovies(int sceneIndex) const = 0;

  virtual std::vector<std::shared_ptr<RKLight>>& renderLights() = 0;

  /// Light arriving from the environment as a whole, which is why it sits beside the lights rather
  /// than inside one of them: a rig is free to turn every lamp off without the scene going black.
  /// Acts as a multiplier on each material's own ambient, which the representation style owns.
  virtual double renderSceneAmbientIntensity() const = 0;
  virtual RKColor renderSceneAmbientColor() const = 0;

  /// How much of the ambient occlusion darkens the direct light as well as the ambient term, where 0
  /// is physically correct and 1 reproduces the older "Fancy" look. Applies to both the rasterizer
  /// and the path tracer, and to both interactive frames and exports: it is purely a look, with no
  /// bearing on render cost, so there is no reason for it to differ between them.
  virtual double renderAmbientOcclusionStrength() const = 0;

  /// Whether the renderer traces the scene to find out which lights actually reach each surface, so
  /// that geometry standing in the way casts a shadow. Only meaningful for a rig whose lights sit off
  /// the camera axis: a light at the eye can never be blocked from anything the eye can see.
  ///
  /// Read by the rasterizer. The path tracer casts its own shadow rays and ignores it.
  virtual bool renderShadows() const = 0;

  /// Whether shadows are asked for and there is a light able to cast one. A light at the eye can
  /// never be blocked from anything the eye can see, so under a rig whose lights all sit on the
  /// camera axis the pass that traces them is skipped rather than traced for nothing. This is what
  /// leaves the setting free to be on by default.
  bool wantsShadows()
  {
    if (!renderShadows())
      return false;
    for (const std::shared_ptr<RKLight> &light : renderLights())
    {
      if (light && light->castsShadows())
        return true;
    }
    return false;
  }

  /// How exported pictures and movies are rendered. Unlike the interactive settings, which describe
  /// what the current machine can keep up with, these are choices about the output and so travel with
  /// the document.
  virtual bool renderPictureRayTracing() const = 0;
  virtual int renderPictureSampleCount() const = 0;
  virtual int renderPictureMaximumBounces() const = 0;

  /// The stored export settings, clamped to what the tracer can be asked for. The stored values come
  /// from editable fields and from documents written by other versions, so neither is trusted here.
  int picturePathTracerSampleCount() const
  {
    return std::clamp(renderPictureSampleCount(), 1, RKRenderSettings::maximumSupportedPictureSamples);
  }
  int picturePathTracerMaximumBounces() const
  {
    return std::clamp(renderPictureMaximumBounces(), 0, RKRenderSettings::maximumSupportedPictureBounces);
  }

  virtual std::vector<RKInPerInstanceAttributesAtoms> renderMeasurementPoints() const = 0;
  virtual std::vector<RKRenderObject> renderMeasurementStructure() const = 0;

  virtual SKBoundingBox renderBoundingBox() const = 0;

  virtual bool hasSelectedObjects() const = 0;

  virtual RKBackgroundType renderBackgroundType() const = 0;
  virtual RKColor renderBackgroundColor() const = 0;
  virtual const RKImage renderBackgroundCachedImage() = 0;

  virtual int renderImageNumberOfPixels() const = 0;
  virtual double renderImagePhysicalSizeInInches() const = 0;

  virtual bool showBoundingBox() const = 0;
  virtual std::vector<RKInPerInstanceAttributesAtoms> renderBoundingBoxSpheres() const = 0;
  virtual std::vector<RKInPerInstanceAttributesBonds> renderBoundingBoxCylinders() const = 0;

  virtual std::shared_ptr<RKGlobalAxes> axes() const = 0;
};

class RKRenderViewController
{
 public:
  virtual ~RKRenderViewController() = 0;

  virtual const std::vector<RKString>& logData() const = 0;
  virtual void redraw() = 0;
  virtual void redrawWithQuality(RKRenderQuality quality) = 0;

  virtual void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures) = 0;
  virtual void setRenderDataSource(std::shared_ptr<RKRenderDataSource> source) = 0;
  virtual void reloadData() = 0;
  virtual void reloadData(RKRenderQuality ambientOcclusionQuality) = 0;
  virtual void reloadAmbientOcclusionData() = 0;
  virtual void reloadRenderData() = 0;
  virtual void reloadSelectionData() = 0;
  virtual void reloadRenderMeasurePointsData() = 0;
  virtual void reloadBoundingBoxData() = 0;
  virtual void reloadGlobalAxesData() = 0;
  virtual void reloadStructureUniforms() = 0;

  virtual void reloadBackgroundImage() = 0;

  virtual void invalidateCachedAmbientOcclusionTextures(std::vector<std::shared_ptr<RKRenderObject>> structures) = 0;
  virtual void invalidateCachedIsosurfaces(std::vector<std::shared_ptr<RKRenderObject>> structures) = 0;
 // virtual void computeHeliumVoidFraction(std::vector<std::shared_ptr<RKRenderObject>> structures) = 0;
 // virtual void computeNitrogenSurfaceArea(std::vector<std::shared_ptr<RKRenderObject>> structures) = 0;

  virtual void updateTransformUniforms() = 0;
  virtual void updateStructureUniforms() = 0;
  virtual void updateIsosurfaceUniforms() = 0;
  virtual void updateLightUniforms() = 0;
  virtual void updateGlobalAxesUniforms() = 0;

  virtual void updateVertexArrays() = 0;

  virtual RKImage renderSceneToImage(int width, int height, RKRenderQuality quality) = 0;

  virtual std::array<int,4> pickTexture(int x, int y, int width, int height) = 0;
};
