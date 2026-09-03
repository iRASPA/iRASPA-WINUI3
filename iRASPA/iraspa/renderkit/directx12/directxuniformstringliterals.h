/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <string>

class DirectXUniformStringLiterals
{
public:
  DirectXUniformStringLiterals() = default;

  /// The tag saying which edge cues the surface at a pixel asked for, in step with
  /// RKEdgeCueingParameters in rkrenderuniforms.h. Written by the molecular raster passes into the
  /// stencil and by the path tracer's resolve kernel into a buffer, and read by the cue pass out of
  /// whichever of the two the frame was drawn with.
  inline static const std::string EdgeCueingStencilTagStringLiteral = R"foo(
#define EDGE_CUEING_STENCIL_MODE_MASK 0x03
#define EDGE_CUEING_STENCIL_CUEABLE_BIT 0x80
)foo";

  // Nested struct keeps C++ memcpy layout and avoids cross-cbuffer name clashes.
  inline static const std::string FrameUniformBlockStringLiteral = R"foo(
struct FrameUniformData
{
  float4x4 projectionMatrix;
  float4x4 viewMatrix;
  float4x4 mvpMatrix;
  float4x4 shadowMatrix;
  float4x4 projectionMatrixInverse;
  float4x4 viewMatrixInverse;
  float4x4 normalMatrix;

  float4x4 axesProjectionMatrix;
  float4x4 axesViewMatrix;
  float4x4 axesMvpMatrix;
  float4x4 padMatrix;

  float4 cameraPosition;
  float4 edgeCueing;
  float numberOfMultiSamplePoints;
  float shadowMaskWidth;
  float shadowMaskHeight;
  float pad9;
  float bloomLevel;
  float bloomPulse;
  float edgeCueingContourDepth;
  float edgeCueingHaloDepth;
};

cbuffer FrameUniformBlock : register(b0)
{
  FrameUniformData frameUniforms;
};
)foo";

  // The struct on its own. The raster passes reach one structure at a time and take it as the
  // constant buffer below; the ray-tracing kernels reach every structure at once, indexed by the
  // instance they hit, and take it as a structured buffer instead. Both layouts agree because every
  // member here falls in a group of four scalars or is a float4, so nothing is padded differently
  // between the two.
  inline static const std::string StructureUniformStructStringLiteral = R"foo(
struct StructureUniformData
{
  int sceneIdentifier;
  int MovieIdentifier;
  float atomScaleFactor;
  int numberOfMultiSamplePoints;

  int ambientOcclusion;
  int ambientOcclusionPatchNumber;
  float ambientOcclusionPatchSize;
  float ambientOcclusionInverseTextureSize;

  float atomHue;
  float atomSaturation;
  float atomValue;
  float pad111;

  int atomHDR;
  float atomHDRExposure;
  float atomSelectionIntensity;
  int clipAtomsAtUnitCell;

  float4 atomAmbientColor;
  float4 atomDiffuseColor;
  float4 atomSpecularColor;
  float atomShininess;

  float bondHue;
  float bondSaturation;
  float bondValue;

  int bondHDR;
  float bondHDRExposure;
  float bondSelectionIntensity;
  int clipBondsAtUnitCell;

  float4 bondAmbientColor;
  float4 bondDiffuseColor;
  float4 bondSpecularColor;

  float bondShininess;
  float bondScaling;
  int bondColorMode;
  float unitCellScaling;
  float4 unitCellColor;

  float4 clipPlaneLeft;
  float4 clipPlaneRight;

  float4 clipPlaneTop;
  float4 clipPlaneBottom;
  float4 clipPlaneFront;
  float4 clipPlaneBack;

  float4x4 modelMatrix;

  float4x4 inverseModelMatrix;
  float4x4 boxMatrix;

  float4x4 inverseBoxMatrix;
  float atomSelectionStripesDensity;
  float atomSelectionStripesFrequency;
  float atomSelectionWorleyNoise3DFrequency;
  float atomSelectionWorleyNoise3DJitter;

