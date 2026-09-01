/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "directxuniformstringliterals.h"
#include <string>

/// HLSL of the ray-tracing kernels. The layouts here mirror directxpathtracercommon.h and the
/// analytic intersections mirror the imposter pixel shaders, so a traced surface and a rasterized
/// one agree where they meet.
///
/// These are Shader Model 6.5 and go through DirectXDxcCompiler rather than D3DCompile, inline ray
/// tracing being the one thing the renderkit needs that Shader Model 5 cannot express.
class DirectXPathTracerStringLiterals
{
public:
  /// Mirror of directxpathtracercommon.h.
  inline static const std::string CommonStringLiteral = R"foo(
#define PATH_TRACER_KIND_SPHERE    0
#define PATH_TRACER_KIND_CYLINDER  1
#define PATH_TRACER_KIND_RIBBON    2

#define PATH_TRACER_SELECTION_NONE     0
#define PATH_TRACER_SELECTION_WORLEY   1
#define PATH_TRACER_SELECTION_STRIPED  2

#define PATH_TRACER_MASK_SURFACE    0x1
#define PATH_TRACER_MASK_SELECTION  0x2

#define PATH_TRACER_CATEGORY_ATOM   0
#define PATH_TRACER_CATEGORY_BOND   1
#define PATH_TRACER_CATEGORY_RIBBON 2

struct PathTracerSphere
{
  float4 center;      // xyz = centre, w = radius
  float4 ambient;
  float4 diffuse;
  float4 specular;
};

struct PathTracerCylinder
{
  float4 pointA;      // xyz = first end-cap centre, w = radius
  float4 pointB;      // xyz = second end-cap centre
  float4 color1;
  float4 color2;
  float4 axisX;       // model x-axis, which the selection patterns are wound from
  float4 axisZ;       // model z-axis
};

struct PathTracerInstance
{
  uint kind;
  uint primitiveBase;
  uint structureIndex;
  uint clipAtUnitCell;

  uint selectionStyle;
  uint pad0;
  uint pad1;
  uint pad2;
};

// Mirror of RKRibbonVertex. Only the position and the normal are read here; the two coordinate
// pairs are carried so that the stride matches the buffer the raster ribbon pass shares.
struct PathTracerRibbonVertex
{
  float4 position;
  float4 normal;
  float2 st;
  float2 pad;
  float2 stripeST;
};

cbuffer PathTracerUniformBlock : register(b2)
{
  uint traceWidth;
  uint traceHeight;
  uint samplesPerDispatch;
  uint sampleOffset;

  uint maximumBounces;
  uint traceSeed;
  float rayEpsilon;
  float accumulatedSamples;

  float ambientOcclusionStrength;
  uint sceneColorSwapsRedAndBlue;
  float tracePad1;
  float tracePad2;
};
)foo";

  /// The six unit-cell clip planes as a convex constraint on a ray, shared by the sphere and the
  /// cylinder. Unlike the imposter pixel shaders, which discard clipped fragments and so leave
  /// hollow shells, the opening is capped: a traced ray must not find its way into the inside of
  /// an atom.
  inline static const std::string ClipPlanesStringLiteral = R"foo(
// The visible solid is an intersection of convex constraints, so the entry parameter is the
// largest of all entry parameters and the exit parameter the smallest of all exit parameters.
// `entryPlane` comes back as the plane that produced the entry point, or -1 when the entry point
// lies on the solid itself. False when the ray misses the cell altogether.
bool pathTracerClipPlanes(float3 origin, float3 direction, StructureUniformData su,
                          inout float tmin, inout float tmax, inout int entryPlane)
{
  float4 planes[6] = { su.clipPlaneLeft, su.clipPlaneRight, su.clipPlaneTop,
                       su.clipPlaneBottom, su.clipPlaneFront, su.clipPlaneBack };
  float4 so = float4(origin, 1.0);
  float4 sd = float4(direction, 0.0);

  for (int i = 0; i < 6; i++)
  {
    float f0 = dot(planes[i], so);
    float df = dot(planes[i], sd);
    if (abs(df) < 1.0e-8)
    {
      if (f0 < 0.0) return false;
    }
    else
    {
      float tp = -f0 / df;
      if (df > 0.0)
      {
        if (tp > tmin) { tmin = tp; entryPlane = i; }
      }
      else
      {
        tmax = min(tmax, tp);
      }
    }
  }
  return true;
}

float3 pathTracerClipPlaneNormal(int plane, StructureUniformData su)
{
  float4 planes[6] = { su.clipPlaneLeft, su.clipPlaneRight, su.clipPlaneTop,
                       su.clipPlaneBottom, su.clipPlaneFront, su.clipPlaneBack };
  // the outward normal of the cap is opposite to the plane's inward gradient
  return -normalize(planes[plane].xyz);
}
)foo";

  /// Analytic sphere and capped cylinder, in the structure space a candidate procedural primitive
  /// arrives in. Include after ClipPlanesStringLiteral.
  ///
  /// Where Metal hands an intersection function a payload to write its normal into, a RayQuery has
  /// none: the traversal reports only which instance and primitive was committed and at what
  /// distance. So these report a distance during traversal and the caller re-derives the normal
  /// afterwards, by intersecting the committed primitive a second time. That costs one extra
  /// intersection per pixel and keeps the candidate loop free of anything it does not need.
  inline static const std::string IntersectionsStringLiteral = R"foo(
struct PathTracerHit
{
  bool hit;
  float distance;
  float3 normal;         // structure space, at the entry point; not normalized
  float axialFraction;   // 0 at pointA, 1 at pointB; unused for spheres
};

