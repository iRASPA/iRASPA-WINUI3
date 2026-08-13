/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomorthographicimpostershader.h"
#include <iostream>
#include "directxatomsphereshader.h"
#include "directxambientocclusionshadowmapshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/quadgeometry.h"
#include <cstddef>

DirectXAtomOrthographicImposterShader::DirectXAtomOrthographicImposterShader(DirectXAtomSphereShader &atomSphereShader)
  : _atomSphereShader(atomSphereShader)
{
}

void DirectXAtomOrthographicImposterShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomOrthographicImposterShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                       DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEAMBIENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, ambient)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEDIFFUSE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, diffuse)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESPECULAR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, specular)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXAtomOrthographicImposterShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXAtomOrthographicImposterShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = std::move(structures);
}

void DirectXAtomOrthographicImposterShader::deleteBuffers()
{
  _quadMesh = DirectXDeviceHelpers::IndexedMesh{};
}

void DirectXAtomOrthographicImposterShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  QuadGeometry quad;
  const auto vertices = quad.vertices();
  const auto indices = quad.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _quadMesh,
                                          vertices.data(), vertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          indices.data(), indices.size() * sizeof(short));
}

bool DirectXAtomOrthographicImposterShader::isQuadReady() const
{
  return _quadMesh.vertexBuffer && _quadMesh.indexBuffer && _quadMesh.indexCount > 0;
}

UINT DirectXAtomOrthographicImposterShader::quadIndexCount() const
{
  return _quadMesh.indexCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomOrthographicImposterShader::quadVbv() const
{
  return _quadMesh.vbv;
}

D3D12_INDEX_BUFFER_VIEW DirectXAtomOrthographicImposterShader::quadIbv() const
{
  return _quadMesh.ibv;
}

void DirectXAtomOrthographicImposterShader::paint(ID3D12GraphicsCommandList *commandList,
                                                  D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                  UINT structureCBVStride,
                                                  DirectXAmbientOcclusionShadowMapShader *aoShader)
{
  if (!_psoReady || !_pso)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && _quadMesh.indexCount > 0
          && _atomSphereShader.isInstanceReady(i, j)
          && _atomSphereShader.instanceCount(i, j) > 0
          && _quadMesh.vertexBuffer && _quadMesh.indexBuffer)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        if (aoShader)
          commandList->SetGraphicsRootDescriptorTable(3, aoShader->aoSrv(i, j));

        D3D12_VERTEX_BUFFER_VIEW instanceVbv = _atomSphereShader.instanceVbv(i, j);
        D3D12_VERTEX_BUFFER_VIEW views[2] = { _quadMesh.vbv, instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&_quadMesh.ibv);
        commandList->DrawIndexedInstanced(_quadMesh.indexCount, _atomSphereShader.instanceCount(i, j), 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXAtomOrthographicImposterShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceAmbientColor : INSTANCEAMBIENT;
  float4 instanceDiffuseColor : INSTANCEDIFFUSE;
  float4 instanceSpecularColor : INSTANCESPECULAR;
  float4 instanceScale : INSTANCESCALE;
  uint instanceId : SV_InstanceID;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 instancePosition : TEXCOORD2;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 specular : COLOR2;
  float3 frag_pos : TEXCOORD3;
  float3 N : NORMAL0;
  float3 L : TEXCOORD4;
  float3 V : TEXCOORD5;
  nointerpolation float4 sphere_radius : TEXCOORD6;
  nointerpolation float k1 : TEXCOORD7;
  nointerpolation float k2 : TEXCOORD8;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 scale = structureUniforms.atomScaleFactor * input.instanceScale;

  if (structureUniforms.colorAtomsWithBondColor != 0)
  {
    output.ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
    output.diffuse = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor;
    output.specular = lightUniforms.lights[0].specular * structureUniforms.bondSpecularColor;
  }
  else
  {
    output.ambient = lightUniforms.lights[0].ambient * structureUniforms.atomAmbientColor * input.instanceAmbientColor;
    output.diffuse = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceDiffuseColor;
    output.specular = lightUniforms.lights[0].specular * structureUniforms.atomSpecularColor * input.instanceSpecularColor;
  }

  output.N = float3(0, 0, 1);
  output.instancePosition = input.instancePosition;

  // Cocoa encodes atom visibility in instancePosition.w (±1). Negative w must not rasterize.
  if (input.instancePosition.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
    return output;
  }

  output.eye_position = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));

  output.L = (lightUniforms.lights[0].position - output.eye_position * lightUniforms.lights[0].position.w).xyz;
  output.V = -output.eye_position.xyz;

  output.texcoords = input.vertexPosition.xy;
  output.sphere_radius = scale;

  float4 pos2 = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  pos2.xy += scale.xy * input.vertexPosition.xy;
  output.frag_pos = pos2.xyz;

  int patchNumber = max(structureUniforms.ambientOcclusionPatchNumber, 1);
  output.k1 = (float)(input.instanceId % (uint)patchNumber);
  output.k2 = (float)(input.instanceId / (uint)patchNumber);

  float4 clip = mul(frameUniforms.projectionMatrix, pos2);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXAtomOrthographicImposterShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