  float4 atomAnnotationTextDisplacement;
  float4 atomAnnotationTextColor;
  float atomAnnotationTextScaling;
  float atomSelectionScaling;
  float bondSelectionScaling;
  int colorAtomsWithBondColor;

  float4x4 transformationMatrix;
  float4x4 transformationNormalMatrix;

  float4 primitiveAmbientFrontSide;
  float4 primitiveDiffuseFrontSide;
  float4 primitiveSpecularFrontSide;
  int primitiveFrontSideHDR;
  float primitiveFrontSideHDRExposure;
  float primitiveOpacity;
  float primitiveShininessFrontSide;

  float4 primitiveAmbientBackSide;
  float4 primitiveDiffuseBackSide;
  float4 primitiveSpecularBackSide;
  int primitiveBackSideHDR;
  float primitiveBackSideHDRExposure;
  // RKEdgeCueing as a float. The raster passes tag the stencil instead; these are for the tracer,
  // whose resolve kernel writes the same tag into a buffer.
  float edgeCueingRibbons;
  float primitiveShininessBackSide;

  float bondSelectionStripesDensity;
  float bondSelectionStripesFrequency;
  float bondSelectionWorleyNoise3DFrequency;
  float bondSelectionWorleyNoise3DJitter;

  float primitiveSelectionStripesDensity;
  float primitiveSelectionStripesFrequency;
  float primitiveSelectionWorleyNoise3DFrequency;
  float primitiveSelectionWorleyNoise3DJitter;

  float primitiveSelectionScaling;
  float primitiveSelectionIntensity;
  float pad7;
  float edgeCueingAtoms;

  float primitiveHue;
  float primitiveSaturation;
  float primitiveValue;
  // How far occlusion leans towards darkening the direct terms as well as the ambient one. 0 is
  // physically correct, 1 reproduces the "Fancy" look.
  float ambientOcclusionStrength;

  float4 localAxisPosition;
  float4 numberOfReplicas;

  float4 ribbonCoilColor;
  float4 ribbonHelixColor;
  float4 ribbonSheetColor;
  int ribbonHDR;
  float ribbonHDRExposure;
  float ribbonHue;
  float ribbonSaturation;
  float ribbonValue;
  int ribbonAmbientOcclusion;
  float padRibbon1;
  float ribbonShininess;
  float padRibbon2;
  float padRibbon3;
  float padRibbon4;
  float padRibbon5;
  float4 ribbonAmbientColor;
  float4 ribbonDiffuseColor;
  float4 ribbonSpecularColor;

  float4 padTail1;
  float4 padTail2;
  float4 padTail3;
  float4 padTail4;
  float4 padTail5;
  float4 padTail6;
  float4 padTail7;
  float4 padTail8;
  float4 padTail9;
};
)foo";

  inline static const std::string StructureUniformBlockStringLiteral =
      StructureUniformStructStringLiteral + R"foo(
cbuffer StructureUniformBlock : register(b1)
{
  StructureUniformData structureUniforms;
};
)foo";

  inline static const std::string ShadowUniformBlockStringLiteral = R"foo(
struct ShadowUniformData
{
  float4x4 projectionMatrix;
  float4x4 viewMatrix;
  float4x4 shadowMatrix;
  float4x4 normalMatrix;
};

cbuffer ShadowUniformBlock : register(b2)
{
  ShadowUniformData shadowUniforms;
};
)foo";

  // Matches RKIsosurfaceUniforms / OpenGL binding 2. Root signature binds this at root index 4.
  inline static const std::string IsosurfaceUniformBlockStringLiteral = R"foo(
struct IsosurfaceUniformData
{
  float4x4 unitCellMatrix;
  float4x4 inverseUnitCellMatrix;
  float4x4 unitCellNormalMatrix;

  float4x4 boxMatrix;
  float4x4 inverseBoxMatrix;

  float4 ambientFrontSide;
  float4 diffuseFrontSide;
  float4 specularFrontSide;
  int frontHDR;
  float frontHDRExposure;
  float transparencyThreshold;
  float shininessFrontSide;

