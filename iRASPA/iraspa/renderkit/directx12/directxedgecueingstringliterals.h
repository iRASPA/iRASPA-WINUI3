/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <string>

#include "directxuniformstringliterals.h"

/// The pass that draws the edge cues, kept apart from the class that runs it so that tools\dumpshaders
/// can put it through the compiler without a device. A mistake in here is otherwise not a build error
/// but a pipeline that fails to create, and cues that never appear with nothing said about why.
class DirectXEdgeCueingStringLiterals
{
public:
  /// Where the surface a pixel shows is to be read from. The rasterizer leaves it in its own depth
  /// buffer and in the stencil it carries alongside; the path tracer, which writes no stencil and
  /// whose molecular geometry never went through a raster pass, leaves both in buffers of its own.
  enum class Source
  {
    rasterizedMultisampled,
    rasterized,
    traced
  };

  static std::string vertexShaderSource()
  {
    return R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = input.position;
  return o;
}
)foo";
  }

  /// The depth and the tag come from different places depending on how the scene was drawn, so the
  /// pass is compiled once per \a source. A multisampled stencil is read as it stands, sample by
  /// sample, there being no resolve for a stencil the way there is for colour and depth; only sample
  /// zero is read, a silhouette one sample ragged not being visible under a cue several pixels wide.
  static std::string pixelShaderSource(Source source)
  {
    // Whichever source it is, it arrives in the same two slots, so one root signature and one
    // descriptor table serve all three variants.
    std::string surface;
    switch (source)
    {
    case Source::rasterizedMultisampled:
      surface = R"foo(
Texture2D<float> sceneDepth : register(t1);
Texture2DMS<uint2> sceneStencil : register(t2);
float deviceDepthAt(int2 pixel, float2 size) {return sceneDepth.Load(int3(pixel, 0));}
uint stencilAt(int2 pixel, float2 size) {return sceneStencil.Load(pixel, 0).g;}
)foo";
      break;
    case Source::rasterized:
      surface = R"foo(
Texture2D<float> sceneDepth : register(t1);
Texture2D<uint2> sceneStencil : register(t2);
float deviceDepthAt(int2 pixel, float2 size) {return sceneDepth.Load(int3(pixel, 0));}
uint stencilAt(int2 pixel, float2 size) {return sceneStencil.Load(int3(pixel, 0)).g;}
)foo";
      break;
    case Source::traced:
      surface = R"foo(
