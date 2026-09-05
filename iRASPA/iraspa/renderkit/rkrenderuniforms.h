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

#include "rkcolor.h"
#include <mathkit.h>
#include "rkcolor.h"
#include <array>
#include <memory>

// Uniform                                  binding point
// RKTransformationUniforms                 0
// RKStructureUniforms                      1
// RKIsosurfaceUniforms                     2
// RKLightsUniforms                         3
//
// RKGlobalAxesUniforms                     5
// RKStructureUniforms (ambient occlusion)  6
// ShadowUniformBlock  (ambient occlusion)  7

class RKRenderObject;
class RKRenderDataSource;

enum class RKEnergySurfaceType: int64_t
{
  isoSurface = 0,
  volumeRendering = 1,
  wellSurface = 2,
  // The merged-well filament: the thin tube along channel axes too narrow for the probe's contact
  // sheet, where the adsorbate is enclosed and sits on that axis. Drawn as its own rendering method
  // so it can be superimposed on a copy of the structure showing the well surface, each with its
  // own material.
  wellSurfaceOverlay = 3,
  /// Union of probe-inflated force-field collision spheres (Cocoa geometricSurface = 4).
  geometricSurface = 4,
  /// Union of probe-inflated Bondi van der Waals spheres (Cocoa vdwGeometricSurface = 5).
  vdwGeometricSurface = 5,
  /// Mixed-selection sentinel; kept after the Cocoa raw values.
  multiple_values = 6
};

/// Whether the surface is a triangle mesh drawn by the isosurface pipeline, the well surface being
/// the iso-surface mapped onto the locus of energy minima rather than a separate construction.
inline bool isTriangulated(RKEnergySurfaceType type)
{
  switch (type)
  {
    case RKEnergySurfaceType::isoSurface:
    case RKEnergySurfaceType::wellSurface:
    case RKEnergySurfaceType::wellSurfaceOverlay:
      return true;
    case RKEnergySurfaceType::volumeRendering:
    case RKEnergySurfaceType::geometricSurface:
    case RKEnergySurfaceType::vdwGeometricSurface:
    case RKEnergySurfaceType::multiple_values:
      return false;
  }
  return false;
}

/// Spherical-patch geometric constructions (not a grid iso-surface).
inline bool isGeometricSurface(RKEnergySurfaceType type)
{
  return type == RKEnergySurfaceType::geometricSurface ||
         type == RKEnergySurfaceType::vdwGeometricSurface;
}

/// GPU instance of one geometric-surface patch. Layout matches Cocoa RKGeometricSurfacePatchInstance.
struct RKGeometricSurfacePatchInstance
{
  float4 position = float4();
  float4 scale = float4();
  float4 cellOrigin = float4();
  uint32_t firstClip = 0;
  uint32_t clipCount = 0;
  uint32_t clipToCell = 0;
  uint32_t pad1 = 0;

  RKGeometricSurfacePatchInstance() = default;
  RKGeometricSurfacePatchInstance(float3 positionValue, float radius, float3 cellOriginValue,
                                  uint32_t firstClipValue, uint32_t clipCountValue, bool clipToCellValue)
      : position(float4(positionValue.x, positionValue.y, positionValue.z, 1.0f)),
        scale(float4(radius, radius, radius, radius)),
        cellOrigin(float4(cellOriginValue.x, cellOriginValue.y, cellOriginValue.z, 0.0f)),
        firstClip(firstClipValue),
        clipCount(clipCountValue),
        clipToCell(clipToCellValue ? 1u : 0u)
  {
  }
};

/// GPU clip sphere. Layout matches Cocoa RKGeometricSurfaceClip.
struct RKGeometricSurfaceClip
{
  float4 sphere = float4();

  RKGeometricSurfaceClip() = default;
  RKGeometricSurfaceClip(float3 center, float radius)
      : sphere(float4(center.x, center.y, center.z, radius))
  {
  }
};

/// Whether the surface is a level set of the analytic well field rather than of the energy grid.
inline bool isWellSurface(RKEnergySurfaceType type)
{
  return type == RKEnergySurfaceType::wellSurface || type == RKEnergySurfaceType::wellSurfaceOverlay;
}