  float4 ambientBackSide;
  float4 diffuseBackSide;
  float4 specularBackSide;
  int backHDR;
  float backHDRExposure;
  int transferFunctionIndex;
  float shininessBackSide;

  float hue;
  float saturation;
  float value;
  float stepLength;

  float4 scaleToEncompassing;
  float4 pad5;
  float4 pad6;
};

cbuffer IsosurfaceUniformBlock : register(b2)
{
  IsosurfaceUniformData isosurfaceUniforms;
};
)foo";

  inline static const std::string LightUniformBlockStringLiteral = R"foo(
struct Light
{
  float4 position;
  float4 ambient;
  float4 diffuse;
  float4 specular;

  float4 spotDirection;

  float constantAttenuation;
  float linearAttenuation;
  float quadraticAttenuation;
  float spotCutoff;

  float spotExponent;
  float shininess;
  // RKLightType as a float: 0 directional, 1 point, 2 spot.
  float lightType;
  // Non-zero when the light contributes. Only the camera light in slot 0 is on by default.
  float enabled;
  float pad3;
  float pad4;
  float pad5;
  float pad6;
};

// The array length must match RKLightsUniforms::numberOfLights, which mirrors this block by hand.
struct LightsUniformData
{
  Light lights[8];
  // Ambient light for the scene as a whole rather than for any one lamp.
  float4 sceneAmbient;
};

cbuffer LightsUniformBlock : register(b3)
{
  LightsUniformData lightUniforms;
};
)foo";

  // Shading summed over the whole light rig, and the shadow mask that gates it. Include after the
  // frame and light blocks, which this reads. Every surface that is lit goes through
  // accumulateLighting, so a light added to the rig reaches all of them at once; the vertex stages
  // hand on material colours and the pixel stage finishes the shading, since a per-light sum can
  // no longer be folded into a single interpolated colour.
  inline static const std::string LightingStringLiteral = R"foo(
// One bit per light, set when that light reaches the surface at this pixel. Written by the ray
// tracer's shadow-mask kernel: a raster pass has no way of knowing what stands between a surface
// and a light, so with an off-axis rig it would otherwise light faces the tracer leaves in shadow.
//
// A buffer rather than a texture because the renderer swaps descriptor heaps between passes. A
// root shader-resource view is bound by address and survives that, where a table-bound texture
// would have to be present in every one of those heaps.
StructuredBuffer<uint> shadowMaskBuffer : register(t1);

// Every light reaching the surface: the mask value that leaves shading untouched. Eight bits for
// the eight lights of LightsUniformData.
static const uint allLightsVisible = 0xFFu;

// Guide geometry — the unit cell, the bounding box, the axes — has no material to set an ambient
// level with, so it takes this share of the colour it is drawn in. It sits in the same range as the
// 0.2 an atom's representation style asks for, and it is what keeps guides visible when every light
// is switched off, which the scene ambient makes a reasonable thing to do.
static const float guideGeometryAmbient = 0.2;

/// The light-visibility bits at this fragment, from the pixel position the rasterizer assigned it.
/// Reports every light lit when no mask has been traced, which is what makes the whole shadow path
/// optional.
uint shadowMaskAtFragment(float4 windowPosition)
{
  int width = int(frameUniforms.shadowMaskWidth);
  int height = int(frameUniforms.shadowMaskHeight);
  if (width <= 0 || height <= 0)
    return allLightsVisible;

  // A mask traced at a different size than the pass being shaded would otherwise read out of
  // bounds; clamping costs nothing and keeps the two independent.
  int2 pixel = clamp(int2(windowPosition.xy), int2(0, 0), int2(width - 1, height - 1));
  return shadowMaskBuffer[pixel.y * width + pixel.x];
}

struct LightingWeights
{
  float3 ambient;
  float3 diffuse;
  float3 specular;
};