StructuredBuffer<float> tracedDepth : register(t1);
StructuredBuffer<uint> tracedCueMask : register(t2);
float deviceDepthAt(int2 pixel, float2 size) {return tracedDepth[pixel.y * int(size.x) + pixel.x];}
uint stencilAt(int2 pixel, float2 size) {return tracedCueMask[pixel.y * int(size.x) + pixel.x];}
)foo";
      break;
    }

    return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral + std::string(R"foo(
Texture2D<float4> sceneColor : register(t0);
)foo") + surface + DirectXUniformStringLiterals::EdgeCueingStencilTagStringLiteral
         + std::string(R"foo(
struct Cues
{
  bool contours;
  bool halos;
  bool cueable;
};

/// What the molecular passes recorded for this pixel. A pixel that belongs to a structure but asked
/// for nothing is still cueable, so that it takes the halo of an atom in front of it; the background
/// and the unit cell are not, and so are left alone.
Cues cuesAtPixel(int2 pixel, float2 size)
{
  int2 clamped = clamp(pixel, int2(0, 0), int2(size) - int2(1, 1));
  uint stencil = stencilAt(clamped, size);
  uint mode = stencil & EDGE_CUEING_STENCIL_MODE_MASK;

  Cues cues;
  cues.cueable = (stencil & EDGE_CUEING_STENCIL_CUEABLE_BIT) != 0;
  cues.contours = cues.cueable && (mode == 1 || mode == 3);
  cues.halos = cues.cueable && (mode == 2 || mode == 3);
  return cues;
}

float deviceDepthAtPixel(int2 pixel, float2 size)
{
  int2 clamped = clamp(pixel, int2(0, 0), int2(size) - int2(1, 1));
  return deviceDepthAt(clamped, size);
}

/// How far the surface at a pixel is from the camera, in the units the scene is measured in. Both
/// cues compare the depths of neighbouring pixels, and the value in the buffer is no use for that: it
/// is spread by the projection, so that a step near the camera and the same step far from it read as
/// wildly different numbers.
float sceneDistanceAtPixel(int2 pixel, float2 size)
{
  int2 clamped = clamp(pixel, int2(0, 0), int2(size) - int2(1, 1));
  float deviceDepth = deviceDepthAtPixel(clamped, size);

  float2 normalized = (float2(clamped) + 0.5) / size;
  float2 clipPosition = normalized * float2(2.0, -2.0) + float2(-1.0, 1.0);
  // The projection is OpenGL's, whose clip space runs from -1 to 1 in z while the buffer holds what
  // Direct3D wants, from 0 to 1: the passes that draw the scene remap on the way out, and this remaps
  // back before unprojecting.
  float4 eyePosition = mul(frameUniforms.projectionMatrixInverse,
                           float4(clipPosition, 2.0 * deviceDepth - 1.0, 1.0));
  return -eyePosition.z / eyePosition.w;
}

/// The step in depth across a pixel, taken forwards and backwards at once so that the slope of a
/// single curved surface cancels and only a jump from one surface to another is left.
float depthBreakAlong(int2 pixel, int2 offset, float2 size, float distanceHere)
{
  float forward = sceneDistanceAtPixel(pixel + offset, size);
  float backward = sceneDistanceAtPixel(pixel - offset, size);
  return (forward - distanceHere) + (backward - distanceHere);
}

/// A contour line, drawn where one surface passes in front of another. Rings of growing radius, each
/// asking for a larger step than the last, which is what widens the line with the size of the jump: a
/// sphere against a distant one is outlined heavily, one against its neighbour barely at all.
float contourLineStrength(int2 pixel, float2 size)
{
  float distanceHere = sceneDistanceAtPixel(pixel, size);
  int widest = int(clamp(frameUniforms.edgeCueing.y, 1.0, 5.0));

  float contour = 0.0;
  for (int radius = 1; radius <= widest; radius++)
  {
    float step = 0.0;
    step = max(step, depthBreakAlong(pixel, int2(radius, 0), size, distanceHere));
    step = max(step, depthBreakAlong(pixel, int2(0, radius), size, distanceHere));

    float needed = frameUniforms.edgeCueingContourDepth * float(radius) / float(widest);
    contour = max(contour, smoothstep(0.4 * needed, needed, step));
  }
  return contour;
}

/// The halo a nearer surface casts over what lies behind it. Read the other way about from the
/// contour: this pixel is darkened by its neighbours, and only by those whose structure asked to cast
/// one.
float haloStrength(int2 pixel, float2 size)
{
  const int taps = 8;
  const float2 directions[taps] = {float2( 1.0,  0.0), float2( 0.0,  1.0), float2(-1.0,  0.0), float2( 0.0, -1.0),
                                   float2( 0.707,  0.707), float2(-0.707,  0.707), float2(-0.707, -0.707), float2( 0.707, -0.707)};
  const int rings = 3;

  // Fraction of the reference depth a step has to reach before it is read as one surface in front of
  // another rather than as the slope of a single curved one.
  const float floorFraction = 0.35;

  float distanceHere = sceneDistanceAtPixel(pixel, size);
  float reach = max(frameUniforms.edgeCueing.w, 1.0);
  float scale = max(frameUniforms.edgeCueingHaloDepth, 1.0e-6);

  float halo = 0.0;
  for (int ring = 1; ring <= rings; ring++)
  {
    float radius = reach * float(ring) / float(rings);
    float weight = 1.0 - float(ring - 1) / float(rings);

    for (int i = 0; i < taps; i++)
    {
      int2 offset = int2(round(directions[i] * radius));
      if (!cuesAtPixel(pixel + offset, size).halos) {continue;}

      float distanceThere = sceneDistanceAtPixel(pixel + offset, size);
      float step = (distanceHere - distanceThere) / scale;
      halo = max(halo, weight * smoothstep(floorFraction, 1.0, step));
    }
  }
  return halo;
}

struct PSInput { float4 position : SV_POSITION; };

float4 PSMain(PSInput input) : SV_TARGET
{
  uint2 dimensions;
  sceneColor.GetDimensions(dimensions.x, dimensions.y);
  float2 size = float2(dimensions);
  int2 pixel = int2(input.position.xy);

  float4 color = sceneColor.Load(int3(pixel, 0));
  Cues here = cuesAtPixel(pixel, size);

  if (here.contours && frameUniforms.edgeCueing.x > 0.0)
  {
    float contour = contourLineStrength(pixel, size);
    color.xyz *= 1.0 - frameUniforms.edgeCueing.x * contour;
  }

  // The background takes a halo as readily as a surface does, an atom at the edge of a structure
  // casting one onto nothing at all.
  bool isBackground = deviceDepthAtPixel(pixel, size) >= 0.99999;
  if ((here.cueable || isBackground) && frameUniforms.edgeCueing.z > 0.0)
  {
    float halo = haloStrength(pixel, size);
    color.xyz *= 1.0 - frameUniforms.edgeCueing.z * halo;
  }

  return float4(clamp(color.xyz, 0.0, 1.0), color.a);
}
)foo");
  }
};