enum class RKPredefinedVolumeRenderingTransferFunction: int64_t
{
  RASPA_PES = 0,
  CoolWarmDiverging = 1,
  Xray = 2,
  Gray = 3,
  Rainbow = 4,
  Turbo = 5,
  Gnuplot = 6,
  Spectral = 7,
  Cool = 8,
  Viridis = 9,
  Plasma = 10,
  Inferno = 11,
  Magma = 12,
  Cividis = 13,
  Spring = 14,
  Summer = 15,
  Autumn = 16,
  Winter = 17,
  Reds = 18,
  Blues = 19,
  Greens = 20,
  Purples = 21,
  Oranges = 22,
  multiple_values = 23
};

enum class RKBackgroundType: int64_t
{
  color = 0, linearGradient = 1, radialGradient = 2, image = 3
};

enum class RKBondColorMode: int64_t
{
  uniform = 0, split = 1, smoothed_split = 2, multiple_values = 3
};

enum class RKRenderQuality: int64_t
{
  low = 0, medium = 1, high = 2, picture = 3
};

/// How a frame is drawn: by filling triangles, or by following light through the scene.
enum class RKRenderMode: int64_t
{
  rasterization = 0, rayTracing = 1
};

enum class RKImageQuality: int64_t
{
  rgb_8_bits = 0, rgb_16_bits = 1, cmyk_8_bits = 2, cmyk_16_bits = 3
};

enum class RKImageDPI: int64_t
{
  dpi_72 = 0, dpi_75 = 1, dpi_150 = 2, dpi_300 = 3, dpi_600 = 4, dpi_1200 = 5
};

enum class RKImageDimensions: int64_t
{
  physical = 0, pixels = 1
};

enum class RKImageUnits: int64_t
{
  inch = 0, cm = 1
};

enum class RKSelectionStyle: int64_t
{
  None = 0, WorleyNoise3D = 1, striped = 2, glow = 3, multiple_values = 4
};

/// The edge cues of Tarini, Cignoni and Montani: a dark contour around each sphere and a light halo
/// behind it, which is what makes a dense structure readable. Cocoa keeps this apart from the
/// representation style so that it can be set on any of them, and stores it as these raw values.
enum class RKEdgeCueing: int64_t
{
  off = 0, contours = 1, halos = 2, contoursAndHalos = 3, multiple_values = 4
};

namespace RKEdgeCueingParameters
{
  /// How the cues are drawn, as opposed to which of them are: contour strength, contour width in
  /// pixels, halo strength and halo radius in pixels.
  ///
  /// One setting for the whole image, while which cues appear is decided for each pixel from the
  /// structure that drew it. Two structures asking for contours therefore get the same contours,
  /// which is what keeps a picture looking drawn by one hand.
  inline constexpr float contourStrength = 0.9f;
  inline constexpr float contourWidthInPixels = 3.0f;
  inline constexpr float haloStrength = 0.5f;
  inline constexpr float haloRadiusInPixels = 4.0f;

  /// Both cues judge a depth step against a reference, and what counts as a large step is a question
  /// about this structure rather than a fixed number of Angstrom: a step that separates two strands
  /// of a small protein would pass unnoticed inside a zeolite. Tying the reference to the size of the
  /// scene lets one setting suit either. Fractions of the scene's bounding sphere.
  inline constexpr double contourDepthFraction = 0.04;
  inline constexpr double haloDepthFraction = 0.15;

  /// How the molecular passes record their cueing in the scene's stencil, for the pass that draws the
  /// cues to read back. The low bits hold an RKEdgeCueing raw value and the high bit says the pixel
  /// belongs to a structure at all, whatever it asked for: a pixel that asked for nothing still takes
  /// the halo of an atom in front of it, while the background and the unit cell do not.
  inline constexpr unsigned int stencilModeMask = 0x03;
  inline constexpr unsigned int stencilCueableBit = 0x80;

