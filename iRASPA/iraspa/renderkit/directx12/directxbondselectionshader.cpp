/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxbondselectionshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/cappedcylindersinglebondgeometry.h"
#include "geometry/cappedcylinderdoublebondgeometry.h"
#include "geometry/cappedcylinderpartialdoublebondgeometry.h"
#include "geometry/cappedcylindertriplebondgeometry.h"
#include "skasymmetricbond.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace
{
D3D12_INPUT_ELEMENT_DESC bondSelectionInputLayout[] = {
  { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  // HLSL INSTANCEPOSITION1 == semantic INSTANCEPOSITION, index 1.
  { "INSTANCEPOSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position1)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  { "INSTANCEPOSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position2)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  { "INSTANCECOLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color1)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  { "INSTANCECOLOR", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color2)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, scale)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  { "INSTANCETYPE", 0, DXGI_FORMAT_R32_SINT, 1,
    static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, type)),
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
};

void fillOverlayBlend(D3D12_GRAPHICS_PIPELINE_STATE_DESC &psoDesc)
{
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
}

void fillCommonRasterDepth(D3D12_GRAPHICS_PIPELINE_STATE_DESC &psoDesc, DXGI_FORMAT dsvFormat)
{
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { bondSelectionInputLayout, _countof(bondSelectionInputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();
}
} // namespace

void DirectXBondSelectionShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBondSelectionShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                            DXGI_FORMAT overlayRtvFormat, DXGI_FORMAT glowRtvFormat,
                                            DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> externalVs = compileShader(_externalVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> stripesPs = compileShader(_stripesPixelShaderSource, "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> worleyPs = compileShader(_worleyPixelShaderSource, "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> glowPs = compileShader(_glowPixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !stripesPs || !worleyPs || !glowPs)
    return;

  auto createPso = [&](ComPtr<ID3D12PipelineState> &out, ID3DBlob *shaderVs, ID3DBlob *ps, DXGI_FORMAT rtv,
                       bool blend, bool &readyFlag, const char *name) {
    if (!shaderVs)
      return;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { shaderVs->GetBufferPointer(), shaderVs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    if (blend)
      fillOverlayBlend(psoDesc);
    else
      psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    fillCommonRasterDepth(psoDesc, dsvFormat);
    psoDesc.RTVFormats[0] = rtv;
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out))))
    {
      std::cerr << "DirectXBondSelectionShader: failed to create" << name;
      return;
    }
    readyFlag = true;
  };

  createPso(_stripesPso, vs.Get(), stripesPs.Get(), overlayRtvFormat, true, _stripesReady, "stripes PSO");
  createPso(_worleyPso, vs.Get(), worleyPs.Get(), overlayRtvFormat, true, _worleyReady, "worley PSO");
  createPso(_externalStripesPso, externalVs.Get(), stripesPs.Get(), overlayRtvFormat, true,
            _externalStripesReady, "external stripes PSO");
  createPso(_externalWorleyPso, externalVs.Get(), worleyPs.Get(), overlayRtvFormat, true,
            _externalWorleyReady, "external worley PSO");

  // Glow targets are single-sample (offscreen), not the MSAA scene color.
  auto createGlowPso = [&](ComPtr<ID3D12PipelineState> &out, ID3DBlob *shaderVs, bool &readyFlag,
                           const char *name) {
    if (!shaderVs)
      return;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { shaderVs->GetBufferPointer(), shaderVs->GetBufferSize() };
    psoDesc.PS = { glowPs->GetBufferPointer(), glowPs->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    fillCommonRasterDepth(psoDesc, dsvFormat);
    psoDesc.RTVFormats[0] = glowRtvFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out))))
    {
      std::cerr << "DirectXBondSelectionShader: failed to create" << name;
      return;
    }
    readyFlag = true;
  };
  createGlowPso(_glowPso, vs.Get(), _glowReady, "glow PSO");
  createGlowPso(_externalGlowPso, externalVs.Get(), _externalGlowReady, "external glow PSO");
}

void DirectXBondSelectionShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXBondSelectionShader::deleteBuffers()
{
  _internalBuffers.clear();
  _externalBuffers.clear();
}

void DirectXBondSelectionShader::generateBuffers()
{
  _internalBuffers.resize(_renderStructures.size());
  _externalBuffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    _internalBuffers[i].resize(_renderStructures[i].size());
    _externalBuffers[i].resize(_renderStructures[i].size());
  }
}