PathTracerHit pathTracerMiss()
{
  PathTracerHit result;
  result.hit = false;
  result.distance = 0.0;
  result.normal = float3(0.0, 0.0, 1.0);
  result.axialFraction = 0.0;
  return result;
}

// `direction` is the object-space ray direction, which DXR does not normalize; the quadratic is
// written so that the resulting distance is in the same parameterisation as the world-space ray.
PathTracerHit pathTracerIntersectSphere(float3 origin, float3 direction, float minDistance,
                                        float maxDistance, PathTracerSphere sphere,
                                        bool clipAtUnitCell, StructureUniformData su)
{
  PathTracerHit result = pathTracerMiss();

  float3 oc = origin - sphere.center.xyz;
  float radius = sphere.center.w;

  float a = dot(direction, direction);
  float b = dot(oc, direction);
  float c = dot(oc, oc) - radius * radius;
  float discriminant = b * b - a * c;
  if (discriminant < 0.0) return result;

  float root = sqrt(discriminant);
  float tmin = (-b - root) / a;
  float tmax = (-b + root) / a;
  int entryPlane = -1;

  if (clipAtUnitCell)
  {
    if (!pathTracerClipPlanes(origin, direction, su, tmin, tmax, entryPlane)) return result;
  }

  if (tmax < tmin || tmin < minDistance || tmin > maxDistance) return result;

  result.hit = true;
  result.distance = tmin;
  result.normal = (entryPlane < 0) ? (oc + tmin * direction) / radius
                                   : pathTracerClipPlaneNormal(entryPlane, su);
  return result;
}

PathTracerHit pathTracerIntersectCylinder(float3 origin, float3 direction, float minDistance,
                                          float maxDistance, PathTracerCylinder cylinder,
                                          bool clipAtUnitCell, StructureUniformData su)
{
  PathTracerHit result = pathTracerMiss();

  float3 pointA = cylinder.pointA.xyz;
  float3 pointB = cylinder.pointB.xyz;
  float radius = cylinder.pointA.w;

  // the k2/k1/k0 formulation of the imposter shaders assumes a normalized direction, so work with
  // a normalized copy and convert the resulting parameter back at the end
  float length2 = dot(direction, direction);
  if (length2 <= 0.0) return result;
  float invLength = rsqrt(length2);
  float3 rd = direction * invLength;

  float3 ba = pointB - pointA;
  float3 oc = origin - pointA;
  float baba = dot(ba, ba);
  float bard = dot(ba, rd);
  float baoc = dot(ba, oc);
  float k2 = baba - bard * bard;
  float k1 = baba * dot(oc, rd) - baoc * bard;
  float k0 = baba * dot(oc, oc) - baoc * baoc - radius * radius * baba;

  float tmin = -1.0e30;
  float tmax = 1.0e30;
  // -1 undetermined, 0 cylinder mantle, 1 end cap, 2 clip plane
  int entryType = -1;

  // the infinite cylinder around the axis
  if (k2 > 1.0e-6 * baba)
  {
    float h = k1 * k1 - k2 * k0;
    if (h < 0.0) return result;
    h = sqrt(h);
    tmin = (-k1 - h) / k2;
    tmax = (-k1 + h) / k2;
    entryType = 0;
  }
  else if (k0 > 0.0)
  {
    // the ray is (nearly) parallel to the axis and lies outside the cylinder
    return result;
  }

  // the slab between the two end-cap planes: 0 <= baoc + t * bard <= baba
  if (abs(bard) > 1.0e-6)
  {
    float tCapA = (0.0 - baoc) / bard;
    float tCapB = (baba - baoc) / bard;
    float tEnter = min(tCapA, tCapB);
    float tExit = max(tCapA, tCapB);
    if (tEnter > tmin) { tmin = tEnter; entryType = 1; }
    tmax = min(tmax, tExit);
  }
  else if (baoc < 0.0 || baoc > baba)
  {
    return result;
  }

  int entryPlane = -1;
  if (clipAtUnitCell)
  {
    // the clip-plane parameters are measured along the same normalized direction
    if (!pathTracerClipPlanes(origin, rd, su, tmin, tmax, entryPlane)) return result;
    if (entryPlane >= 0) entryType = 2;
  }

  if (tmax < tmin || entryType < 0) return result;

  // back to the parameterisation of the unnormalized direction
  float distance = tmin * invLength;
  if (distance < minDistance || distance > maxDistance) return result;

  float y = baoc + tmin * bard;

  result.hit = true;
  result.distance = distance;
  result.axialFraction = clamp(y / baba, 0.0, 1.0);

  if (entryType == 0)
  {
    result.normal = (oc + tmin * rd - ba * y / baba) / radius;
  }
  else if (entryType == 1)
  {
    result.normal = (y < 0.5 * baba) ? -ba / sqrt(baba) : ba / sqrt(baba);
  }
  else
  {
    result.normal = pathTracerClipPlaneNormal(entryPlane, su);
  }
  return result;
}
)foo";

  /// Resources every kernel binds, and the traversal shared by all of them. Include after
  /// IntersectionsStringLiteral.
  inline static const std::string TraversalStringLiteral = R"foo(
RaytracingAccelerationStructure pathTracerScene : register(t0);
StructuredBuffer<StructureUniformData> pathTracerStructures : register(t1);
StructuredBuffer<PathTracerInstance> pathTracerInstances : register(t2);
StructuredBuffer<PathTracerSphere> pathTracerSpheres : register(t3);
StructuredBuffer<PathTracerCylinder> pathTracerCylinders : register(t4);
StructuredBuffer<PathTracerRibbonVertex> pathTracerRibbonVertices : register(t5);
StructuredBuffer<uint> pathTracerRibbonIndices : register(t6);