  /// What a molecular pass sets as its stencil reference. Zero for anything that is not a structure,
  /// which is what takes the tag off a pixel such geometry covers.
  inline constexpr unsigned int stencilValue(RKEdgeCueing cueing)
  {
    const unsigned int mode = static_cast<unsigned int>(cueing) & stencilModeMask;
    return stencilCueableBit | mode;
  }
}

enum class RKTextStyle: int64_t
{
  flatBillboard = 0, multiple_values = 1
};

enum class RKTextEffect: int64_t
{
  none = 0, glow = 1, pulsate = 2, squiggle = 3, multiple_values = 4
};

enum class RKTextType: int64_t
{
  none = 0, displayName = 1, identifier = 2, chemicalElement = 3, forceFieldType = 4, position = 5, charge = 6, multiple_values = 7
};

enum class RKTextAlignment: int64_t
{
  center = 0, left = 1, right = 2, top = 3, bottom = 4, topLeft = 5, topRight = 6, bottomLeft = 7, bottomRight = 8, multiple_values = 9
};

struct RKVertex
{
  RKVertex():position(float4()),normal(float4()), st(float2()) {}
  RKVertex(float4 pos, float4 norm, float2 c): position(pos), normal(norm), st(c) {}

  float4 position;
  float4 normal;
  float2 st;
  float2 pad = float2();
};

struct RKPrimitiveVertex
{
  RKPrimitiveVertex(float4 pos, float4 norm, float4 c, float2 uv): position(pos), normal(norm), color(c), st(uv) {}
  float4 position;
  float4 normal;
  float4 color;
  float2 st;
  float2 pad;
};

struct RKTextVertex
{
  float4 position  = float4();
  float4 st = float4();
};

struct RKBondVertex
{
  float4 position1;
  float4 position2;
};

struct RKInPerInstanceAttributesText
{
  float4 position = float4();
  float4 scale = float4();
  float4 vertexCoordinatesData = float4();
  float4 textureCoordinatesData = float4();

  RKInPerInstanceAttributesText(float4 p, float4 s, float4 v, float4 t): position(p), scale(s), vertexCoordinatesData(v), textureCoordinatesData(t) {}
};

struct RKInPerInstanceAttributesAtoms
{
  float4 position = float4();
  float4 ambient = float4();
  float4 diffuse = float4();
  float4 specular = float4();
  float4 scale = float4();
  int32_t tag = int32_t();
  RKInPerInstanceAttributesAtoms(float4 p, float4 a, float4 d, float4 s, float4 sc, int32_t tag): position(p), ambient(a), diffuse(d), specular(s), scale(sc), tag(tag) {}
};

struct RKInPerInstanceAttributesBonds
{
  float4 position1 = float4();
  float4 position2 = float4();
  float4 color1 = float4();
  float4 color2 = float4();
  float4 scale = float4();
  int32_t tag  = int32_t();
  int32_t type  = int32_t();
  RKInPerInstanceAttributesBonds() {}
  RKInPerInstanceAttributesBonds(float4 pos1, float4 pos2, float4 c1, float4 c2, float4 sc, int32_t tag, int32_t type): position1(pos1), position2(pos2), color1(c1), color2(c2), scale(sc), tag(tag), type(type) {}
};

// Note: must be aligned at vector-length (16-bytes boundaries, 4 Floats of 4 bytes)
// current number of bytes: 512 bytes

struct RKTransformationUniforms
{
  float4x4 projectionMatrix = float4x4(1.0);
  float4x4 modelViewMatrix = float4x4(1.0);
  float4x4 mvpMatrix = float4x4();
  float4x4 shadowMatrix = float4x4();
  float4x4 projectionMatrixInverse = float4x4();
  float4x4 viewMatrixInverse = float4x4();
  float4x4 normalMatrix = float4x4();

  float4x4 axesProjectionMatrix = float4x4();
  float4x4 axesViewMatrix = float4x4();
  float4x4 axesMvpMatrix = float4x4();
  float4x4 padMatrix = float4x4();