void DirectXBondSelectionShader::uploadInstances(ID3D12Device *device, MeshBuffers &bufs,
                                                 const std::vector<RKInPerInstanceAttributesBonds> &instances)
{
  bufs = MeshBuffers{};
  DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                             bufs.instanceCount, instances.data(), instances.size(),
                                             sizeof(RKInPerInstanceAttributesBonds));
}

void DirectXBondSelectionShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  CappedCylinderSingleBondGeometry singleBondCylinder(1.0, 41);
  CappedCylinderDoubleBondGeometry doubleBondCylinder(1.0, 41);
  CappedCylinderPartialDoubleBondGeometry partialDoubleBondCylinder(1.0, 41);
  CappedCylinderTripleBondGeometry tripleBondCylinder(1.0, 41);
  const auto singleVertices = singleBondCylinder.vertices();
  const auto singleIndices = singleBondCylinder.indices();
  const auto doubleVertices = doubleBondCylinder.vertices();
  const auto doubleIndices = doubleBondCylinder.indices();
  const auto partialVertices = partialDoubleBondCylinder.vertices();
  const auto partialIndices = partialDoubleBondCylinder.indices();
  const auto tripleVertices = tripleBondCylinder.vertices();
  const auto tripleIndices = tripleBondCylinder.indices();

  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshSingle,
                                          singleVertices.data(), singleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          singleIndices.data(), singleIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshDouble,
                                          doubleVertices.data(), doubleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          doubleIndices.data(), doubleIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshPartialDouble,
                                          partialVertices.data(), partialVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          partialIndices.data(), partialIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshTriple,
                                          tripleVertices.data(), tripleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          tripleIndices.data(), tripleIndices.size() * sizeof(short));

  const int32_t singleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::singleBond);
  const int32_t doubleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::doubleBond);
  const int32_t partialDoubleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::partialDoubleBond);
  const int32_t tripleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::tripleBond);

  auto reloadSet = [&](bool internal) {
    auto &bufferSet = internal ? _internalBuffers : _externalBuffers;
    for (size_t i = 0; i < _renderStructures.size(); ++i)
    {
      for (size_t j = 0; j < _renderStructures[i].size(); ++j)
      {
        StructureBondBuffers &bufs = bufferSet[i][j];
        bufs = StructureBondBuffers{};

        auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
        if (!source)
          continue;

        std::vector<RKInPerInstanceAttributesBonds> bondInstanceData =
            internal ? source->renderSelectedInternalBonds() : source->renderSelectedExternalBonds();

        uploadInstances(device, bufs.all, bondInstanceData);

        std::vector<RKInPerInstanceAttributesBonds> singleBondInstanceData;
        std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(singleBondInstanceData),
                     [singleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == singleBondType; });
        uploadInstances(device, bufs.single, singleBondInstanceData);

        std::vector<RKInPerInstanceAttributesBonds> doubleBondInstanceData;
        std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(doubleBondInstanceData),
                     [doubleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == doubleBondType; });
        uploadInstances(device, bufs.doubleBond, doubleBondInstanceData);

        std::vector<RKInPerInstanceAttributesBonds> partialDoubleBondInstanceData;
        std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(partialDoubleBondInstanceData),
                     [partialDoubleBondType](const RKInPerInstanceAttributesBonds &b) {
                       return b.type == partialDoubleBondType;
                     });
        uploadInstances(device, bufs.partialDouble, partialDoubleBondInstanceData);

        std::vector<RKInPerInstanceAttributesBonds> tripleBondInstanceData;
        std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(tripleBondInstanceData),
                     [tripleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == tripleBondType; });
        uploadInstances(device, bufs.triple, tripleBondInstanceData);
      }
    }
  };

  reloadSet(true);
  reloadSet(false);
}

void DirectXBondSelectionShader::drawMesh(ID3D12GraphicsCommandList *commandList,
                                          const DirectXDeviceHelpers::IndexedMesh &mesh,
                                          const MeshBuffers &bufs)
{
  if (mesh.indexCount == 0 || bufs.instanceCount == 0
      || !mesh.vertexBuffer || !mesh.indexBuffer || !bufs.instanceBuffer)
    return;

  D3D12_VERTEX_BUFFER_VIEW views[2] = { mesh.vbv, bufs.instanceVbv };
  commandList->IASetVertexBuffers(0, 2, views);
  commandList->IASetIndexBuffer(&mesh.ibv);
  commandList->DrawIndexedInstanced(mesh.indexCount, bufs.instanceCount, 0, 0, 0);
}

