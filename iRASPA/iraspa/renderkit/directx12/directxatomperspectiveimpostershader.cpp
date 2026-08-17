/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomperspectiveimpostershader.h"
#include <iostream>
#include "directxatomsphereshader.h"
#include "directxatomorthographicimpostershader.h"
#include "directxambientocclusionshadowmapshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include <cstddef>

DirectXAtomPerspectiveImposterShader::DirectXAtomPerspectiveImposterShader(
    DirectXAtomSphereShader &atomSphereShader,
    DirectXAtomOrthographicImposterShader &orthoImposter)
  : _atomSphereShader(atomSphereShader),
    _orthoImposter(orthoImposter)
{
}

void DirectXAtomPerspectiveImposterShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomPerspectiveImposterShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, true);
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, false);
}

void DirectXAtomPerspectiveImposterShader::initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                                         bool perSample)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(pixelShaderSource(perSample), "PSMain", "ps_5_0");
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

  ComPtr<ID3D12PipelineState> &pso = perSample ? _pso : _perPixelPso;
  bool &ready = perSample ? _psoReady : _perPixelPsoReady;
  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "DirectXAtomPerspectiveImposterShader: failed to create "
              << (perSample ? "per-sample" : "per-pixel") << " PSO";
    return;
  }
  ready = true;
}

void DirectXAtomPerspectiveImposterShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = std::move(structures);
}

void DirectXAtomPerspectiveImposterShader::reloadData(ID3D12Device * /*device*/)
{
}

void DirectXAtomPerspectiveImposterShader::paint(ID3D12GraphicsCommandList *commandList,
                                                 D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                 UINT structureCBVStride,
                                                 DirectXAmbientOcclusionShadowMapShader *aoShader)
{
  const bool perSample = DirectXDeviceHelpers::perSampleImposterShading();
  ID3D12PipelineState *pso = perSample ? (_psoReady ? _pso.Get() : nullptr)
                                       : (_perPixelPsoReady ? _perPixelPso.Get() : nullptr);
  if (!pso)
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && _orthoImposter.isQuadReady()
          && _atomSphereShader.isInstanceReady(i, j)
          && _atomSphereShader.instanceCount(i, j) > 0)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        if (aoShader)
          commandList->SetGraphicsRootDescriptorTable(3, aoShader->aoSrv(i, j));

        D3D12_VERTEX_BUFFER_VIEW instanceVbv = _atomSphereShader.instanceVbv(i, j);
        D3D12_VERTEX_BUFFER_VIEW views[2] = { _orthoImposter.quadVbv(), instanceVbv };
        D3D12_INDEX_BUFFER_VIEW ibv = _orthoImposter.quadIbv();
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&ibv);
        commandList->DrawIndexedInstanced(_orthoImposter.quadIndexCount(),
                                          _atomSphereShader.instanceCount(i, j), 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXAtomPerspectiveImposterShader::_vertexShaderSource =
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
  nointerpolation float3 frag_center : TEXCOORD4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD5;
  float3 V : TEXCOORD6;
  nointerpolation float4 sphere_radius : TEXCOORD7;
  nointerpolation float k1 : TEXCOORD8;
  nointerpolation float k2 : TEXCOORD9;
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

  // Cocoa encodes atom visibility in instancePosition.w (±1). Negative w must not rasterize.
  if (input.instancePosition.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
    return output;
  }

  output.eye_position = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.instancePosition = input.instancePosition;
  output.frag_center = output.eye_position.xyz;

  output.L = (lightUniforms.lights[0].position - output.eye_position * lightUniforms.lights[0].position.w).xyz;
  output.V = -output.eye_position.xyz;

  output.texcoords = input.vertexPosition.xy;
  output.sphere_radius = scale;

  float4 pos2 = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  pos2.xy += 1.5 * scale.xy * input.vertexPosition.xy;
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

std::string DirectXAtomPerspectiveImposterShader::pixelShaderSource(bool perSample)
{
  // See the orthographic imposter: `sample` on the varyings the ray is built from promotes the
  // pixel shader to per-sample execution, so MSAA also anti-aliases the ray-traced silhouette,
  // the clipping and the depth instead of just the quad edges.
  const char *s = perSample ? "sample " : "";

  return
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
  )foo") + s + std::string(R"foo(float2 texcoords : TEXCOORD1;
  nointerpolation float4 instancePosition : TEXCOORD2;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 specular : COLOR2;
  )foo") + s + std::string(R"foo(float3 frag_pos : TEXCOORD3;
  nointerpolation float3 frag_center : TEXCOORD4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD5;
  float3 V : TEXCOORD6;
  nointerpolation float4 sphere_radius : TEXCOORD7;
  nointerpolation float k1 : TEXCOORD8;
  nointerpolation float k2 : TEXCOORD9;
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

  float4 screen_pos = mul(frameUniforms.projectionMatrix, float4(hit, 1.0));
  output.depth = 0.5 * (screen_pos.z / screen_pos.w) + 0.5;

  float3 N = normalize(hit - input.frag_center);

  float4x4 ambientOcclusionTransformMatrix = transpose(mul(frameUniforms.normalMatrix, structureUniforms.modelMatrix));

  if (structureUniforms.clipAtomsAtUnitCell != 0)
  {
    float3 vertexPosition = mul(ambientOcclusionTransformMatrix, (input.sphere_radius * float4(N, 1.0))).xyz;
    float4 position = float4(input.instancePosition.xyz + vertexPosition.xyz, 1.0);
    if (dot(structureUniforms.clipPlaneLeft, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneRight, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneTop, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneBottom, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneFront, position) < 0.0) discard;
    if (dot(structureUniforms.clipPlaneBack, position) < 0.0) discard;
  }

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
}
