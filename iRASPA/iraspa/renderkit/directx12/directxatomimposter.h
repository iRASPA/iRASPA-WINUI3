/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <cstddef>
#include <string>
#include "directxdevicehelpers.h"
#include "rkrenderuniforms.h"

// Atoms are drawn as ray-traced sphere imposters: the vertex shader rasterizes a view-aligned quad
// around each atom and the pixel shader intersects the eye-space ray with the analytic sphere. The
// scene pass, the picking pass and the selection overlays all rasterize the same quad and solve the
// same intersection, so the surfaces they produce agree to the pixel.
//
// The intersection is written twice, once per projection, because the two are genuinely different:
// under an orthographic projection all rays are parallel to the view axis and the quad's texture
// coordinates already are the sphere's x and y, while under perspective the ray has to be traced
// from the eye. Which of the two a pass uses follows the camera, not the render quality.
namespace DirectXAtomImposter
{
  // Slot 0 is the imposter quad, slot 1 the per-atom instance buffer.
  inline void fillSelectionInputLayout(D3D12_INPUT_ELEMENT_DESC *out)
  {
    out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    out[1] = { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[2] = { "INSTANCEAMBIENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, ambient)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[3] = { "INSTANCEDIFFUSE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, diffuse)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[4] = { "INSTANCESPECULAR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, specular)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
    out[5] = { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
               static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
               D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  }
  inline constexpr UINT selectionInputLayoutSize = 6;

  inline const std::string SelectionVertexInputStringLiteral = R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceAmbientColor : INSTANCEAMBIENT;
  float4 instanceDiffuseColor : INSTANCEDIFFUSE;
  float4 instanceSpecularColor : INSTANCESPECULAR;
  float4 instanceScale : INSTANCESCALE;
};
)foo";

  // Varyings of the selection overlays. frag_center and frag_pos are only read by the perspective
  // intersection, texcoords and eye_position only by the orthographic one; both are always filled
  // so the two projections can share one vertex shader body and one struct.
  //
  // Under `perSample` the pixel shader runs once per MSAA sample and the intersection is solved at
  // each sample's own position rather than at the pixel centre, so the depth it writes is the
  // sphere's depth there. Only the two varyings the intersection reads need to move: texcoords
  // under an orthographic projection and frag_pos under a perspective one. A vertex output always
  // uses the plain form, the rate being a property of the pixel stage alone.
  inline std::string selectionVaryings(bool perSample)
  {
    const std::string rate = perSample ? "  sample " : "  ";
    return
"  float4 position : SV_POSITION;\n"
"  float4 eye_position : TEXCOORD0;\n" +
rate + "float2 texcoords : TEXCOORD1;\n"
"  nointerpolation float4 ambient : COLOR0;\n"
"  nointerpolation float4 diffuse : COLOR1;\n"
"  nointerpolation float4 specular : COLOR2;\n" +
rate + "float3 frag_pos : TEXCOORD2;\n"
"  nointerpolation float3 frag_center : TEXCOORD3;\n"
"  float3 V : TEXCOORD5;\n"
"  nointerpolation float4 sphere_radius : TEXCOORD6;\n";
  }

  // The overlays wrap the atom they mark, so the sphere is inflated by atomSelectionScaling. The
  // perspective quad is widened further because a sphere seen in perspective projects to an
  // ellipse that is larger than the sphere's own diameter.
  inline std::string selectionVertexShaderBody(bool orthographic)
  {
    return std::string(R"foo(
VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 scale = structureUniforms.atomSelectionScaling * structureUniforms.atomScaleFactor * input.instanceScale;
  // Material colours only: the pixel stage sums the rig, so a light colour folded in here would be
  // applied once per light.
  output.ambient = structureUniforms.atomAmbientColor * input.instanceAmbientColor;
  output.diffuse = structureUniforms.atomDiffuseColor * input.instanceDiffuseColor;
  output.specular = structureUniforms.atomSpecularColor * input.instanceSpecularColor;

  output.eye_position = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.frag_center = output.eye_position.xyz;
  output.V = -output.eye_position.xyz;

  output.texcoords = input.vertexPosition.xy;
  output.sphere_radius = scale;

  float4 pos2 = output.eye_position;
  pos2.xy += )foo") + (orthographic ? "" : "1.5 * ") + std::string(R"foo(scale.xy * input.vertexPosition.xy;
  output.frag_pos = pos2.xyz;

  float4 clip = mul(frameUniforms.projectionMatrix, pos2);
  // OpenGL NDC z runs from -1 to 1, D3D from 0 to 1.
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");
  }

  // Solves the ray/sphere intersection for the current fragment, discarding where the ray misses.
  // Leaves the eye-space normal in N and writes the hit's depth to output.depth; the caller only
  // has to shade. The orthographic form reads the sphere's surface straight off the quad, which is
  // exact because the rays are parallel.
  inline std::string hitStringLiteral(bool orthographic)
  {
    if (orthographic)
    {
      return R"foo(
  float x = input.texcoords.x;
  float y = input.texcoords.y;
  float zz = 1.0 - x * x - y * y;
  if (zz <= 0.0)
    discard;

  float z = sqrt(zz);
  float3 N = float3(x, y, z);

  // The point actually shaded is on the sphere, not at its centre, which is what a positional or
  // spot light has to be measured from.
  float4 surfaceEyePosition = input.eye_position;
  surfaceEyePosition.z += input.sphere_radius.z * z;

  float4 pos = mul(frameUniforms.projectionMatrix, surfaceEyePosition);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;
)foo";
    }

    return R"foo(
  float3 rij = -input.frag_center;
  float3 vij = input.frag_pos;

  float A = dot(vij, vij);
  float B = dot(rij, vij);
  float C = dot(rij, rij) - input.sphere_radius.z * input.sphere_radius.z;
  float argument = B * B - A * C;
  if (argument < 0.0)
    discard;

  float t = -C / (B - sqrt(argument));
  float3 hit = t * vij;
  float3 N = normalize(hit - input.frag_center);

  // Named to match the orthographic branch, so a shader that shades from it reads the same either
  // way; here the ray-sphere hit is already the point on the surface.
  float4 surfaceEyePosition = float4(hit, 1.0);

  float4 pos = mul(frameUniforms.projectionMatrix, surfaceEyePosition);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;
)foo";
  }

  // Model-space direction of the hit point, which is what the striped and Worley-noise patterns
  // are evaluated in so that they stay glued to the atom as the camera moves.
  inline const std::string ModelNormalStringLiteral = R"foo(
  float4x4 modelNormalMatrix = transpose(mul(frameUniforms.normalMatrix, structureUniforms.modelMatrix));
  float3 t1 = mul(modelNormalMatrix, float4(N, 1.0)).xyz;
)foo";

  inline const std::string DepthOutputStringLiteral = R"foo(
struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};
)foo";
}