void DirectXBondSelectionShader::paintBondSet(ID3D12GraphicsCommandList *commandList,
                                              D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                              UINT structureCBVStride,
                                              const std::vector<std::vector<StructureBondBuffers>> &buffers,
                                              RKSelectionStyle style)
{
  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
      const StructureBondBuffers &bufs = buffers[i][j];
      if (source && source->bondSelectionStyle() == style
          && source->drawBonds() && _renderStructures[i][j]->isVisible())
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        if (source->isUnity())
          drawMesh(commandList, _meshSingle, bufs.all);
        else
        {
          drawMesh(commandList, _meshSingle, bufs.single);
          drawMesh(commandList, _meshDouble, bufs.doubleBond);
          drawMesh(commandList, _meshPartialDouble, bufs.partialDouble);
          drawMesh(commandList, _meshTriple, bufs.triple);
        }
      }
      ++index;
    }
  }
}

void DirectXBondSelectionShader::paintOverlays(ID3D12GraphicsCommandList *commandList,
                                               D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                               UINT structureCBVStride)
{
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  if (_stripesReady && _stripesPso)
  {
    commandList->SetPipelineState(_stripesPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _internalBuffers, RKSelectionStyle::striped);
  }
  if (_externalStripesReady && _externalStripesPso)
  {
    commandList->SetPipelineState(_externalStripesPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _externalBuffers, RKSelectionStyle::striped);
  }

  if (_worleyReady && _worleyPso)
  {
    commandList->SetPipelineState(_worleyPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _internalBuffers, RKSelectionStyle::WorleyNoise3D);
  }
  if (_externalWorleyReady && _externalWorleyPso)
  {
    commandList->SetPipelineState(_externalWorleyPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _externalBuffers, RKSelectionStyle::WorleyNoise3D);
  }
}

void DirectXBondSelectionShader::paintGlow(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride)
{
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  if (_glowReady && _glowPso)
  {
    commandList->SetPipelineState(_glowPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _internalBuffers, RKSelectionStyle::glow);
  }
  if (_externalGlowReady && _externalGlowPso)
  {
    commandList->SetPipelineState(_externalGlowPso.Get());
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _externalBuffers, RKSelectionStyle::glow);
  }
}

bool DirectXBondSelectionShader::hasGlowWork() const
{
  auto hasInstances = [](const MeshBuffers &bufs) { return bufs.instanceCount > 0; };
  auto scan = [&](const std::vector<std::vector<StructureBondBuffers>> &buffers) {
    for (size_t i = 0; i < _renderStructures.size(); ++i)
    {
      for (size_t j = 0; j < _renderStructures[i].size(); ++j)
      {
        auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
        if (!source || source->bondSelectionStyle() != RKSelectionStyle::glow
            || !source->drawBonds() || !_renderStructures[i][j]->isVisible())
          continue;
        const StructureBondBuffers &bufs = buffers[i][j];
        if (source->isUnity() ? hasInstances(bufs.all)
                              : (hasInstances(bufs.single) || hasInstances(bufs.doubleBond)
                                 || hasInstances(bufs.partialDouble) || hasInstances(bufs.triple)))
          return true;
      }
    }
    return false;
  };
  return scan(_internalBuffers) || scan(_externalBuffers);
}