/// Intersects the procedural primitive a candidate reports, whichever kind it is. Triangles are
/// not routed through here: the built-in test handles those.
PathTracerHit pathTracerIntersectCandidate(PathTracerInstance instance, uint primitiveIndex,
                                           float3 origin, float3 direction, float minDistance,
                                           float maxDistance)
{
  StructureUniformData su = pathTracerStructures[instance.structureIndex];
  bool clip = (instance.clipAtUnitCell != 0);

  if (instance.kind == PATH_TRACER_KIND_SPHERE)
  {
    PathTracerSphere sphere = pathTracerSpheres[instance.primitiveBase + primitiveIndex];
    return pathTracerIntersectSphere(origin, direction, minDistance, maxDistance, sphere, clip, su);
  }

  PathTracerCylinder cylinder = pathTracerCylinders[instance.primitiveBase + primitiveIndex];
  return pathTracerIntersectCylinder(origin, direction, minDistance, maxDistance, cylinder, clip, su);
}

/// Whether anything in `instanceMask` stands within `maxDistance` along the ray. Stops at the first
/// hit, so it says nothing about which surface it found.
bool pathTracerIsOccluded(float3 origin, float3 direction, float minDistance, float maxDistance,
                          uint instanceMask)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = minDistance;
  ray.TMax = maxDistance;

  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
  query.TraceRayInline(pathTracerScene, RAY_FLAG_NONE, instanceMask, ray);

  while (query.Proceed())
  {
    if (query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
    {
      PathTracerInstance instance = pathTracerInstances[query.CandidateInstanceID()];
      // Any primitive along the ray ends this query, so a candidate is tested against the whole
      // interval rather than against a nearest hit that is not being looked for.
      PathTracerHit hit = pathTracerIntersectCandidate(
          instance, query.CandidatePrimitiveIndex(), query.CandidateObjectRayOrigin(),
          query.CandidateObjectRayDirection(), minDistance, maxDistance);
      if (hit.hit)
      {
        query.CommitProceduralPrimitiveHit(hit.distance);
      }
    }
    else
    {
      // A non-opaque triangle only reaches here for a striped ribbon selection shell, and those
      // are masked out of every ray that asks about occlusion.
      query.CommitNonOpaqueTriangleHit();
    }
  }

  return query.CommittedStatus() != COMMITTED_NOTHING;
}

/// What the closest surface in `instanceMask` is, and its world-space normal. `found` is false when
/// the ray escapes the scene.
struct PathTracerSurfaceHit
{
  bool found;
  float distance;
  float3 position;      // world space
  float3 normal;        // world space, normalized, turned towards the ray
  uint instanceIndex;
  uint primitiveIndex;
  float2 barycentrics;
  float axialFraction;
};

PathTracerSurfaceHit pathTracerClosestSurface(float3 origin, float3 direction, float minDistance,
                                              float maxDistance, uint instanceMask)
{
  PathTracerSurfaceHit surface;
  surface.found = false;
  surface.distance = maxDistance;
  surface.position = origin;
  surface.normal = float3(0.0, 0.0, 1.0);
  surface.instanceIndex = 0;
  surface.primitiveIndex = 0;
  surface.barycentrics = float2(0.0, 0.0);
  surface.axialFraction = 0.0;

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = minDistance;
  ray.TMax = maxDistance;

  RayQuery<RAY_FLAG_NONE> query;
  query.TraceRayInline(pathTracerScene, RAY_FLAG_NONE, instanceMask, ray);

  // The interval still worth searching, narrowed by hand as primitives are committed. Asking the
  // query for its committed distance instead would be reading a value that is only meaningful once
  // something has been committed, and a procedural primitive is only ever committed from here.
  float nearest = maxDistance;

  while (query.Proceed())
  {
    if (query.CandidateType() == CANDIDATE_PROCEDURAL_PRIMITIVE)
    {
      PathTracerInstance instance = pathTracerInstances[query.CandidateInstanceID()];
      PathTracerHit hit = pathTracerIntersectCandidate(
          instance, query.CandidatePrimitiveIndex(), query.CandidateObjectRayOrigin(),
          query.CandidateObjectRayDirection(), minDistance, nearest);
      if (hit.hit)
      {
        query.CommitProceduralPrimitiveHit(hit.distance);
        nearest = hit.distance;
      }
    }
    else
    {
      query.CommitNonOpaqueTriangleHit();
    }
  }

  if (query.CommittedStatus() == COMMITTED_NOTHING) return surface;

  surface.found = true;
  surface.distance = query.CommittedRayT();
  surface.position = origin + surface.distance * direction;
  surface.instanceIndex = query.CommittedInstanceID();
  surface.primitiveIndex = query.CommittedPrimitiveIndex();

  PathTracerInstance instance = pathTracerInstances[surface.instanceIndex];
  StructureUniformData su = pathTracerStructures[instance.structureIndex];

  if (instance.kind == PATH_TRACER_KIND_RIBBON)
  {
    surface.barycentrics = query.CommittedTriangleBarycentrics();
    uint triangleIndex = instance.primitiveBase + surface.primitiveIndex;
    uint i0 = pathTracerRibbonIndices[3 * triangleIndex + 0];
    uint i1 = pathTracerRibbonIndices[3 * triangleIndex + 1];
    uint i2 = pathTracerRibbonIndices[3 * triangleIndex + 2];

    float w0 = 1.0 - surface.barycentrics.x - surface.barycentrics.y;
    float3 localNormal = w0 * pathTracerRibbonVertices[i0].normal.xyz
                       + surface.barycentrics.x * pathTracerRibbonVertices[i1].normal.xyz
                       + surface.barycentrics.y * pathTracerRibbonVertices[i2].normal.xyz;
    surface.normal = normalize(mul(su.modelMatrix, float4(localNormal, 0.0)).xyz);
  }
  else
  {
    // The traversal reported no normal, so the committed primitive is intersected once more. The
    // object-space ray is the one the intersection was found with, so this cannot disagree with it.
    PathTracerHit hit = pathTracerIntersectCandidate(
        instance, surface.primitiveIndex, query.CommittedObjectRayOrigin(),
        query.CommittedObjectRayDirection(), minDistance, maxDistance);
    surface.axialFraction = hit.axialFraction;
    surface.normal = normalize(mul(su.modelMatrix, float4(normalize(hit.normal), 0.0)).xyz);
  }

  if (dot(surface.normal, direction) > 0.0) surface.normal = -surface.normal;
  return surface;
}