  // moved 'numberOfMultiSamplePoints' to here (for downsampling when no structures are present)
  float4 cameraPosition = float4();
  /// Edge cueing, after Tarini et al. section 5: x is the contour line strength, y its greatest width
  /// in pixels, z the halo strength and w the halo radius in pixels. A zero strength turns that cue
  /// off, which is what the renderer leaves here when no structure asked for either.
  float4 edgeCueing = float4();
  float numberOfMultiSamplePoints = 8.0;
  // Size of the shadow mask the ray tracer writes, which the raster shaders index by pixel. A
  // width of zero means no mask was traced and every light reaches every surface, which is what
  // the renderer leaves here whenever ray tracing is off or unavailable.
  float shadowMaskWidth = 0.0;
  float shadowMaskHeight = 0.0;
  /// Where the Metal build carries a flag saying whether the cues read the tracer's depth or the
  /// rasterizer's. This back end compiles the cue pass once for each source instead, so the slot is
  /// only held open to keep the block in step with the one the Metal shaders are handed.
  float pad9 = 0.0;
  float bloomLevel = 1.0;
  float bloomPulse = 1.0;
  /// Depth steps, in scene units, at which a contour line reaches its full width and a halo its full
  /// darkness. Both are set from the size of the scene, so that one setting suits any structure.
  float edgeCueingContourDepth = 0.0;
  float edgeCueingHaloDepth = 0.0;

  RKTransformationUniforms() {};
  RKTransformationUniforms(double4x4 projectionMatrix, double4x4 modelViewMatrix, double4x4 modelMatrix, double4x4 viewMatrix, double4x4 axesProjectionMatrix, double4x4 axesModelViewMatrix, bool isOrthographic, double bloomLevel, double bloomPulse, int multiSampling);
};

// IMPORTANT: must be aligned on 256-bytes boundaries
// current number of bytes: 512 bytes
struct RKStructureUniforms
{
  int32_t sceneIdentifier = 0;
  int32_t MovieIdentifier = 0;
  float atomScaleFactor = 1.0f;
  int32_t numberOfMultiSamplePoints = 8;

  int32_t ambientOcclusion = false;
  int32_t ambientOcclusionPatchNumber = 64;
  float ambientOcclusionPatchSize = 16.0f;
  float ambientOcclusionInverseTextureSize = float(1.0/1024.0);

  float atomHue = 1.0f;
  float atomSaturation = 1.0f;
  float atomValue = 1.0f;
  float pad111 = 1.0f;

  int32_t atomHDR = true;
  float atomHDRExposure = 1.5f;
  float atomSelectionIntensity = 0.5f;
  int32_t clipAtomsAtUnitCell = false;

  float4 atomAmbient = float4(1.0, 1.0, 1.0, 1.0);
  float4 atomDiffuse = float4(1.0, 1.0, 1.0, 1.0);
  float4 atomSpecular = float4(1.0, 1.0, 1.0, 1.0);
  float atomShininess = 4.0;

  float bondHue = 0.0f;
  float bondSaturation = 0.0f;
  float bondValue = 0.0f;

  //----------------------------------------  128 bytes boundary

  int32_t bondHDR = true;
  float bondHDRExposure = 1.5f;
  float bondSelectionIntensity = 1.0f;
  int32_t clipBondsAtUnitCell = false;

  float4 bondAmbientColor = float4(1.0, 1.0, 1.0, 1.0);
  float4 bondDiffuseColor = float4(1.0, 1.0, 1.0, 1.0);
  float4 bondSpecularColor = float4(1.0, 1.0, 1.0, 1.0);

  float bondShininess = 4.0f;
  float bondScaling = 1.0f;
  int32_t bondColorMode = 0;

  float unitCellScaling = 1.0f;
  float4 unitCellDiffuseColor = float4(1.0, 1.0, 1.0, 1.0);

  float4 clipPlaneLeft = float4(1.0, 1.0, 1.0, 1.0);
  float4 clipPlaneRight = float4(1.0, 1.0, 1.0, 1.0);

  //----------------------------------------  256 bytes boundary

