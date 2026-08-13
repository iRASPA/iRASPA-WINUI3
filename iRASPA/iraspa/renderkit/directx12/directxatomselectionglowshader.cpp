/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomselectionglowshader.h"
#include <iostream>
#include "directxatomselectionworleynoise3dshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/quadgeometry.h"
#include <cstddef>

void DirectXAtomSelectionGlowShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomSelectionGlowShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEAMBIENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, ambient)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEDIFFUSE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, diffuse)),
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
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc.Count = 1;

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXAtomSelectionGlowShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXAtomSelectionGlowShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = std::move(structures);
}

void DirectXAtomSelectionGlowShader::reloadData(ID3D12Device *device)
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

void DirectXAtomSelectionGlowShader::paint(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride,
                                           const DirectXAtomSelectionWorleyNoise3DShader &instanceSource)
{
  if (!_psoReady || !_pso || _quadMesh.indexCount == 0)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->atomSelectionStyle() == RKSelectionStyle::glow
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && instanceSource.isInstanceReady(i, j))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        D3D12_VERTEX_BUFFER_VIEW views[2] = { _quadMesh.vbv, instanceSource.instanceVbv(i, j) };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&_quadMesh.ibv);
        commandList->DrawIndexedInstanced(_quadMesh.indexCount, instanceSource.instanceCount(i, j), 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXAtomSelectionGlowShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceAmbientColor : INSTANCEAMBIENT;
  float4 instanceDiffuseColor : INSTANCEDIFFUSE;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float4 eyePosition : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 sphereRadius : COLOR2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 scale = structureUniforms.atomSelectionScaling * structureUniforms.atomScaleFactor * input.instanceScale;
  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.atomAmbientColor * input.instanceAmbientColor;
  output.diffuse = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceDiffuseColor;
  output.eyePosition = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.texcoords = input.vertexPosition.xy;
  output.sphereRadius = scale;

  float4 pos2 = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  pos2.xy += scale.xy * float2(input.vertexPosition.x, input.vertexPosition.y);

  float4 clip = mul(frameUniforms.projectionMatrix, pos2);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXAtomSelectionGlowShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float4 eyePosition : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  nointerpolation float4 sphereRadius : COLOR2;
};

struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};

PSOutput PSMain(PSInput input)
{
  PSOutput output;
  float x = input.texcoords.x;
  float y = input.texcoords.y;
  float zz = 1.0 - x * x - y * y;
  if (zz <= 0.0)
    discard;

  float z = sqrt(zz);
  float4 pos = input.eyePosition;
  pos.z += input.sphereRadius.z * z;
  pos = mul(frameUniforms.projectionMatrix, pos);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;
  output.color = float4(structureUniforms.atomSelectionIntensity * (input.ambient.xyz + input.diffuse.xyz), 1.0);
  return output;
}
)foo");