/// Ambient, diffuse and specular summed over the enabled lights, each weighted by its distance
/// falloff, its spotlight cone and whether `lightVisibility` says it reaches this point. The
/// caller multiplies these by its own material colours.
LightingWeights accumulateLighting(float3 N, float3 V, float4 eyePosition, float materialShininess,
                                   uint lightVisibility)
{
  LightingWeights weights;

  // ambient belongs to the scene, so it is set once here rather than summed over the lights
  weights.ambient = lightUniforms.sceneAmbient.xyz;
  weights.diffuse = float3(0.0, 0.0, 0.0);
  weights.specular = float3(0.0, 0.0, 0.0);

  for (int i = 0; i < 8; i++)
  {
    if (lightUniforms.lights[i].enabled < 0.5)
      continue;

    // in shadow for this light: no direct light of any kind reaches the point
    if ((lightVisibility & (1u << uint(i))) == 0u)
      continue;

    // w selects the meaning of position: a direction for a directional light, a location otherwise
    float4 lightPosition = lightUniforms.lights[i].position;
    float3 toLight = (lightPosition - eyePosition * lightPosition.w).xyz;
    float distanceToLight = length(toLight);
    float3 L = (distanceToLight > 0.0) ? toLight / distanceToLight : float3(0.0, 0.0, 1.0);

    float attenuation = 1.0;
    if (lightPosition.w > 0.5)
    {
      attenuation = 1.0 / max(lightUniforms.lights[i].constantAttenuation +
                              lightUniforms.lights[i].linearAttenuation * distanceToLight +
                              lightUniforms.lights[i].quadraticAttenuation * distanceToLight * distanceToLight,
                              1.0e-4);

      if (lightUniforms.lights[i].lightType > 1.5) // spot
      {
        float3 spotAxis = normalize(lightUniforms.lights[i].spotDirection.xyz);
        float spotCosine = dot(-L, spotAxis);
        float cutoffCosine = cos(0.01745329252 * clamp(lightUniforms.lights[i].spotCutoff, 0.0, 180.0));
        // A cutoff wider than a right angle admits directions the cosine is negative for, which
        // pow is not defined at, so it is clamped rather than left to the cutoff test alone.
        attenuation *= (spotCosine < cutoffCosine)
                           ? 0.0
                           : pow(max(spotCosine, 0.0), max(lightUniforms.lights[i].spotExponent, 0.0));
      }
    }

    float3 R = reflect(-L, N);
    float specularFactor = pow(max(dot(R, V), 0.0), lightUniforms.lights[i].shininess + materialShininess);

    weights.diffuse += attenuation * max(dot(N, L), 0.0) * lightUniforms.lights[i].diffuse.xyz;
    weights.specular += attenuation * specularFactor * lightUniforms.lights[i].specular.xyz;
  }

  return weights;
}

/// For the passes a shadow does not apply to. The selection overlays are the main ones: a marking
/// on the model rather than a part of it, so neither the shadow mask nor the baked occlusion map
/// touches them.
LightingWeights accumulateLighting(float3 N, float3 V, float4 eyePosition, float materialShininess)
{
  return accumulateLighting(N, V, eyePosition, materialShininess, allLightsVisible);
}
)foo";

  inline static const std::string RGBHSVStringLiteral = R"foo(
float3 rgb2hsv(float3 c)
{
  float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
  float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));

  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv2rgb(float3 c)
{
  float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}
)foo";

  // Port of OpenGLWorleyNoise3DStringLiteral (cellular noise for selection overlays).
  inline static const std::string WorleyNoise3DStringLiteral = R"foo(
float3 mod289(float3 x)
{
  return x - floor(x * (1.0 / 289.0)) * 289.0;
}

float3 permute(float3 x)
{
  return mod289(((x * 34.0) + 1.0) * x);
}

float3 mod3(float3 x, float y)
{
  return x - y * floor(x / y);
}