  float4 clipPlaneTop = float4(1.0, 1.0, 1.0, 1.0);
  float4 clipPlaneBottom = float4(1.0, 1.0, 1.0, 1.0);

  float4 clipPlaneFront = float4(1.0, 1.0, 1.0, 1.0);
  float4 clipPlaneBack = float4(1.0, 1.0, 1.0, 1.0);

  float4x4 modelMatrix = float4x4(0.0f);

  //----------------------------------------  384 bytes boundary

  float4x4 inverseModelMatrix = float4x4(double4x4());
  float4x4 boxMatrix = float4x4();

  //----------------------------------------  512 bytes boundary

  float4x4 inverseBoxMatrix = float4x4(double4x4());
  float atomSelectionStripesDensity = 0.25f;
  float atomSelectionStripesFrequency = 12.0f;
  float atomSelectionWorleyNoise3DFrequency = 2.0f;
  float atomSelectionWorleyNoise3DJitter = 0.0f;

  float4 atomAnnotationTextDisplacement = float4();
  float4 atomAnnotationTextColor = float4(0.0,0.0,0.0,1.0);
  float atomAnnotationTextScaling = 1.0f;
  float atomSelectionScaling = 1.0f;
  float bondSelectionScaling = 1.25f;
  int32_t colorAtomsWithBondColor = false;

  //----------------------------------------  640 bytes boundary

  float4x4 primitiveTransformationMatrix = float4x4();
  float4x4 primitiveTransformationNormalMatrix = float4x4();

  //----------------------------------------  768 bytes boundary

  float4 primitiveAmbientFrontSide = float4(0.0,0.0,0.0,1.0);
  float4 primitiveDiffuseFrontSide = float4(0.0,0.0,0.0,1.0);
  float4 primitiveSpecularFrontSide = float4(0.0,0.0,0.0,1.0);
  int32_t primitiveFrontSideHDR = true;
  float primitiveFrontSideHDRExposure = 1.5f;
  float primitiveOpacity = 1.0f;
  float primitiveShininessFrontSide = 4.0f;

  float4 primitiveAmbientBackSide = float4(0.0,0.0,0.0,1.0);
  float4 primitiveDiffuseBackSide = float4(0.0,0.0,0.0,1.0);
  float4 primitiveSpecularBackSide = float4(0.0,0.0,0.0,1.0);
  int32_t primitiveBackSideHDR = true;
  float primitiveBackSideHDRExposure = 1.5f;
  // RKEdgeCueing as a float, for the ribbons of this structure. Read by the path tracer's resolve
  // kernel, which has to record what the rasterizer records in its stencil and has no stencil.
  float edgeCueingRibbons = 0.0f;
  float primitiveShininessBackSide = 4.0f;

  //----------------------------------------  896 bytes boundary

  float bondSelectionStripesDensity = 0.25f;
  float bondSelectionStripesFrequency = 12.0f;
  float bondSelectionWorleyNoise3DFrequency = 2.0f;
  float bondSelectionWorleyNoise3DJitter = 1.0f;

  float primitiveSelectionStripesDensity = 0.25f;
  float primitiveSelectionStripesFrequency = 12.0f;
  float primitiveSelectionWorleyNoise3DFrequency = 2.0f;
  float primitiveSelectionWorleyNoise3DJitter = 1.0f;

  float primitiveSelectionScaling = 1.01f;
  float primitiveSelectionIntensity = 0.8f;
  float pad7 = 0.0f;
  /// RKEdgeCueing as a float, for the atoms and bonds of this structure. See `edgeCueingRibbons`.
  float edgeCueingAtoms = 0.0f;

  float primitiveHue = 1.0f;
  float primitiveSaturation = 1.0f;
  float primitiveValue = 1.0f;
  // How far occlusion leans towards darkening the direct terms as well as the ambient one. 0 is
  // physically correct, 1 reproduces the "Fancy" look. One grading for the whole scene, so it is set
  // by the renderer off the project rather than by the per-structure constructors below.
  float ambientOcclusionStrength = 0.0f;

