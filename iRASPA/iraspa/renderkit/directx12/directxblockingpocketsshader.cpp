/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxblockingpocketsshader.h"
#include <cstddef>
#include <iostream>
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"

void DirectXBlockingPocketsShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBlockingPocketsShader::initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  auto make = [&](D3D12_CULL_MODE cullMode, ComPtr<ID3D12PipelineState> &outPso, const char *label) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = cullMode;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    // A pocket must not hide the geometry behind it, so it tests against the depth of the opaque pass
    // but does not add itself to it: overlapping pockets then blend rather than occlude each other.
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = dsvFormat;
    psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPso))))
    {
      std::cerr << "DirectXBlockingPocketsShader: failed to create " << label << "\n";
      return false;
    }
    return true;
  };

  _psoReady = make(D3D12_CULL_MODE_FRONT, _frontCullPso, "front-cull PSO")
              && make(D3D12_CULL_MODE_BACK, _backCullPso, "back-cull PSO");
}

void DirectXBlockingPocketsShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                              DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializePSOs(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXBlockingPocketsShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _instanceBuffers.clear();
  _renderStructures = std::move(structures);
  _instanceBuffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _instanceBuffers[i].resize(_renderStructures[i].size());
}

void DirectXBlockingPocketsShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  SphereGeometry sphere(1.0, 41);
  const auto vertices = sphere.vertices();
  const auto indices = sphere.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _sphere, vertices.data(),
                                          vertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          indices.data(), indices.size() * sizeof(short));

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      InstanceBuffer &instances = _instanceBuffers[i][j];
      instances = InstanceBuffer{};

      auto *source = dynamic_cast<RKRenderBlockingPocketsSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      std::vector<RKInPerInstanceAttributesAtoms> pockets = source->renderBlockingPockets();
      if (pockets.empty())
        continue;

      DirectXDeviceHelpers::uploadInstanceBuffer(device, instances.buffer, instances.vbv,
                                                 instances.instanceCount, pockets.data(), pockets.size(),
                                                 sizeof(RKInPerInstanceAttributesAtoms));
    }
  }
}

void DirectXBlockingPocketsShader::paintTransparent(ID3D12GraphicsCommandList *commandList,
                                                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                    UINT structureCBVStride,
                                                    D3D12_GPU_VIRTUAL_ADDRESS blockingPocketCBVBase,
                                                    UINT blockingPocketCBVStride,
                                                    size_t sceneIndex, size_t movieIndex,
                                                    size_t structureIndex)
{
  if (!_psoReady || !_frontCullPso || !_backCullPso || _sphere.indexCount == 0)
    return;
  if (sceneIndex >= _renderStructures.size() || movieIndex >= _renderStructures[sceneIndex].size())
    return;
  if (sceneIndex >= _instanceBuffers.size() || movieIndex >= _instanceBuffers[sceneIndex].size())
    return;

  auto *source = dynamic_cast<RKRenderBlockingPocketsSource *>(_renderStructures[sceneIndex][movieIndex].get());
  const InstanceBuffer &instances = _instanceBuffers[sceneIndex][movieIndex];
  if (!source
      || !_renderStructures[sceneIndex][movieIndex]->isVisible()
      || !source->drawBlockingPockets()
      || instances.instanceCount == 0
      || !instances.buffer)
    return;

  commandList->SetGraphicsRootConstantBufferView(
      1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureIndex) * structureCBVStride);
  commandList->SetGraphicsRootConstantBufferView(
      DirectXDeviceHelpers::kBlockingPocketRootParameter,
      blockingPocketCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureIndex) * blockingPocketCBVStride);

  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  D3D12_VERTEX_BUFFER_VIEW views[2] = { _sphere.vbv, instances.vbv };
  commandList->IASetVertexBuffers(0, 2, views);
  commandList->IASetIndexBuffer(&_sphere.ibv);

  // The far wall of a pocket first, so that it blends underneath the near wall.
  commandList->SetPipelineState(_frontCullPso.Get());
  commandList->DrawIndexedInstanced(_sphere.indexCount, instances.instanceCount, 0, 0, 0);

  commandList->SetPipelineState(_backCullPso.Get());
  commandList->DrawIndexedInstanced(_sphere.indexCount, instances.instanceCount, 0, 0, 0);
}

const std::string DirectXBlockingPocketsShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
};

// A blocking pocket is a sphere of a given radius in Angstrom around a position in the cell, so the unit
// sphere mesh only needs the per-instance radius and centre; unlike the unit cell and the bounding box
// there is no scaling of the geometry to keep it readable, the radius is the quantity of interest.
VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = float4((input.instanceScale * input.vertexPosition + input.instancePosition).xyz, 1.0);

  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXBlockingPocketsShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::BlockingPocketUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
};

// Both faces of the sphere are drawn, so the normal is flipped on the inside to keep the far wall of a
// pocket shaded rather than black. The opacity travels in the alpha of the diffuse colour and the result
// is premultiplied by it, which is what the blend state of the transparent pass expects.
float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 V = normalize(input.V);

  LightingWeights lighting = accumulateLighting(isFrontFace ? N : -N, V, float4(-input.V, 1.0),
                                                blockingPocketUniforms.shininess);

  float3 ambient = lighting.ambient * blockingPocketUniforms.ambient.xyz;
  float3 diffuse = lighting.diffuse * blockingPocketUniforms.diffuse.xyz;
  float3 specular = lighting.specular * blockingPocketUniforms.specular.xyz;

  float4 color = float4(ambient + diffuse + specular, 1.0);
  if (blockingPocketUniforms.hdr)
  {
    color = 1.0 - exp2(-color * blockingPocketUniforms.hdrExposure);
  }

  float opacity = blockingPocketUniforms.diffuse.w;
  return opacity * float4(color.xyz, 1.0);
}
)foo");