/// The world-space ray through the centre of pixel `pixel`. Through the centre rather than a
/// jittered point, so that a mask traced from it is the same every frame and shadow edges do not
/// crawl while the camera sits still.
void pathTracerPixelRay(uint2 pixel, uint width, uint height, out float3 origin, out float3 direction)
{
  float2 uv = (float2(pixel) + 0.5) / float2(float(width), float(height));
  float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

  // The projection matrix is the OpenGL one the raster passes are handed too, whose clip space runs
  // z from -1 at the near plane to 1 at the far plane; each of those passes remaps the depth it
  // writes to the 0..1 D3D holds. Unprojecting 0 here would put the ray's origin halfway down the
  // frustum, behind most of what the camera can see.
  float4 nearEye = mul(frameUniforms.projectionMatrixInverse, float4(ndc, -1.0, 1.0));
  float4 farEye = mul(frameUniforms.projectionMatrixInverse, float4(ndc, 1.0, 1.0));
  nearEye /= nearEye.w;
  farEye /= farEye.w;

  origin = mul(frameUniforms.viewMatrixInverse, nearEye).xyz;
  direction = normalize(mul(frameUniforms.viewMatrixInverse, farEye).xyz - origin);
}
)foo";

  /// The shadow-mask kernel: one bit per light, set where that light reaches the surface at a pixel.
  ///
  /// A compute pass rather than a test inside the pixel shaders because the imposters write their
  /// own depth and so defeat early-z: a per-pixel pass traces once for what a per-fragment one
  /// would trace several times over. The mask records visibility only; falloff, the spotlight cone
  /// and the surface's own orientation stay with the rasterizer, which already computes them.
  inline static const std::string ShadowMaskKernelStringLiteral = R"foo(
RWStructuredBuffer<uint> pathTracerShadowMask : register(u0);

[numthreads(8, 8, 1)]
void shadowMaskKernel(uint3 threadPosition : SV_DispatchThreadID)
{
  uint2 pixel = threadPosition.xy;
  if (pixel.x >= traceWidth || pixel.y >= traceHeight) return;
  uint slot = pixel.y * traceWidth + pixel.x;

  float3 rayOrigin;
  float3 rayDirection;
  pathTracerPixelRay(pixel, traceWidth, traceHeight, rayOrigin, rayDirection);

  // The selection shells are left out: the raster selection is neither shadowed nor shadowing.
  PathTracerSurfaceHit surface =
      pathTracerClosestSurface(rayOrigin, rayDirection, 0.0, 1.0e30, PATH_TRACER_MASK_SURFACE);

  if (!surface.found)
  {
    pathTracerShadowMask[slot] = allLightsVisible;
    return;
  }

  uint mask = allLightsVisible;

  for (uint li = 0; li < 8; li++)
  {
    if (lightUniforms.lights[li].enabled < 0.5) continue;

    float4 lightPosition = lightUniforms.lights[li].position;
    bool positional = lightPosition.w > 0.5;

    float3 lightWorldPosition = mul(frameUniforms.viewMatrixInverse, float4(lightPosition.xyz, 1.0)).xyz;
    float3 lightDirection = positional
        ? normalize(lightWorldPosition - surface.position)
        : normalize(mul(frameUniforms.viewMatrixInverse, float4(lightPosition.xyz, 0.0)).xyz);
    float lightDistance = positional ? length(lightWorldPosition - surface.position) : 1.0e30;

    // A surface turned away from a light is left marked as lit: the rasterizer's own N.L already
    // takes all of that light away, and tracing from behind the surface would only hit itself.
    if (dot(surface.normal, lightDirection) <= 0.0) continue;

    if (pathTracerIsOccluded(surface.position + surface.normal * rayEpsilon, lightDirection, 0.0,
                             lightDistance, PATH_TRACER_MASK_SURFACE))
    {
      mask &= ~(1u << li);
    }
  }

  pathTracerShadowMask[slot] = mask;
}
)foo";

  /// A random number per thread, and the cosine-weighted direction the path leaves a surface in.
  ///
  /// A plain linear congruential sequence, hashed on the way out so that neighbouring pixels do not
  /// walk the same low bits. No Halton or Sobol sequence: the samples of a pixel are independent,
  /// which costs a little convergence and saves carrying a per-pixel dimension counter across the
  /// batches a still image is traced in.
  inline static const std::string RandomStringLiteral = R"foo(