  float4 localAxisPosition = float4(0.0f,0.0f,0.0f,0.0f);
  float4 numberOfReplicas = float4(0.0f,0.0f,0.0f,0.0f);

  // The ribbon block sits at the same offsets as the Qt version, so the two describe one layout and
  // the shader constants can be read across. rkrenderuniforms.cpp asserts the offsets.
  float4 ribbonCoilColor = float4(0.0f, 1.0f, 0.0f, 1.0f);
  float4 ribbonHelixColor = float4(1.0f, 0.0f, 1.0f, 1.0f);
  float4 ribbonSheetColor = float4(1.0f, 1.0f, 0.0f, 1.0f);
  int32_t ribbonHDR = 1;
  float ribbonHDRExposure = 1.5f;
  float ribbonHue = 1.0f;
  float ribbonSaturation = 1.0f;
  float ribbonValue = 1.0f;
  int32_t ribbonAmbientOcclusion = 0;
  float padRibbon1 = 0.0f;
  float ribbonShininess = 6.0f;
  float padRibbon2 = 0.0f;
  float padRibbon3 = 0.0f;
  float padRibbon4 = 0.0f;
  float padRibbon5 = 0.0f;
  float4 ribbonAmbientColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float4 ribbonDiffuseColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float4 ribbonSpecularColor = float4(1.0f, 1.0f, 1.0f, 1.0f);

  // A constant buffer view is placed on a 256-byte boundary, so the struct is filled out to one.
  float4 padTail1 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail2 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail3 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail4 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail5 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail6 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail7 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail8 = float4(0.0f,0.0f,0.0f,0.0f);
  float4 padTail9 = float4(0.0f,0.0f,0.0f,0.0f);

  RKStructureUniforms() {}
  RKStructureUniforms(size_t sceneIdentifier, size_t movieIdentifier, std::shared_ptr<RKRenderObject> structure);
  RKStructureUniforms(size_t sceneIdentifier, size_t movieIdentifier, std::shared_ptr<RKRenderObject> structure, double4x4 inverseModelMatrix);
};

// IMPORTANT: must be aligned on 256-bytes boundaries
// current number of bytes: 256 bytes
struct RKShadowUniforms
{
  float4x4 projectionMatrix = float4x4();
  float4x4 viewMatrix = float4x4();
  float4x4 shadowMatrix = float4x4();
  float4x4 normalMatrix = float4x4();

  RKShadowUniforms();
  RKShadowUniforms(double4x4 projectionMatrix, double4x4 viewMatrix, double4x4 modelMatrix);
};

struct RKIsosurfaceUniforms
{
  float4x4 unitCellMatrix = float4x4();
  float4x4 inverseUnitCellMatrix = float4x4();
  float4x4 unitCellNormalMatrix = float4x4();

  float4x4 boxMatrix = float4x4();
  float4x4 inverseBoxMatrix = float4x4();

  //----------------------------------------  128 bytes boundary

  float4 ambientFrontSide = float4(0.0f, 0.0f, 0.0f, 1.0f);
  float4 diffuseFrontSide = float4(0.588235f, 0.670588f, 0.729412f, 1.0f);
  float4 specularFrontSide = float4(1.0f, 1.0f, 1.0f, 1.0f);
  int32_t frontHDR = true;
  float frontHDRExposure = 1.5;
  float transparencyThreshold = 0.0;
  float shininessFrontSide = 4.0;

  float4 ambientBackSide = float4(0.0f, 0.0f, 0.0f, 1.0f);
  float4 diffuseBackSide = float4(0.588235f, 0.670588f, 0.729412f, 1.0f);
  float4 specularBackSide = float4(0.9f, 0.9f, 0.9f, 1.0f);
  int32_t backHDR = true;
  float backHDRExposure = 1.5;
  int32_t transferFunctionIndex;
  float shininessBackSide = 4.0;

  //----------------------------------------  256 bytes boundary

  float hue;
  float saturation;
  float value;
  float stepLength = 0.001;

  float4 scaleToEncompassing;
  float4 pad5;
  float4 pad6;

