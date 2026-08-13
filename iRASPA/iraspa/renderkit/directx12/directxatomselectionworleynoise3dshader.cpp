/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomselectionworleynoise3dshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"
#include <cstddef>

void DirectXAtomSelectionWorleyNoise3DShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomSelectionWorleyNoise3DShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXAtomSelectionWorleyNoise3DShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXAtomSelectionWorleyNoise3DShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXAtomSelectionWorleyNoise3DShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXAtomSelectionWorleyNoise3DShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXAtomSelectionWorleyNoise3DShader::reloadData(ID3D12Device *device)
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

      std::vector<RKInPerInstanceAttributesAtoms> atomData = source->renderSelectedAtoms();
      DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                                 bufs.instanceCount, atomData.data(), atomData.size(),
                                                 sizeof(RKInPerInstanceAttributesAtoms));
    }
  }
}

bool DirectXAtomSelectionWorleyNoise3DShader::isInstanceReady(size_t i, size_t j) const
{
  return i < _buffers.size() && j < _buffers[i].size()
      && _buffers[i][j].instanceBuffer != nullptr
      && _buffers[i][j].instanceCount > 0;
}

UINT DirectXAtomSelectionWorleyNoise3DShader::instanceCount(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return 0;
  return _buffers[i][j].instanceCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::instanceVbv(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return {};
  return _buffers[i][j].instanceVbv;
}

bool DirectXAtomSelectionWorleyNoise3DShader::isSphereMeshReady() const
{
  return _sphereMesh.vertexBuffer && _sphereMesh.indexBuffer && _sphereMesh.indexCount > 0;
}

UINT DirectXAtomSelectionWorleyNoise3DShader::sphereIndexCount() const
{
  return _sphereMesh.indexCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::sphereVbv() const
{
  return _sphereMesh.vbv;
}

D3D12_INDEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::sphereIbv() const
{
  return _sphereMesh.ibv;
}

bool DirectXAtomSelectionWorleyNoise3DShader::hasGlowWork() const
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->atomSelectionStyle() == RKSelectionStyle::glow
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && isInstanceReady(i, j))
        return true;
    }
  }
  return false;
}

void DirectXAtomSelectionWorleyNoise3DShader::paint(ID3D12GraphicsCommandList *commandList,
                                                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                    UINT structureCBVStride)
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
      if (source && source->atomSelectionStyle() == RKSelectionStyle::WorleyNoise3D
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && _sphereMesh.indexCount > 0 && bufs.instanceCount > 0
          && _sphereMesh.vertexBuffer && _sphereMesh.indexBuffer && bufs.instanceBuffer)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        D3D12_VERTEX_BUFFER_VIEW views[2] = { _sphereMesh.vbv, bufs.instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&_sphereMesh.ibv);
        commandList->DrawIndexedInstanced(_sphereMesh.indexCount, bufs.instanceCount, 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXAtomSelectionWorleyNoise3DShader::_vertexShaderSource =
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
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.atomAmbientColor * input.instanceAmbientColor;
  output.diffuse = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceDiffuseColor;
  output.specular = lightUniforms.lights[0].specular * structureUniforms.atomSpecularColor * input.instanceSpecularColor;

  float4 scale = structureUniforms.atomSelectionScaling * structureUniforms.atomScaleFactor * input.instanceScale;
  float4 pos = input.instancePosition + scale * input.vertexPosition;

  output.ModelN = input.vertexNormal.xyz;
  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXAtomSelectionWorleyNoise3DShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
DirectXUniformStringLiterals::WorleyNoise3DStringLiteral +
std::string(R"foo(
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
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float4 ambient = input.ambient;
  float4 diffuse = max(dot(N, L), 0.0) * input.diffuse;
  float4 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.atomShininess) * input.specular;

  float frequency = structureUniforms.atomSelectionWorleyNoise3DFrequency;
  float jitter = structureUniforms.atomSelectionWorleyNoise3DJitter;
  float2 F = cellular3D(frequency * input.ModelN, jitter);
  float n = F.y - F.x;

  float4 color = n * (ambient + diffuse + specular);

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
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.atomSelectionIntensity;
  return float4(hsv2rgb(hsv) * bloomLevel, bloomLevel);
}
)foo");