uint pathTracerHash(uint x)
{
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float pathTracerRandom(inout uint state)
{
  state = state * 1664525u + 1013904223u;
  return float(pathTracerHash(state) >> 8) * (1.0 / 16777216.0);
}

/// Cosine-weighted about `normal`, which is the distribution a diffuse bounce wants: the cosine of
/// the reflectance and the cosine of the density cancel, so a path carries only the albedo.
float3 pathTracerCosineDirection(float3 normal, inout uint state)
{
  float u1 = pathTracerRandom(state);
  float u2 = pathTracerRandom(state);

  float radius = sqrt(u1);
  float phi = 6.283185307179586 * u2;

  float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
  float3 tangent = normalize(cross(up, normal));
  float3 bitangent = cross(normal, tangent);

  return normalize(radius * cos(phi) * tangent + radius * sin(phi) * bitangent +
                   sqrt(max(0.0, 1.0 - u1)) * normal);
}
)foo";

  /// The material of a hit, and the exposure and colour adjustment it is finally read through.
  /// Whichever of the atom, bond or ribbon parameters of a structure a colour came from decides
  /// both, which is why a hit carries the category it belongs to.
  inline static const std::string MaterialsStringLiteral = R"foo(
struct PathTracerSurface
{
  float3 ambient;
  float3 diffuse;
  float3 specular;
  float shininess;
  uint category;
};

float3 pathTracerRibbonColor(float structureType, StructureUniformData su)
{
  if (structureType < 0.5) return su.ribbonCoilColor.xyz;
  if (structureType < 1.5) return su.ribbonHelixColor.xyz;
  return su.ribbonSheetColor.xyz;
}

PathTracerSurface pathTracerSphereSurface(PathTracerSphere sphere, StructureUniformData su)
{
  PathTracerSurface surface;

  if (su.colorAtomsWithBondColor != 0)
  {
    surface.ambient = su.bondAmbientColor.xyz;
    surface.diffuse = su.bondDiffuseColor.xyz;
    surface.specular = su.bondSpecularColor.xyz;
  }
  else
  {
    surface.ambient = (su.atomAmbientColor * sphere.ambient).xyz;
    surface.diffuse = (su.atomDiffuseColor * sphere.diffuse).xyz;
    surface.specular = (su.atomSpecularColor * sphere.specular).xyz;
  }
  surface.shininess = su.atomShininess;
  surface.category = PATH_TRACER_CATEGORY_ATOM;

  return surface;
}

PathTracerSurface pathTracerCylinderSurface(PathTracerCylinder cylinder, float axialFraction,
                                            StructureUniformData su)
{
  PathTracerSurface surface;

  float4 diffuseColor = (su.bondColorMode == 0) ? su.bondDiffuseColor : su.atomDiffuseColor;
  float4 color1 = diffuseColor * cylinder.color1;
  float4 color2 = diffuseColor * cylinder.color2;

  surface.ambient = su.bondAmbientColor.xyz;
  surface.specular = su.bondSpecularColor.xyz;
  if (su.bondColorMode == 0)
  {
    surface.diffuse = su.bondDiffuseColor.xyz;
  }
  else if (su.bondColorMode == 1)
  {
    surface.diffuse = (axialFraction < 0.5 ? color1 : color2).xyz;
  }
  else
  {
    surface.diffuse = lerp(color1, color2, smoothstep(0.0, 1.0, axialFraction)).xyz;
  }
  surface.shininess = su.bondShininess;
  surface.category = PATH_TRACER_CATEGORY_BOND;

  return surface;
}

/// `adjustHueSaturationValue` is there for the bond selections, which the raster shaders
/// exposure-map without also shifting.
float3 pathTracerToneMap(float3 radiance, uint category, StructureUniformData su,
                         bool adjustHueSaturationValue)
{
  bool useHDR = su.atomHDR != 0;
  float exposure = su.atomHDRExposure;
  float hueScale = su.atomHue;
  float saturationScale = su.atomSaturation;
  float valueScale = su.atomValue;

  if (category == PATH_TRACER_CATEGORY_BOND)
  {
    useHDR = su.bondHDR != 0;
    exposure = su.bondHDRExposure;
    hueScale = su.bondHue;
    saturationScale = su.bondSaturation;
    valueScale = su.bondValue;
  }
  else if (category == PATH_TRACER_CATEGORY_RIBBON)
  {
    useHDR = su.ribbonHDR != 0;
    exposure = su.ribbonHDRExposure;
    hueScale = su.ribbonHue;
    saturationScale = su.ribbonSaturation;
    valueScale = su.ribbonValue;
  }

  float3 color = useHDR ? (1.0 - exp2(-radiance * exposure)) : radiance;

  if (!adjustHueSaturationValue) return color;

  float3 hsv = rgb2hsv(color);
  hsv.x = hsv.x * hueScale;
  hsv.y = hsv.y * saturationScale;
  hsv.z = hsv.z * valueScale;
  return hsv2rgb(hsv);
}
)foo";

  /// The lights of the raster path are written in eye space, so they are brought into the world
  /// space the trace works in once per pixel rather than once per hit. The falloff is the
  /// rasterizer's own, so that the two renderers agree about a light rather than merely about
  /// which surfaces it reaches.
  inline static const std::string LightsStringLiteral = R"foo(
struct PathTracerLights
{
  float3 position[8];
  float3 direction[8];
  float3 spotAxis[8];
};

PathTracerLights pathTracerWorldLights()
{
  PathTracerLights lights;

  for (uint li = 0; li < 8; li++)
  {
    float4 lightPosition = lightUniforms.lights[li].position;
    lights.position[li] = mul(frameUniforms.viewMatrixInverse, float4(lightPosition.xyz, 1.0)).xyz;
    lights.direction[li] =
        normalize(mul(frameUniforms.viewMatrixInverse, float4(lightPosition.xyz, 0.0)).xyz);
    lights.spotAxis[li] = normalize(
        mul(frameUniforms.viewMatrixInverse, float4(lightUniforms.lights[li].spotDirection.xyz, 0.0)).xyz);
  }

  return lights;
}