  //----------------------------------------  384 bytes boundary

  RKIsosurfaceUniforms();
  RKIsosurfaceUniforms(std::shared_ptr<RKRenderObject> structure);
};

/// The material of the translucent spheres that show the blocking pockets of a structure.
///
/// One material per structure rather than per pocket: the pockets of a structure describe the same
/// inaccessible pore space and are read from a single file. The spheres are drawn two-sided, and the
/// far wall takes this material with the normal flipped, so there is no separate inside material.
struct RKBlockingPocketUniforms
{
  /// The opacity of a single sphere face. A sphere is drawn back-face then front-face, so a pocket
  /// covers a fragment twice, and the value is deliberately low enough to keep the framework visible
  /// through it.
  static constexpr double opacity = 0.3;

  float4 ambient = float4(0.0f, 0.0f, 0.0f, 1.0f);
  float4 diffuse = float4(0.2f, 0.65f, 0.85f, 1.0f);
  float4 specular = float4(0.92f, 0.92f, 0.92f, 1.0f);
  int32_t hdr = 1;
  float hdrExposure = 1.5f;
  float shininess = 4.0f;
  float pad0 = 0.0f;

  //----------------------------------------  64 bytes boundary

  RKBlockingPocketUniforms();
  RKBlockingPocketUniforms(std::shared_ptr<RKRenderObject> structure);
};

struct RKLightUniform
{
  float4 position = float4(0.0, 0.0, 100.0, 0.0);  // w=0 directional light, w=1.0 positional light
  float4 ambient = float4(1.0, 1.0, 1.0, 1.0);
  float4 diffuse = float4(1.0, 1.0, 1.0, 1.0);
  float4 specular = float4(1.0, 1.0, 1.0, 1.0);

  float4 spotDirection = float4(1.0, 1.0, 1.0, 0.0);
  float constantAttenuation = 1.0;
  float linearAttenuation = 1.0;
  float quadraticAttenuation = 1.0;
  float spotCutoff = 1.0;

  float spotExponent = 1.0;
  float shininess = 4.0;
  // RKLightType as a float: 0 directional, 1 point, 2 spot. Occupies what were pad slots, so the
  // 128-byte stride is unchanged.
  float lightType = 0.0;
  // Non-zero when the light contributes. Only the camera light in slot 0 is on by default.
  float enabled = 0.0;

  float pad3 = 0.0;
  float pad4 = 0.0;
  float pad5 = 0.0;
  float pad6 = 0.0;

  RKLightUniform();
  RKLightUniform(std::shared_ptr<RKRenderDataSource> project, int lightIndex);
};

struct RKLightsUniforms
{
  // Must match the length of the lights array in DirectXUniformStringLiterals'
  // LightUniformBlockStringLiteral, which declares the block itself.
  static constexpr size_t numberOfLights = 8;

  RKLightsUniforms() {}
  RKLightsUniforms(std::shared_ptr<RKRenderDataSource> project);

  std::array<RKLightUniform, numberOfLights> lights{};

  // Stands in for the light arriving from the whole environment, so it belongs to the scene rather
  // than to any one lamp: enabling or disabling a light leaves the ambient floor untouched.
  // accumulateLighting reads this once; each light's ambient slot stays unused (layout only).
  float4 sceneAmbient = float4(1.0f, 1.0f, 1.0f, 1.0f);
};

struct RKGlobalAxesUniforms
{
  float4 axesBackgroundColor = float4(0.8f,0.8f,0.8f,0.05f);
  float3x4 textColor = float3x4(float4(0.0f, 0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.0f, 1.0f));
  float3x4 textDisplacement = float3x4(float4(0.0f, 0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.0f, 1.0f));
  int32_t axesBackGroundStyle = 1;
  float axesScale = 5.0f;
  float centerScale = 0.0f;
  float textOffset = 0.0f;
  float4 textScale = float4(1.0f, 1.0f, 1.0f, 1.0f);

  RKGlobalAxesUniforms(std::shared_ptr<RKRenderDataSource> project);
};