float2 cellular3D(float3 P, float jitter)
{
  static const float K = 0.142857142857;
  static const float Ko = 0.428571428571;
  static const float K2 = 0.020408163265306;
  static const float Kz = 0.166666666667;
  static const float Kzo = 0.416666666667;

  float3 Pi = mod3(floor(P), 289.0);
  float3 Pf = frac(P) - 0.5;

  float3 Pfx = Pf.x + float3(1.0, 0.0, -1.0);
  float3 Pfy = Pf.y + float3(1.0, 0.0, -1.0);
  float3 Pfz = Pf.z + float3(1.0, 0.0, -1.0);

  float3 p = permute(Pi.x + float3(-1.0, 0.0, 1.0));
  float3 p1 = permute(p + Pi.y - 1.0);
  float3 p2 = permute(p + Pi.y);
  float3 p3 = permute(p + Pi.y + 1.0);

  float3 p11 = permute(p1 + Pi.z - 1.0);
  float3 p12 = permute(p1 + Pi.z);
  float3 p13 = permute(p1 + Pi.z + 1.0);

  float3 p21 = permute(p2 + Pi.z - 1.0);
  float3 p22 = permute(p2 + Pi.z);
  float3 p23 = permute(p2 + Pi.z + 1.0);

  float3 p31 = permute(p3 + Pi.z - 1.0);
  float3 p32 = permute(p3 + Pi.z);
  float3 p33 = permute(p3 + Pi.z + 1.0);

  float3 ox11 = frac(p11 * K) - Ko;
  float3 oy11 = mod3(floor(p11 * K), 7.0) * K - Ko;
  float3 oz11 = floor(p11 * K2) * Kz - Kzo;

  float3 ox12 = frac(p12 * K) - Ko;
  float3 oy12 = mod3(floor(p12 * K), 7.0) * K - Ko;
  float3 oz12 = floor(p12 * K2) * Kz - Kzo;

  float3 ox13 = frac(p13 * K) - Ko;
  float3 oy13 = mod3(floor(p13 * K), 7.0) * K - Ko;
  float3 oz13 = floor(p13 * K2) * Kz - Kzo;

  float3 ox21 = frac(p21 * K) - Ko;
  float3 oy21 = mod3(floor(p21 * K), 7.0) * K - Ko;
  float3 oz21 = floor(p21 * K2) * Kz - Kzo;

  float3 ox22 = frac(p22 * K) - Ko;
  float3 oy22 = mod3(floor(p22 * K), 7.0) * K - Ko;
  float3 oz22 = floor(p22 * K2) * Kz - Kzo;

  float3 ox23 = frac(p23 * K) - Ko;
  float3 oy23 = mod3(floor(p23 * K), 7.0) * K - Ko;
  float3 oz23 = floor(p23 * K2) * Kz - Kzo;

  float3 ox31 = frac(p31 * K) - Ko;
  float3 oy31 = mod3(floor(p31 * K), 7.0) * K - Ko;
  float3 oz31 = floor(p31 * K2) * Kz - Kzo;

  float3 ox32 = frac(p32 * K) - Ko;
  float3 oy32 = mod3(floor(p32 * K), 7.0) * K - Ko;
  float3 oz32 = floor(p32 * K2) * Kz - Kzo;

  float3 ox33 = frac(p33 * K) - Ko;
  float3 oy33 = mod3(floor(p33 * K), 7.0) * K - Ko;
  float3 oz33 = floor(p33 * K2) * Kz - Kzo;

  float3 dx11 = Pfx + jitter * ox11;
  float3 dy11 = Pfy.x + jitter * oy11;
  float3 dz11 = Pfz.x + jitter * oz11;

  float3 dx12 = Pfx + jitter * ox12;
  float3 dy12 = Pfy.x + jitter * oy12;
  float3 dz12 = Pfz.y + jitter * oz12;

  float3 dx13 = Pfx + jitter * ox13;
  float3 dy13 = Pfy.x + jitter * oy13;
  float3 dz13 = Pfz.z + jitter * oz13;

  float3 dx21 = Pfx + jitter * ox21;
  float3 dy21 = Pfy.y + jitter * oy21;
  float3 dz21 = Pfz.x + jitter * oz21;

  float3 dx22 = Pfx + jitter * ox22;
  float3 dy22 = Pfy.y + jitter * oy22;
  float3 dz22 = Pfz.y + jitter * oz22;

  float3 dx23 = Pfx + jitter * ox23;
  float3 dy23 = Pfy.y + jitter * oy23;
  float3 dz23 = Pfz.z + jitter * oz23;

  float3 dx31 = Pfx + jitter * ox31;
  float3 dy31 = Pfy.z + jitter * oy31;
  float3 dz31 = Pfz.x + jitter * oz31;

  float3 dx32 = Pfx + jitter * ox32;
  float3 dy32 = Pfy.z + jitter * oy32;
  float3 dz32 = Pfz.y + jitter * oz32;

  float3 dx33 = Pfx + jitter * ox33;
  float3 dy33 = Pfy.z + jitter * oy33;
  float3 dz33 = Pfz.z + jitter * oz33;

  float3 d11 = dx11 * dx11 + dy11 * dy11 + dz11 * dz11;
  float3 d12 = dx12 * dx12 + dy12 * dy12 + dz12 * dz12;
  float3 d13 = dx13 * dx13 + dy13 * dy13 + dz13 * dz13;
  float3 d21 = dx21 * dx21 + dy21 * dy21 + dz21 * dz21;
  float3 d22 = dx22 * dx22 + dy22 * dy22 + dz22 * dz22;
  float3 d23 = dx23 * dx23 + dy23 * dy23 + dz23 * dz23;
  float3 d31 = dx31 * dx31 + dy31 * dy31 + dz31 * dz31;
  float3 d32 = dx32 * dx32 + dy32 * dy32 + dz32 * dz32;
  float3 d33 = dx33 * dx33 + dy33 * dy33 + dz33 * dz33;

  float3 d1a = min(d11, d12);
  d12 = max(d11, d12);
  d11 = min(d1a, d13);
  d13 = max(d1a, d13);
  d12 = min(d12, d13);
  float3 d2a = min(d21, d22);
  d22 = max(d21, d22);
  d21 = min(d2a, d23);
  d23 = max(d2a, d23);
  d22 = min(d22, d23);
  float3 d3a = min(d31, d32);
  d32 = max(d31, d32);
  d31 = min(d3a, d33);
  d33 = max(d3a, d33);
  d32 = min(d32, d33);
  float3 da = min(d11, d21);
  d21 = max(d11, d21);
  d11 = min(da, d31);
  d31 = max(da, d31);
  d11.xy = (d11.x < d11.y) ? d11.xy : d11.yx;
  d11.xz = (d11.x < d11.z) ? d11.xz : d11.zx;
  d12 = min(d12, d21);
  d12 = min(d12, d22);
  d12 = min(d12, d31);
  d12 = min(d12, d32);
  d11.yz = min(d11.yz, d12.xy);
  d11.y = min(d11.y, d12.z);
  d11.y = min(d11.y, d11.z);
  return sqrt(d11.xy);
}
)foo";

  inline static const std::string BlockingPocketUniformBlockStringLiteral = R"foo(
struct BlockingPocketUniformData
{
  float4 ambient;
  float4 diffuse;
  float4 specular;
  int hdr;
  float hdrExposure;
  float shininess;
  float pad0;
};

cbuffer BlockingPocketUniformBlock : register(b4)
{
  BlockingPocketUniformData blockingPocketUniforms;
};
)foo";

  inline static const std::string GlobalAxesUniformBlockStringLiteral = R"foo(
struct GlobalAxesUniformData
{
  float4 axesBackgroundColor;
  float4 textColor[3];
  float4 textDisplacement[3];
  int axesBackGroundStyle;
  float axesScale;
  float centerScale;
  float textOffset;
  float4 textScale;
};

cbuffer GlobalAxesUniformBlock : register(b5)
{
  GlobalAxesUniformData globalAxesUniforms;
};
)foo";
};