struct PathTracerLightSample
{
  float3 direction;
  float distance;
  float attenuation;
};

PathTracerLightSample pathTracerSampleLight(PathTracerLights lights, uint index, float3 position)
{
  PathTracerLightSample light;

  bool positional = lightUniforms.lights[index].position.w > 0.5;
  light.direction = positional ? normalize(lights.position[index] - position) : lights.direction[index];
  light.distance = positional ? length(lights.position[index] - position) : 1.0e30;
  light.attenuation = 1.0;

  if (positional)
  {
    light.attenuation = 1.0 / max(lightUniforms.lights[index].constantAttenuation +
                                  lightUniforms.lights[index].linearAttenuation * light.distance +
                                  lightUniforms.lights[index].quadraticAttenuation * light.distance *
                                      light.distance,
                                  1.0e-4);

    if (lightUniforms.lights[index].lightType > 1.5) // spot
    {
      float spotCosine = max(dot(-light.direction, lights.spotAxis[index]), 0.0);
      float cutoffCosine = cos((3.141592653589793 / 180.0) *
                               clamp(lightUniforms.lights[index].spotCutoff, 0.0, 180.0));
      light.attenuation *= (spotCosine < cutoffCosine)
                               ? 0.0
                               : pow(spotCosine, max(lightUniforms.lights[index].spotExponent, 0.0));
    }
  }

  return light;
}
)foo";

  /// The accumulate kernel: traces `samplesPerDispatch` more samples of every pixel into the
  /// running sums.
  ///
  /// Three sums are kept rather than one. The direct term is what the lights deliver to the surface
  /// a primary ray found; the indirect term is what reaches it by way of another surface, and the
  /// ambient the scene contributes where a bounce ray escapes; and the counts in the fourth channels
  /// are what the resolve pass averages by. Kept apart because the occlusion strength weights the
  /// direct term alone, which is what the raster path does with its baked occlusion map.
  ///
  /// A still image is traced in batches, each one a dispatch of its own, so `sampleOffset` says
  /// whether these sums are being started or continued.
  inline static const std::string AccumulateKernelStringLiteral = R"foo(
RWStructuredBuffer<float4> pathTracerAccumulation : register(u0);
RWStructuredBuffer<float4> pathTracerIndirect : register(u1);
RWStructuredBuffer<float4> pathTracerSurfaceInfo : register(u2);

