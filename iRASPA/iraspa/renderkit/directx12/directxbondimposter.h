/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cmath>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>
#include "directxdevicehelpers.h"
#include "rkrenderuniforms.h"

// Bonds are drawn as ray-traced cylinder imposters: the vertex shader rasterizes a small
// view-aligned hull around each (sub-)cylinder and the pixel shader intersects the eye-space ray
// with the analytic capped cylinder. The scene pass, the picking pass and the selection overlays
// all rasterize the same hulls and run the same intersection code, so the surfaces they produce
// agree to the pixel.
namespace DirectXBondImposter
{
  // The hull is three quads: one in each end-cap plane and one on the camera-facing tangent
  // plane. Corners are in cylinder-local coordinates where x and z span the radius and y runs
  // from end A to end B.
  inline const float hullCorners[18][3] = {
    // cap quad at endpoint A
    { -1.0f, -1.0f, -1.0f },
    {  1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    { -1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f, -1.0f },
    // camera-facing quad
    { -1.0f, -1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f,  1.0f,  1.0f },
    { -1.0f,  1.0f,  1.0f },
    {  1.0f, -1.0f,  1.0f },
    // cap quad at endpoint B
    { -1.0f,  1.0f,  1.0f },
    {  1.0f,  1.0f,  1.0f },
    { -1.0f,  1.0f, -1.0f },
    {  1.0f,  1.0f, -1.0f },
    { -1.0f,  1.0f, -1.0f },
    {  1.0f,  1.0f,  1.0f }
  };

  // One hull per sub-cylinder. The displacements are in the model x/z-plane and reproduce the
  // sub-cylinder layout of the double and triple bond geometries; the radius factor is the
  // thickness of those thinner cylinders.
  inline std::vector<RKVertex> hullVertices(const std::vector<float2> &displacements, float radiusFactor)
  {
    std::vector<RKVertex> vertices;
    vertices.reserve(displacements.size() * 18);
    for (const float2 &displacement : displacements)
    {
      for (const auto &corner : hullCorners)
      {
        vertices.push_back(RKVertex(float4(corner[0], corner[1], corner[2], radiusFactor),
                                    float4(displacement.x, displacement.y, 0.0f, 0.0f), float2()));
      }
    }
    return vertices;
  }

  inline void uploadHull(ID3D12Device *device, DirectXDeviceHelpers::IndexedMesh &mesh,
                         const std::vector<float2> &displacements, float radiusFactor)
  {
    const std::vector<RKVertex> vertices = hullVertices(displacements, radiusFactor);
    std::vector<short> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), static_cast<short>(0));
    DirectXDeviceHelpers::uploadIndexedMesh(device, mesh,
                                            vertices.data(), vertices.size() * sizeof(RKVertex),
                                            sizeof(RKVertex),
                                            indices.data(), indices.size() * sizeof(short));
  }

  // The four hull meshes, one per bond type.
  struct Hulls
  {
    DirectXDeviceHelpers::IndexedMesh single;
    DirectXDeviceHelpers::IndexedMesh doubleBond;
    DirectXDeviceHelpers::IndexedMesh partialDouble;
    DirectXDeviceHelpers::IndexedMesh triple;

    void upload(ID3D12Device *device)
    {
      if (!device)
        return;
      const float dz = 0.5f * std::sqrt(3.0f);
      uploadHull(device, single, { float2(0.0f, 0.0f) }, 1.0f);
      uploadHull(device, partialDouble, { float2(0.0f, 0.0f) }, 1.0f);
      uploadHull(device, doubleBond, { float2(-1.0f, 0.0f), float2(1.0f, 0.0f) }, 0.8f);
      uploadHull(device, triple, { float2(-1.0f, -dz), float2(1.0f, -dz), float2(0.0f, dz) }, 0.8f);
    }

    bool ready() const
    {
      return single.indexCount > 0 && doubleBond.indexCount > 0
          && partialDouble.indexCount > 0 && triple.indexCount > 0;
    }
  };

  // Slot 0 is the hull mesh, slot 1 the per-bond instance buffer. The colour attributes are only
  // read by the shading and selection passes; picking uses the tag instead.
  inline void fillHullVertexElements(D3D12_INPUT_ELEMENT_DESC *out)
  {
    out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    out[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    // HLSL INSTANCEPOSITION1 == semantic INSTANCEPOSITION, index 1.
    out[2] = { "INSTANCEPOSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position1)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[3] = { "INSTANCEPOSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position2)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  }

  // POSITION, NORMAL, INSTANCEPOSITION1/2, INSTANCECOLOR1/2, INSTANCESCALE
  inline void fillShadingInputLayout(D3D12_INPUT_ELEMENT_DESC *out)
  {
    fillHullVertexElements(out);
    out[4] = { "INSTANCECOLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color1)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[5] = { "INSTANCECOLOR", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color2)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[6] = { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, scale)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  }
  inline constexpr UINT shadingInputLayoutSize = 7;

  // POSITION, NORMAL, INSTANCEPOSITION1/2, INSTANCETAG
  inline void fillPickingInputLayout(D3D12_INPUT_ELEMENT_DESC *out)
  {
    fillHullVertexElements(out);
    out[4] = { "INSTANCETAG", 0, DXGI_FORMAT_R32_SINT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, tag)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  }
  inline constexpr UINT pickingInputLayoutSize = 5;

  // The vertex-shader input attributes every imposter pass shares.
  inline const std::string HullVertexInputStringLiteral = R"foo(
  // xyz: hull corner in cylinder-local coordinates, w: radius factor of the sub-cylinder
  float4 vertexPosition : POSITION;
  // xy: displacement of the sub-cylinder in the model x/z-plane
  float4 vertexNormal : NORMAL;
  float4 instancePosition1 : INSTANCEPOSITION1;
  float4 instancePosition2 : INSTANCEPOSITION2;
)foo";

  // Places one hull vertex in eye space and hands the pixel shader the cylinder it stands in for.
  inline const std::string HullStringLiteral = R"foo(
struct BondImposterHull
{
  float3 a;
  float3 b;
  float radius;
  float3 posEye;
  // eye-space directions of the cylinder's model x and z axes, so the pixel shader can
  // reconstruct model-space coordinates of the hit point
  float3 axisX;
  float3 axisZ;
};

// The hull wraps around the camera-facing side of the cylinder, so its winding depends on the
// view and the imposter pipelines must disable culling. radiusScale inflates the whole cylinder,
// which is how the selection overlays wrap the bond they mark.
BondImposterHull bondImposterHull(float4 pos1, float4 pos2, float3 corner,
                                  float2 displacementXZ, float radiusFactor, float radiusScale)
{
  BondImposterHull hull;

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = max(length(dr), 1e-5);
  dr /= bondLength;
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));

  // The cylinder geometries map model x to -v1 and model z to -v2.
  float bondScaling = max(structureUniforms.bondScaling, 0.02);
  float3 displacement = bondScaling * (displacementXZ.x * (-v1) + displacementXZ.y * (-v2));
  hull.radius = bondScaling * radiusFactor * radiusScale;

  float4x4 modelViewMatrix = mul(frameUniforms.viewMatrix, structureUniforms.modelMatrix);
  hull.a = mul(modelViewMatrix, float4(pos1.xyz + displacement, 1.0)).xyz;
  hull.b = mul(modelViewMatrix, float4(pos2.xyz + displacement, 1.0)).xyz;
  hull.axisX = normalize(mul(modelViewMatrix, float4(-v1, 0.0)).xyz);
  hull.axisZ = normalize(mul(modelViewMatrix, float4(-v2, 0.0)).xyz);

  float3 vHalf = 0.5 * (hull.b - hull.a);
  float3 center = hull.a + vHalf;

  // Direction from the camera towards the bond.
  bool orthographic = (frameUniforms.projectionMatrix._44 > 0.5);
  float3 e = orthographic ? float3(0.0, 0.0, -1.0) : center;

  float3 u = cross(vHalf, e);
  if (dot(u, u) < 1.0e-8) u = cross(vHalf, float3(0.0, 1.0, 0.0));
  if (dot(u, u) < 1.0e-8) u = cross(vHalf, float3(1.0, 0.0, 0.0));
  u = normalize(u);
  float3 w = normalize(cross(u, normalize(vHalf)));
  if (dot(w, e) > 0.0) w = -w;

  hull.posEye = center + hull.radius * (corner.x * u + corner.z * w) + corner.y * vHalf;
  return hull;
}
)foo";

  // Ray-traces the analytic capped cylinder the hull stands in for. Returns the ray parameter t
  // (negative when the ray misses), the surface normal N and the fraction ct along the axis
  // (0 at pointA, 1 at pointB).
  inline const std::string IntersectStringLiteral = R"foo(
