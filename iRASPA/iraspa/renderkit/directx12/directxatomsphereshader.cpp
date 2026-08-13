/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomsphereshader.h"
#include <iostream>
#include "directxambientocclusionshadowmapshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"
#include <cstddef>

void DirectXAtomSphereShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomSphereShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  // OpenGL meshes are CCW-front; match with FrontCounterClockwise.
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
    std::cerr << "DirectXAtomSphereShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXAtomSphereShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXAtomSphereShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXAtomSphereShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXAtomSphereShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  SphereGeometry sphere(1.0, 41);
  const auto vertices = sphere.vertices();
  const auto indices = sphere.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _sphereMesh,
                                          vertices.data(), vertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          indices.data(), indices.size() * sizeof(short));

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      StructureBuffers &bufs = _buffers[i][j];
      bufs = StructureBuffers{};

      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      std::vector<RKInPerInstanceAttributesAtoms> atomData = source->renderAtoms();
      DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                                 bufs.instanceCount, atomData.data(), atomData.size(),
                                                 sizeof(RKInPerInstanceAttributesAtoms));
    }
  }
}

bool DirectXAtomSphereShader::isInstanceReady(size_t i, size_t j) const
{
  return i < _buffers.size() && j < _buffers[i].size()
      && _buffers[i][j].instanceBuffer != nullptr
      && _buffers[i][j].instanceCount > 0;
}

UINT DirectXAtomSphereShader::instanceCount(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return 0;
  return _buffers[i][j].instanceCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSphereShader::instanceVbv(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return {};
  return _buffers[i][j].instanceVbv;
}

bool DirectXAtomSphereShader::isSphereMeshReady() const
{
  return _sphereMesh.vertexBuffer && _sphereMesh.indexBuffer && _sphereMesh.indexCount > 0;
}

UINT DirectXAtomSphereShader::sphereIndexCount() const
{
  return _sphereMesh.indexCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSphereShader::sphereVbv() const
{
  return _sphereMesh.vbv;
}

D3D12_INDEX_BUFFER_VIEW DirectXAtomSphereShader::sphereIbv() const
{
  return _sphereMesh.ibv;
}

void DirectXAtomSphereShader::paint(ID3D12GraphicsCommandList *commandList,
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
      const StructureBuffers &bufs = _buffers[i][j];
      if (source && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && _sphereMesh.indexCount > 0 && bufs.instanceCount > 0
          && _sphereMesh.vertexBuffer && _sphereMesh.indexBuffer && bufs.instanceBuffer)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        if (aoShader)
          commandList->SetGraphicsRootDescriptorTable(3, aoShader->aoSrv(i, j));

        D3D12_VERTEX_BUFFER_VIEW views[2] = { _sphereMesh.vbv, bufs.instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&_sphereMesh.ibv);
        commandList->DrawIndexedInstanced(_sphereMesh.indexCount, bufs.instanceCount, 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXAtomSphereShader::_vertexShaderSource =
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
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 specular : COLOR2;
  float3 N : NORMAL0;
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  nointerpolation float k1 : TEXCOORD2;
  nointerpolation float k2 : TEXCOORD3;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;

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

  float4 scale = structureUniforms.atomScaleFactor * input.instanceScale;
  float4 pos = input.instancePosition + scale * input.vertexPosition;

  // Cocoa encodes atom visibility in instancePosition.w (±1). Negative w must not rasterize.
  if (input.instancePosition.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
    return output;
  }

  output.ModelN = input.vertexNormal.xyz;
  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  int patchNumber = max(structureUniforms.ambientOcclusionPatchNumber, 1);
  output.k1 = (float)(input.instanceId % (uint)patchNumber);
  output.k2 = (float)(input.instanceId / (uint)patchNumber);

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  // OpenGL NDC Z [-1,1] -> D3D [0,1]
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXAtomSphereShader::_pixelShaderSource =
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
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 specular : COLOR2;
  float3 N : NORMAL0;
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  nointerpolation float k1 : TEXCOORD2;
  nointerpolation float k2 : TEXCOORD3;
};

float2 textureCoordinateForSphereSurfacePosition(float3 sphereSurfacePosition)
{
  float3 absoluteSphereSurfacePosition = abs(sphereSurfacePosition);
  float d = absoluteSphereSurfacePosition.x + absoluteSphereSurfacePosition.y + absoluteSphereSurfacePosition.z;
  return (sphereSurfacePosition.z > 0.0)
    ? sphereSurfacePosition.xy / d
    : sign(sphereSurfacePosition.xy) * (1.0 - absoluteSphereSurfacePosition.yx / d);
}

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
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
    float3 t1 = input.ModelN;
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
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");