[numthreads(8, 8, 1)]
void accumulateKernel(uint3 threadPosition : SV_DispatchThreadID)
{
  uint2 gid = threadPosition.xy;
  if (gid.x >= traceWidth || gid.y >= traceHeight) return;
  uint pixel = gid.y * traceWidth + gid.x;

  PathTracerLights lights = pathTracerWorldLights();
  // Ambient belongs to the scene rather than to the lights, so which lights are on does not alter it.
  float3 totalAmbient = lightUniforms.sceneAmbient.xyz;

  uint state = pathTracerHash(gid.x + gid.y * traceWidth + pathTracerHash(traceSeed));

  float3 accumulatedDirect = float3(0.0, 0.0, 0.0);
  float3 accumulatedIndirect = float3(0.0, 0.0, 0.0);
  float accumulatedCoverage = 0.0;
  float accumulatedVisibility = 0.0;

  float primaryDepth = 1.0;
  uint primaryStructure = 0;
  uint primaryCategory = PATH_TRACER_CATEGORY_ATOM;
  bool recordedPrimary = false;

  for (uint sampleIndex = 0; sampleIndex < samplesPerDispatch; sampleIndex++)
  {
    // The very first sample of a pixel runs through its centre, because it is the one whose depth is
    // recorded below, and that depth is later depth-tested against by passes that measure a pixel at
    // its centre too.
    bool centreSample = (sampleOffset == 0 && sampleIndex == 0);
    float2 jitter = centreSample ? float2(0.5, 0.5)
                                 : float2(pathTracerRandom(state), pathTracerRandom(state));

    float2 uv = (float2(gid) + jitter) / float2(float(traceWidth), float(traceHeight));
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    // The near plane lies at -1, this being the OpenGL projection the rasterizer is handed as well.
    float4 nearEye = mul(frameUniforms.projectionMatrixInverse, float4(ndc, -1.0, 1.0));
    float4 farEye = mul(frameUniforms.projectionMatrixInverse, float4(ndc, 1.0, 1.0));
    nearEye /= nearEye.w;
    farEye /= farEye.w;

    float3 rayOrigin = mul(frameUniforms.viewMatrixInverse, nearEye).xyz;
    float3 rayTarget = mul(frameUniforms.viewMatrixInverse, farEye).xyz;
    float3 rayDirection = normalize(rayTarget - rayOrigin);

    float3 direct = float3(0.0, 0.0, 0.0);
    float3 indirectRadiance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    float3 pendingAmbient = float3(0.0, 0.0, 0.0);
    bool hitAnything = false;
    // The cosine-sampled ray leaving the primary hit is one sample of the cosine-weighted
    // hemispherical visibility that a baked occlusion map integrates, so its escape rate is the
    // same quantity the raster path bakes.
    bool primaryRayEscaped = false;

    for (uint bounce = 0; bounce <= maximumBounces; bounce++)
    {
      // The path sees the model alone: a selection shell neither occludes nor bounces light.
      PathTracerSurfaceHit hit = pathTracerClosestSurface(
          rayOrigin, rayDirection, (bounce == 0) ? 0.0 : rayEpsilon, 1.0e30, PATH_TRACER_MASK_SURFACE);

      if (!hit.found)
      {
        if (bounce == 1) primaryRayEscaped = true;
        // The surface of the previous bounce sees the environment.
        indirectRadiance += pendingAmbient;
        break;
      }

      PathTracerInstance instance = pathTracerInstances[hit.instanceIndex];
      StructureUniformData su = pathTracerStructures[instance.structureIndex];

      PathTracerSurface surface;
      if (instance.kind == PATH_TRACER_KIND_RIBBON)
      {
        uint triangleIndex = instance.primitiveBase + hit.primitiveIndex;
        uint i0 = pathTracerRibbonIndices[3 * triangleIndex + 0];

        float3 baseColor = pathTracerRibbonColor(pathTracerRibbonVertices[i0].pad.x, su);
        surface.ambient = (su.ribbonAmbientColor * float4(baseColor, 1.0)).xyz;
        surface.diffuse = (su.ribbonDiffuseColor * float4(baseColor, 1.0)).xyz;
        surface.specular = su.ribbonSpecularColor.xyz;
        surface.shininess = su.ribbonShininess;
        surface.category = PATH_TRACER_CATEGORY_RIBBON;
      }
      else if (instance.kind == PATH_TRACER_KIND_SPHERE)
      {
        surface = pathTracerSphereSurface(
            pathTracerSpheres[instance.primitiveBase + hit.primitiveIndex], su);
      }
      else
      {
        surface = pathTracerCylinderSurface(
            pathTracerCylinders[instance.primitiveBase + hit.primitiveIndex], hit.axialFraction, su);
      }

      // The traversal already turned the normal towards the ray, which matters here because ribbons
      // are drawn double-sided and a bounce ray can start inside a cut solid.
      float3 normal = hit.normal;

      if (bounce == 0)
      {
        hitAnything = true;
        if (!recordedPrimary)
        {
          float4 clipPosition = mul(frameUniforms.projectionMatrix,
                                    mul(frameUniforms.viewMatrix, float4(hit.position, 1.0)));
          // Onto the 0..1 the rasterizer's depth buffer holds, which the resolve compares this
          // against, the same remapping its vertex shaders apply.
          primaryDepth = 0.5 * (clipPosition.z / clipPosition.w) + 0.5;
          primaryStructure = instance.structureIndex;
          primaryCategory = surface.category;
          recordedPrimary = true;
        }
      }

      // Direct lighting, with one shadow ray per enabled light.
      for (uint li = 0; li < 8; li++)
      {
        if (lightUniforms.lights[li].enabled < 0.5) continue;

        PathTracerLightSample light = pathTracerSampleLight(lights, li, hit.position);
        float cosTheta = max(dot(normal, light.direction), 0.0);
        if (cosTheta <= 0.0) continue;
        if (light.attenuation <= 0.0) continue;

        if (pathTracerIsOccluded(hit.position + normal * rayEpsilon, light.direction, 0.0,
                                 light.distance, PATH_TRACER_MASK_SURFACE))
        {
          continue;
        }

        float3 lightDiffuse = light.attenuation * lightUniforms.lights[li].diffuse.xyz;

        if (bounce == 0)
        {
          direct += surface.diffuse * lightDiffuse * cosTheta;
          float3 reflectDirection = reflect(-light.direction, normal);
          float specularFactor = pow(max(dot(reflectDirection, -rayDirection), 0.0),
                                     lightUniforms.lights[li].shininess + surface.shininess);
          direct += surface.specular * light.attenuation *
                    lightUniforms.lights[li].specular.xyz * specularFactor;
        }
        else
        {
          // Light picked up further along the path is indirect illumination.
          indirectRadiance += throughput * surface.diffuse * lightDiffuse * cosTheta;
        }
      }

      pendingAmbient = throughput * surface.ambient * totalAmbient;
      throughput *= surface.diffuse;

      rayOrigin = hit.position + normal * rayEpsilon;
      rayDirection = pathTracerCosineDirection(normal, state);
    }

    accumulatedDirect += direct;
    accumulatedIndirect += indirectRadiance;
    accumulatedCoverage += hitAnything ? 1.0 : 0.0;
    accumulatedVisibility += (hitAnything && primaryRayEscaped) ? 1.0 : 0.0;
  }

  bool first = (sampleOffset == 0);
  float4 previousDirect = first ? float4(0.0, 0.0, 0.0, 0.0) : pathTracerAccumulation[pixel];
  float4 previousIndirect = first ? float4(0.0, 0.0, 0.0, 0.0) : pathTracerIndirect[pixel];

  pathTracerAccumulation[pixel] = previousDirect + float4(accumulatedDirect, accumulatedCoverage);
  pathTracerIndirect[pixel] = previousIndirect + float4(accumulatedIndirect, accumulatedVisibility);

  if (first)
  {
    pathTracerSurfaceInfo[pixel] =
        float4(primaryDepth, float(primaryStructure), float(primaryCategory), 1.0);
  }
}
)foo";

  /// The resolve kernel: averages the sums, tone-maps them and composites the result over the
  /// rasterized scene.
  ///
  /// The rasterizer still holds everything the tracer does not draw — the background, the unit cell,
  /// the isosurfaces, the text and the axes — so its depth decides where the traced image is allowed
  /// to show. The visible depth is written out as well, the rasterizer's own depth buffer being
  /// unable to say what is in front at a pixel whose molecular geometry never went through it.
  inline static const std::string ResolveKernelStringLiteral = R"foo(