float cylinderIntersect(float3 ro, float3 rd, float3 a, float3 b, float r, out float3 N, out float ct)
{
  N = float3(0.0, 0.0, 1.0);
  ct = 0.0;

  float3 ba = b - a;
  float3 oc = ro - a;
  float baba = dot(ba, ba);
  float bard = dot(ba, rd);
  float baoc = dot(ba, oc);
  float k2 = baba - bard * bard;
  float k1 = baba * dot(oc, rd) - baoc * bard;
  float k0 = baba * dot(oc, oc) - baoc * baoc - r * r * baba;
  float h = k1 * k1 - k2 * k0;
  if (h < 0.0) return -1.0;
  h = sqrt(h);
  float t = (-k1 - h) / k2;

  // body of the cylinder
  float y = baoc + t * bard;
  if (y > 0.0 && y < baba)
  {
    N = (oc + t * rd - ba * y / baba) / r;
    ct = y / baba;
    return t;
  }

  // flat end-caps
  t = (((y < 0.0) ? 0.0 : baba) - baoc) / bard;
  if (abs(k1 + k2 * t) >= h) return -1.0;
  N = ba * float(sign(y)) / sqrt(baba);
  ct = (y < 0.0) ? 0.0 : 1.0;
  return t;
}
)foo";

  // Ray-traces the capped cylinder clipped by the six unit-cell planes, generating the flat caps
  // at the cell boundary analytically; this is what replaces the stencil-based cap pass. The
  // visible solid is an intersection of convex constraints (infinite cylinder, axis slab, six
  // half-spaces), so the entry point is the largest of all entry parameters and the exit point
  // the smallest of all exit parameters. The ray is traced in eye space; toStructure transforms
  // eye-space points into the structure space the clip planes live in.
  inline const std::string ClippedIntersectStringLiteral = R"foo(