const std::string DirectXBondSelectionShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition1 : INSTANCEPOSITION1;
  float4 instancePosition2 : INSTANCEPOSITION2;
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
  int instanceType : INSTANCETYPE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  nointerpolation float4 ambient : COLOR3;
  nointerpolation float4 specular : COLOR4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ModelN : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 scale = input.instanceScale;
  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = length(dr);

  output.mixValue.x = clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.x;
  output.mixValue.y = input.vertexPosition.y;
  output.mixValue.z = 1.0 - clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.z;
  output.mixValue.w = scale.x / scale.z;
  output.ModelN = input.vertexPosition.xyz;

  scale.x = structureUniforms.bondScaling;
  scale.y = bondLength;
  scale.z = structureUniforms.bondScaling;
  scale.w = 1.0;

  float4 scaleFactor = float4(1.01 * structureUniforms.bondSelectionScaling, 1.0,
                              1.01 * structureUniforms.bondSelectionScaling, 1.0);
  float4 pos;
  if (input.instanceType == 1)
    pos = (input.vertexPosition - float4(sign(input.vertexPosition.x), 0.0, 0.0, 0.0)) * scaleFactor
          + float4(sign(input.vertexPosition.x), 0.0, 0.0, 0.0);
  else if (input.instanceType == 2)
  {
    if (input.vertexPosition.x < 0.0 && input.vertexPosition.z < 0.0)
      pos = (input.vertexPosition + float4(1.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            - float4(1.0, 0.0, 0.5 * sqrt(3.0), 0.0);
    else if (input.vertexPosition.x > 0.0 && input.vertexPosition.z < 0.0)
      pos = (input.vertexPosition + float4(-1.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            - float4(-1.0, 0.0, 0.5 * sqrt(3.0), 0.0);
    else
      pos = (input.vertexPosition - float4(0.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            + float4(0.0, 0.0, 0.5 * sqrt(3.0), 0.0);
  }
  else
    pos = input.vertexPosition * scaleFactor;

  dr = normalize(dr);
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));
  float4x4 orientationMatrix = transpose(float4x4(
      float4(-v1.x, -v1.y, -v1.z, 0),
      float4(-dr.x, -dr.y, -dr.z, 0),
      float4(-v2.x, -v2.y, -v2.z, 0),
      float4(0, 0, 0, 1)));

  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
  output.specular = lightUniforms.lights[0].specular * structureUniforms.bondSpecularColor;
  if (structureUniforms.bondColorMode == 0)
  {
    output.color1 = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor * input.instanceColor1;
    output.color2 = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor * input.instanceColor2;
  }
  else
  {
    output.color1 = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceColor1;
    output.color2 = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceColor2;
  }

  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, mul(orientationMatrix, input.vertexNormal))).xyz;

  float4 worldPos = mul(orientationMatrix, scale * pos) + pos1;
  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, float4(worldPos.xyz, 1.0)));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, float4(worldPos.xyz, 1.0)));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXBondSelectionShader::_externalVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition1 : INSTANCEPOSITION1;
  float4 instancePosition2 : INSTANCEPOSITION2;
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
  int instanceType : INSTANCETYPE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  nointerpolation float4 ambient : COLOR3;
  nointerpolation float4 specular : COLOR4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ModelN : TEXCOORD2;
  // D3D allows only SV_ClipDistance0/1; pack 6 planes into float4 + float2.
  float4 clip0123 : SV_ClipDistance0;
  float2 clip45 : SV_ClipDistance1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 scale = input.instanceScale;
  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = length(dr);

  output.mixValue.x = clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.x;
  output.mixValue.y = input.vertexPosition.y;
  output.mixValue.z = 1.0 - clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.z;
  output.mixValue.w = scale.x / scale.z;
  output.ModelN = input.vertexPosition.xyz;

  scale.x = structureUniforms.bondScaling;
  scale.y = bondLength;
  scale.z = structureUniforms.bondScaling;
  scale.w = 1.0;

  float4 scaleFactor = float4(1.01 * structureUniforms.bondSelectionScaling, 1.0,
                              1.01 * structureUniforms.bondSelectionScaling, 1.0);
  float4 pos;
  if (input.instanceType == 1)
    pos = (input.vertexPosition - float4(sign(input.vertexPosition.x), 0.0, 0.0, 0.0)) * scaleFactor
          + float4(sign(input.vertexPosition.x), 0.0, 0.0, 0.0);
  else if (input.instanceType == 2)
  {
    if (input.vertexPosition.x < 0.0 && input.vertexPosition.z < 0.0)
      pos = (input.vertexPosition + float4(1.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            - float4(1.0, 0.0, 0.5 * sqrt(3.0), 0.0);
    else if (input.vertexPosition.x > 0.0 && input.vertexPosition.z < 0.0)
      pos = (input.vertexPosition + float4(-1.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            - float4(-1.0, 0.0, 0.5 * sqrt(3.0), 0.0);
    else
      pos = (input.vertexPosition - float4(0.0, 0.0, 0.5 * sqrt(3.0), 0.0)) * scaleFactor
            + float4(0.0, 0.0, 0.5 * sqrt(3.0), 0.0);
  }
  else
    pos = input.vertexPosition * scaleFactor;

  dr = normalize(dr);
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));
  float4x4 orientationMatrix = transpose(float4x4(
      float4(-v1.x, -v1.y, -v1.z, 0),
      float4(-dr.x, -dr.y, -dr.z, 0),
      float4(-v2.x, -v2.y, -v2.z, 0),
      float4(0, 0, 0, 1)));

  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
  output.specular = lightUniforms.lights[0].specular * structureUniforms.bondSpecularColor;
  if (structureUniforms.bondColorMode == 0)
  {
    output.color1 = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor * input.instanceColor1;
    output.color2 = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor * input.instanceColor2;
  }
  else
  {
    output.color1 = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceColor1;
    output.color2 = lightUniforms.lights[0].diffuse * structureUniforms.atomDiffuseColor * input.instanceColor2;
  }

  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, mul(orientationMatrix, input.vertexNormal))).xyz;

  float4 worldPos = mul(orientationMatrix, scale * pos) + pos1;
  float4 objectPos = float4(worldPos.xyz, 1.0);
  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, objectPos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, objectPos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;

  output.clip0123 = float4(
      dot(structureUniforms.clipPlaneBack, objectPos),
      dot(structureUniforms.clipPlaneBottom, objectPos),
      dot(structureUniforms.clipPlaneLeft, objectPos),
      dot(structureUniforms.clipPlaneFront, objectPos));
  output.clip45 = float2(
      dot(structureUniforms.clipPlaneTop, objectPos),
      dot(structureUniforms.clipPlaneRight, objectPos));
  return output;
}
)foo");