StructuredBuffer<StructureUniformData> pathTracerResolveStructures : register(t0);
StructuredBuffer<float4> pathTracerResolveAccumulation : register(t1);
StructuredBuffer<float4> pathTracerResolveIndirect : register(t2);
StructuredBuffer<float4> pathTracerResolveSurfaceInfo : register(t3);
Texture2D<float4> pathTracerSceneColor : register(t4);
Texture2D<float> pathTracerSceneDepth : register(t5);

RWStructuredBuffer<float> pathTracerCompositeDepth : register(u0);
RWTexture2D<float4> pathTracerComposite : register(u1);
RWStructuredBuffer<uint> pathTracerCompositeCueMask : register(u2);

[numthreads(8, 8, 1)]
void resolveKernel(uint3 threadPosition : SV_DispatchThreadID)
{
  uint2 gid = threadPosition.xy;
  if (gid.x >= traceWidth || gid.y >= traceHeight) return;
  uint pixel = gid.y * traceWidth + gid.x;

  float4 raster = pathTracerSceneColor[gid];
  if (sceneColorSwapsRedAndBlue != 0) raster = float4(raster.b, raster.g, raster.r, raster.a);
  float rasterDepth = pathTracerSceneDepth[gid];
  float4 accumulated = pathTracerResolveAccumulation[pixel];

  float coverage = accumulated.a / max(accumulatedSamples, 1.0);

  float4 composited = raster;
  float visibleDepth = rasterDepth;
  uint cueMask = 0;

  if (coverage > 0.0)
  {
    // Averaged over the samples that hit something, so that a partly covered edge pixel is not
    // darkened before the composite below weights it by its coverage.
    float hits = max(accumulated.a, 1.0e-6);
    float4 accumulatedIndirect = pathTracerResolveIndirect[pixel];
    float3 directRadiance = accumulated.rgb / hits;
    float3 indirectRadiance = accumulatedIndirect.rgb / hits;
    float visibility = accumulatedIndirect.a / hits;

    // The raster path multiplies its baked occlusion into the whole shaded colour, direct lighting
    // included. That is not physical, but it is what the style looks like, so the same factor is
    // applied here to the direct term. The indirect term already carries its own occlusion, having
    // been collected only along rays that escaped.
    float occlusion = lerp(1.0, visibility, clamp(ambientOcclusionStrength, 0.0, 1.0));
    float3 radiance = occlusion * directRadiance + indirectRadiance;

    float4 info = pathTracerResolveSurfaceInfo[pixel];
    StructureUniformData su = pathTracerResolveStructures[uint(info.g)];
    float4 color = float4(pathTracerToneMap(radiance, uint(info.b), su, true), 1.0);

    bool tracedIsNearer = (info.r <= rasterDepth);
    composited = lerp(raster, color, tracedIsNearer ? coverage : 0.0);
    visibleDepth = tracedIsNearer ? info.r : rasterDepth;

    // The same tag the raster passes leave in the stencil, which the tracer cannot write: the
    // molecular geometry never went through a raster pass. Where the trace did not win, what shows
    // is guide geometry or the background, and neither takes a cue.
    float cueing = (uint(info.b) == PATH_TRACER_CATEGORY_RIBBON) ? su.edgeCueingRibbons
                                                                 : su.edgeCueingAtoms;
    uint mode = uint(clamp(cueing, 0.0, 3.0)) & EDGE_CUEING_STENCIL_MODE_MASK;
    cueMask = tracedIsNearer ? (EDGE_CUEING_STENCIL_CUEABLE_BIT | mode) : 0;
  }

  pathTracerComposite[gid] = composited;
  pathTracerCompositeDepth[pixel] = visibleDepth;
  pathTracerCompositeCueMask[pixel] = cueMask;
}
)foo";

  /// The whole accumulate kernel, ready to hand to DXC.
  static std::string accumulateKernelSource()
  {
    return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral
         + DirectXUniformStringLiterals::StructureUniformStructStringLiteral
         + DirectXUniformStringLiterals::LightUniformBlockStringLiteral
         + DirectXUniformStringLiterals::RGBHSVStringLiteral
         + CommonStringLiteral
         + ClipPlanesStringLiteral
         + IntersectionsStringLiteral
         + TraversalStringLiteral
         + RandomStringLiteral
         + MaterialsStringLiteral
         + LightsStringLiteral
         + AccumulateKernelStringLiteral;
  }

  /// The whole resolve kernel, ready to hand to DXC. Touches no geometry, so it takes none of the
  /// traversal.
  static std::string resolveKernelSource()
  {
    return DirectXUniformStringLiterals::StructureUniformStructStringLiteral
         + DirectXUniformStringLiterals::RGBHSVStringLiteral
         + DirectXUniformStringLiterals::EdgeCueingStencilTagStringLiteral
         + CommonStringLiteral
         + MaterialsStringLiteral
         + ResolveKernelStringLiteral;
  }

  /// The whole shadow-mask kernel, ready to hand to DXC.
  static std::string shadowMaskKernelSource()
  {
    return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral
         + DirectXUniformStringLiterals::StructureUniformStructStringLiteral
         + DirectXUniformStringLiterals::LightUniformBlockStringLiteral
         + CommonStringLiteral
         + "static const uint allLightsVisible = 0xFFu;\n"
         + ClipPlanesStringLiteral
         + IntersectionsStringLiteral
         + TraversalStringLiteral
         + ShadowMaskKernelStringLiteral;
  }
};