float clippedCylinderIntersect(float3 ro, float3 rd, float3 a, float3 b, float r,
                               float4x4 toStructure, out float3 N, out float ct)
{
  N = float3(0.0, 0.0, 1.0);
  ct = 0.0;

  float3 ba = b - a;
  float3 oc = ro - a;
  float baba = dot(ba, ba);
  float bard = dot(ba, rd);
  float baoc = dot(ba, oc);
  float k2 = baba - bard * bard;
  float k1 = baba * dot(oc, rd) - baoc * bard;
  float k0 = baba * dot(oc, oc) - baoc * baoc - r * r * baba;

  float tmin = -1.0e30;
  float tmax = 1.0e30;

  // -1: undetermined, 0: cylinder mantle, 1: end-cap, 2..7: clip plane
  int entryType = -1;

  // infinite cylinder around the axis
  if (k2 > 1.0e-6 * baba)
  {
    float h = k1 * k1 - k2 * k0;
    if (h < 0.0) return -1.0;
    h = sqrt(h);
    tmin = (-k1 - h) / k2;
    tmax = (-k1 + h) / k2;
    entryType = 0;
  }
  else if (k0 > 0.0)
  {
    // ray (nearly) parallel to the axis and outside the cylinder
    return -1.0;
  }

  // slab between the two end-cap planes: 0 <= baoc + t * bard <= baba
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
    return -1.0;
  }

  // the six clip planes of the unit cell (in structure space)
  float4 planes[6] = { structureUniforms.clipPlaneLeft, structureUniforms.clipPlaneRight,
                       structureUniforms.clipPlaneTop, structureUniforms.clipPlaneBottom,
                       structureUniforms.clipPlaneFront, structureUniforms.clipPlaneBack };
  float4 so = mul(toStructure, float4(ro, 1.0));
  float4 sd = mul(toStructure, float4(rd, 0.0));
  for (int i = 0; i < 6; i++)
  {
    float f0 = dot(planes[i], so);
    float df = dot(planes[i], sd);
    if (abs(df) < 1.0e-8)
    {
      if (f0 < 0.0) return -1.0;
    }
    else
    {
      float tp = -f0 / df;
      if (df > 0.0)
      {
        if (tp > tmin) { tmin = tp; entryType = 2 + i; }
      }
      else
      {
        tmax = min(tmax, tp);
      }
    }
  }

  if (tmax < tmin || tmin < 0.0 || entryType < 0) return -1.0;

  float t = tmin;
  float y = baoc + t * bard;
  ct = clamp(y / baba, 0.0, 1.0);

  if (entryType == 0)
  {
    N = (oc + t * rd - ba * y / baba) / r;
  }
  else if (entryType == 1)
  {
    N = (y < 0.5 * baba) ? -ba / sqrt(baba) : ba / sqrt(baba);
  }
  else
  {
    // clipped flat cap: the outward normal is opposite to the plane's inward gradient;
    // planes transform as covectors from structure to eye space
    float4 planeEye = mul(transpose(toStructure), planes[entryType - 2]);
    N = -normalize(planeEye.xyz);
  }
  return t;
}
)foo";

  // The eye-space ray through the current fragment, in both projections.
  inline const std::string RayStringLiteral = R"foo(
  bool orthographic = (frameUniforms.projectionMatrix._44 > 0.5);
  float3 ro = orthographic ? float3(input.frag_pos.xy, 0.0) : float3(0.0, 0.0, 0.0);
  float3 rd = orthographic ? float3(0.0, 0.0, -1.0) : normalize(input.frag_pos);
)foo";

  inline const std::string ToStructureStringLiteral =
    "  float4x4 toStructure = mul(structureUniforms.inverseModelMatrix, frameUniforms.viewMatrixInverse);\n";

  // Model-space coordinates of the hit point on the unit cylinder (x and z on the unit circle for
  // mantle hits, y = ct in 0..1), matching the model normal the tessellated selection meshes used
  // to interpolate. The striped and Worley-noise overlays are textured with these.
  inline const std::string ModelCoordsStringLiteral = R"foo(
