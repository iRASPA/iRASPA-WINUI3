/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkrenderuniforms.h"
#include "rkribbonmesh.h"
#include <cstdint>

// Layouts shared between the path tracer's C++ side and its HLSL kernels, mirroring
// PathTracerCommon.h of the Cocoa app. The HLSL side declares the same structs as a string
// literal in directxpathtracerstringliterals.h; keep the two in step, and mind that a
// StructuredBuffer element in HLSL is laid out like the C++ struct only as long as every
// member stays 16-byte sized.

namespace RKPathTracer
{
  /// Which geometry an instance of the top-level acceleration structure carries. The kernels
  /// read this from the instance record rather than from the geometry, because inline ray
  /// tracing has no intersection-function table to dispatch through: a procedural-primitive
  /// candidate arrives with nothing but its instance and primitive index.
  enum class Kind : uint32_t
  {
    sphere = 0,
    cylinder = 1,
    ribbon = 2
  };

  /// Which set of HDR/hue/saturation/value and shininess parameters of RKStructureUniforms
  /// applies to a hit.
  enum class Category : uint32_t
  {
    atom = 0,
    bond = 1,
    ribbon = 2
  };

  /// Mirrors the striped and Worley-noise cases of RKSelectionStyle. An instance marked with
  /// one of these is the enlarged shell a selection is drawn on rather than a surface of the
  /// model; `none` marks the model itself. The glow style is absent on purpose: the rasterizer
  /// draws it into its own texture and blurs it over the finished image, which happens after
  /// the trace has been composited and so needs nothing from here.
  enum class Selection : uint32_t
  {
    none = 0,
    worley = 1,
    striped = 2
  };

  /// Instance masks. The selection shells answer primary rays only: the raster overlay they
  /// stand in for is neither shadowed nor occluding, so the shadow and bounce rays of a path
  /// are traced against the model alone and a selection never darkens what is next to it.
  constexpr uint32_t maskSurface = 0x1;
  constexpr uint32_t maskSelection = 0x2;

  /// The style of shell to pack for a selection, or `none` when the tracer draws none. Both
  /// RKSelectionStyle::None and ::glow come back as `none`: nothing is packed for the first,
  /// and the second is the rasterizer's blurred overlay, composited after the trace.
  inline Selection selectionStyle(RKSelectionStyle style)
  {
    switch (style)
    {
    case RKSelectionStyle::WorleyNoise3D: return Selection::worley;
    case RKSelectionStyle::striped: return Selection::striped;
    default: return Selection::none;
    }
  }

  inline uint32_t instanceMask(Selection selection)
  {
    return (selection == Selection::none) ? maskSurface : maskSelection;
  }

  /// How far a ribbon selection shell stands off the ribbon, as a fraction of what
  /// atomSelectionScaling asks for, matching the expansionScale the ribbon selection vertex
  /// shaders pass to ribbonSelectionExpandedPosition. The striped style is lifted further
  /// than the Worley-noise one: its pattern has gaps, and a shell too close to its ribbon
  /// shows through them as much as beside them. Atoms and bonds have no equivalent, their
  /// shells being scaled about a centre rather than pushed along a surface.
  inline float ribbonExpansionScale(Selection selection)
  {
    return (selection == Selection::striped) ? 0.45f : 0.2f;
  }
}

/// An atom, in the structure space of its owning structure. Colours are the raw per-instance
/// colours of RKInPerInstanceAttributesAtoms; the light and structure colours are folded in by
/// the kernel, exactly as the imposter vertex shader does.
struct RKPathTracerSphere
{
  float4 center = float4();     // xyz = centre, w = radius
  float4 ambient = float4();
  float4 diffuse = float4();
  float4 specular = float4();
};

/// One (sub-)cylinder of a bond, in structure space. Double and triple bonds are expanded into
/// their sub-cylinders when the buffer is packed, so the kernel only ever intersects a single
/// capped cylinder.
struct RKPathTracerCylinder
{
  float4 pointA = float4();     // xyz = first end-cap centre, w = radius
  float4 pointB = float4();     // xyz = second end-cap centre, w unused
  float4 color1 = float4();     // raw per-instance colour at pointA
  float4 color2 = float4();     // raw per-instance colour at pointB
  // The two axes across the cylinder, in structure space, which fix where the selection
  // patterns start winding around it. Named for the model axes of the bond hull, so the angle
  // measured from them is the one bondImposterModelCoords measures.
  float4 axisX = float4();      // xyz = model x-axis, w unused
  float4 axisZ = float4();      // xyz = model z-axis, w unused
};

/// One instance of the top-level acceleration structure, found through the InstanceID the
/// instance descriptor carries. `primitiveBase` turns the geometry-local primitive index into
/// an index into the concatenated global buffers.
struct RKPathTracerInstance
{
  uint32_t kind = 0;             // RKPathTracer::Kind
  uint32_t primitiveBase = 0;
  uint32_t structureIndex = 0;   // index into the RKStructureUniforms array
  uint32_t clipAtUnitCell = 0;   // non-zero when the six clip planes apply

  uint32_t selectionStyle = 0;   // RKPathTracer::Selection, `none` for the model itself
  uint32_t pad0 = 0;
  uint32_t pad1 = 0;
  uint32_t pad2 = 0;
};

/// Per-dispatch constants of the ray-tracing kernels, mirroring the b2 constant buffer of
/// DirectXPathTracerStringLiterals.
struct RKPathTracerUniforms
{
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t samplesPerDispatch = 1;
  uint32_t sampleOffset = 0;          // number of samples already accumulated

  uint32_t maximumBounces = 0;        // 0 = direct lighting only
  uint32_t seed = 0;
  float rayEpsilon = 1.0e-3f;         // secondary-ray offset, scaled to the scene size
  float accumulatedSamples = 0.0f;    // total sample count, used by the resolve kernel

  // How much of the traced ambient occlusion is applied to the direct lighting, which is what the
  // raster path does when it multiplies its baked occlusion into (ambient + diffuse + specular).
  // 0 leaves the direct term physically unoccluded, 1 reproduces the look of the rasterized
  // "Fancy" ribbons.
  float ambientOcclusionStrength = 1.0f;

  // The resolve kernel writes RGBA, that being the one 8-bit order every device can store to
  // through an unordered-access view, but the scene it composites over was drawn into the swap
  // chain's format, which on a composition swap chain is BGRA. A view cannot cross the two
  // formats, so the read is swizzled instead.
  uint32_t sceneColorSwapsRedAndBlue = 0;
  float pad1 = 0.0f;
  float pad2 = 0.0f;
};

static_assert(sizeof(RKPathTracerSphere) == 64, "the sphere stride the kernels assume has changed");
static_assert(sizeof(RKPathTracerCylinder) == 96, "the cylinder stride the kernels assume has changed");
static_assert(sizeof(RKPathTracerInstance) == 32, "the instance stride the kernels assume has changed");
static_assert(sizeof(RKPathTracerUniforms) == 48, "the tracer constant buffer no longer matches its HLSL block");
// The kernels walk the same ribbon vertices the raster ribbon pass draws, so their HLSL mirror has
// to keep this stride.
static_assert(sizeof(RKRibbonVertex) == 56, "the ribbon vertex stride the kernels assume has changed");