Texture2D ambientOcclusionTexture : register(t0);
SamplerState ambientOcclusionSampler : register(s0);

struct PSInput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 instancePosition : TEXCOORD2;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 specular : COLOR2;
  float3 frag_pos : TEXCOORD3;
  float3 N : NORMAL0;
  float3 L : TEXCOORD4;
  float3 V : TEXCOORD5;
  nointerpolation float4 sphere_radius : TEXCOORD6;
  nointerpolation float k1 : TEXCOORD7;
  nointerpolation float k2 : TEXCOORD8;
};

struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};

float2 textureCoordinateForSphereSurfacePosition(float3 sphereSurfacePosition)
{
  float3 absoluteSphereSurfacePosition = abs(sphereSurfacePosition);
  float d = absoluteSphereSurfacePosition.x + absoluteSphereSurfacePosition.y + absoluteSphereSurfacePosition.z;
  return (sphereSurfacePosition.z > 0.0)
    ? sphereSurfacePosition.xy / d
    : sign(sphereSurfacePosition.xy) * (1.0 - absoluteSphereSurfacePosition.yx / d);
}

PSOutput PSMain(PSInput input)
{
  PSOutput output;

  float x = input.texcoords.x;
  float y = input.texcoords.y;
  float zz = 1.0 - x * x - y * y;

  if (zz <= 0.0)
    discard;

  float z = sqrt(zz);
  float4 pos = input.eye_position;
  pos.z += input.sphere_radius.z * z;

  float4x4 ambientOcclusionTransformMatrix = transpose(mul(frameUniforms.normalMatrix, structureUniforms.modelMatrix));

  if (structureUniforms.clipAtomsAtUnitCell != 0)
  {
    float3 vertexPosition = mul(ambientOcclusionTransformMatrix, (input.sphere_radius * float4(x, y, z, 1.0))).xyz;
    float4 position = float4(input.instancePosition.xyz + vertexPosition.xyz, 1.0);
    if (dot(structureUniforms.clipPlaneLeft, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneRight, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneTop, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneBottom, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneFront, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneBack, position) < 0.0) discard;
  }

  pos = mul(frameUniforms.projectionMatrix, pos);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;

  float3 N = float3(x, y, z);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float3 ambient = input.ambient.xyz;
  float3 diffuse = max(dot(N, L), 0.0) * input.diffuse.xyz;
  float3 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.atomShininess) * input.specular.xyz;

  float ao = 1.0;
  if (structureUniforms.ambientOcclusion != 0)
  {
    float patchSize = structureUniforms.ambientOcclusionPatchSize;
    float3 t1 = mul(ambientOcclusionTransformMatrix, float4(N, 1.0)).xyz;
    float2 m2 = (float2(patchSize * (input.k1 + 0.5), patchSize * (input.k2 + 0.5))
               + 0.5 * (patchSize - 1.0) * textureCoordinateForSphereSurfacePosition(t1))
               * structureUniforms.ambientOcclusionInverseTextureSize;
    ao = ambientOcclusionTexture.Sample(ambientOcclusionSampler, m2).r;
  }

  float4 color = float4(ao * (ambient + diffuse + specular), 1.0);

  if (structureUniforms.atomHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.atomHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  output.color = float4(hsv2rgb(hsv), 1.0);
  return output;
}
)foo");