const std::string DirectXBondSelectionShader::_stripesPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  nointerpolation float4 ambient : COLOR3;
  nointerpolation float4 specular : COLOR4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ModelN : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);

  float4 color = max(dot(N, L), 0.0) * float4(1.0, 1.0, 0.0, 1.0);

  float3 t1 = input.ModelN;
  float2 st = float2(0.5 + 0.5 * atan2(t1.x, t1.z) / 3.141592653589793, t1.y);
  float uDensity = structureUniforms.bondSelectionStripesDensity;
  float frequency = structureUniforms.bondSelectionStripesFrequency;
  if (frac(st.x * frequency) >= uDensity && frac(st.y * frequency) >= uDensity)
    discard;

  if (structureUniforms.bondHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.bondHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.bondSelectionIntensity;
  return bloomLevel * float4(hsv2rgb(hsv), 1.0);
}
)foo");

const std::string DirectXBondSelectionShader::_worleyPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
DirectXUniformStringLiterals::WorleyNoise3DStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  nointerpolation float4 ambient : COLOR3;
  nointerpolation float4 specular : COLOR4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ModelN : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float4 ambient = input.ambient;
  float d = max(dot(N, L), 0.0);
  float4 diffuse = float4(d, d, d, 1.0);
  float4 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.atomShininess) * input.specular;
  float t = clamp((input.mixValue.y - input.mixValue.x) / (input.mixValue.z - input.mixValue.x), 0.0, 1.0);

  if (structureUniforms.bondColorMode == 0)
    diffuse *= lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor;
  else if (structureUniforms.bondColorMode == 1)
    diffuse *= (t < 0.5 ? input.color1 : input.color2);
  else if (structureUniforms.bondColorMode == 2)
    diffuse *= lerp(input.color1, input.color2, smoothstep(0.0, 1.0, t));

  float frequency = structureUniforms.bondSelectionWorleyNoise3DFrequency;
  float jitter = structureUniforms.bondSelectionWorleyNoise3DJitter;
  float2 F = cellular3D(frequency * input.ModelN, jitter);
  float n = F.y - F.x;
  float4 color = n * (ambient + diffuse + specular);

  if (structureUniforms.bondHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.bondHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.bondSelectionIntensity;
  return bloomLevel * float4(hsv2rgb(hsv), 1.0);
}
)foo");

const std::string DirectXBondSelectionShader::_glowPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  nointerpolation float4 ambient : COLOR3;
  nointerpolation float4 specular : COLOR4;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ModelN : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float4 ambient = input.ambient;
  float4 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.bondShininess) * input.specular;
  float d = max(dot(N, L), 0.0);
  float4 diffuse = float4(d, d, d, 1.0);
  float t = clamp((input.mixValue.y - input.mixValue.x) / (input.mixValue.z - input.mixValue.x), 0.0, 1.0);

  if (structureUniforms.bondColorMode == 0)
    diffuse *= lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor;
  else if (structureUniforms.bondColorMode == 1)
    diffuse *= (t < 0.5 ? input.color1 : input.color2);
  else if (structureUniforms.bondColorMode == 2)
    diffuse *= lerp(input.color1, input.color2, smoothstep(0.0, 1.0, t));

  float4 color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);

  if (structureUniforms.bondHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.bondHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.bondSelectionIntensity;
  return bloomLevel * float4(hsv2rgb(hsv), 1.0);
}
)foo");