float3 bondImposterModelCoords(float3 pointA, float3 pointB, float radius,
                               float3 axisX, float3 axisZ, float3 pos, float ct)
{
  float3 axisPos = lerp(pointA, pointB, ct);
  float3 pr = (pos - axisPos) / radius;
  return float3(dot(pr, axisX), ct, dot(pr, axisZ));
}
)foo";

  // Under MSAA the ray-traced silhouette, the analytic clipping and the per-pixel depth are all
  // decided inside the pixel shader, so multisampling alone only smooths the hull edges. Marking
  // the interpolated eye-space position `sample` makes the pixel shader run once per sample and
  // anti-aliases those edges too, at the cost of shading every covered sample. The quality path
  // uses it; the fast path used while the camera is moving does not.
  inline std::string interpolatedPosition(bool perSample)
  {
    return std::string(perSample ? "  sample" : "        ") + " float3 frag_pos : TEXCOORD0;\n";
  }

  // Varyings of the scene bond imposters. The vertex output always uses the plain form; only the
  // pixel-shader input differs between the two quality paths.
  inline std::string shadingVaryings(bool perSample)
  {
    return std::string(R"foo(
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  nointerpolation float4 ambient : COLOR2;
  nointerpolation float4 specular : COLOR3;
)foo") + interpolatedPosition(perSample) + std::string(R"foo(
  nointerpolation float3 pointA : TEXCOORD1;
  nointerpolation float3 pointB : TEXCOORD2;
  nointerpolation float radius : TEXCOORD3;
)foo");
  }

  // Varyings of the picking imposters: no shading, just the identifier of the bond.
  inline const std::string PickingVaryingsStringLiteral = R"foo(
  float4 position : SV_POSITION;
  float3 frag_pos : TEXCOORD0;
  nointerpolation float3 pointA : TEXCOORD1;
  nointerpolation float3 pointB : TEXCOORD2;
  nointerpolation float radius : TEXCOORD3;
  nointerpolation int instanceId : TEXCOORD4;
)foo";

  // Varyings of the selection imposters. Like the scene imposters, plus the eye-space model axes
  // the striped and Worley-noise patterns are evaluated in. Selection effects are always shaded
  // once per pixel.
  inline const std::string SelectionVaryingsStringLiteral = R"foo(
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  nointerpolation float4 ambient : COLOR2;
  nointerpolation float4 specular : COLOR3;
  float3 frag_pos : TEXCOORD0;
  nointerpolation float3 pointA : TEXCOORD1;
  nointerpolation float3 pointB : TEXCOORD2;
  nointerpolation float radius : TEXCOORD3;
  nointerpolation float3 axisX : TEXCOORD4;
  nointerpolation float3 axisZ : TEXCOORD5;
)foo";

  inline const std::string DepthOutputStringLiteral = R"foo(
struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};
)foo";

  inline const std::string PickingOutputStringLiteral = R"foo(
struct PSOutput
{
  uint4 color : SV_TARGET;
  float depth : SV_Depth;
};
)foo";

  // D3D clip space runs z from 0 to 1, so the projected depth is remapped rather than passed on.
  inline const std::string WriteDepthStringLiteral = R"foo(
  float4 screen_pos = mul(frameUniforms.projectionMatrix, float4(pos, 1.0));
  output.depth = 0.5 * (screen_pos.z / screen_pos.w) + 0.5;
)foo";
}
