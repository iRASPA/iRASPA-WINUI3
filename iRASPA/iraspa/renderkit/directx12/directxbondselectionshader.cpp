/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxbondselectionshader.h"
#include <iostream>
#include "directxbondimposter.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "skasymmetricbond.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace
{
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

void fillCommonRasterDepth(D3D12_GRAPHICS_PIPELINE_STATE_DESC &psoDesc, DXGI_FORMAT dsvFormat,
                           const D3D12_INPUT_ELEMENT_DESC *inputLayout)
{
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  // The imposter hull is built in the vertex shader with view-dependent winding.
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  // The overlay writes its own ray-traced depth but must not disturb the scene depth buffer.
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, DirectXBondImposter::shadingInputLayoutSize };
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
  ComPtr<ID3DBlob> stripesPs = compileShader(pixelShaderSource(Style::striped, false), "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> worleyPs = compileShader(pixelShaderSource(Style::worleyNoise3D, false), "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> glowPs = compileShader(pixelShaderSource(Style::glow, false), "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> externalStripesPs = compileShader(pixelShaderSource(Style::striped, true), "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> externalWorleyPs = compileShader(pixelShaderSource(Style::worleyNoise3D, true), "PSMain", "ps_5_0");
  ComPtr<ID3DBlob> externalGlowPs = compileShader(pixelShaderSource(Style::glow, true), "PSMain", "ps_5_0");
  if (!vs || !stripesPs || !worleyPs || !glowPs)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[DirectXBondImposter::shadingInputLayoutSize];
  DirectXBondImposter::fillShadingInputLayout(inputLayout);

  auto createPso = [&](ComPtr<ID3D12PipelineState> &out, ID3DBlob *ps, DXGI_FORMAT rtv,
                       bool blend, bool &readyFlag, const char *name) {
    if (!ps)
      return;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    if (blend)
      fillOverlayBlend(psoDesc);
    else
      psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    fillCommonRasterDepth(psoDesc, dsvFormat, inputLayout);
    psoDesc.RTVFormats[0] = rtv;
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out))))
    {
      std::cerr << "DirectXBondSelectionShader: failed to create" << name;
      return;
    }
    readyFlag = true;
  };

  createPso(_stripesPso, stripesPs.Get(), overlayRtvFormat, true, _stripesReady, "stripes PSO");
  createPso(_worleyPso, worleyPs.Get(), overlayRtvFormat, true, _worleyReady, "worley PSO");
  createPso(_externalStripesPso, externalStripesPs.Get(), overlayRtvFormat, true,
            _externalStripesReady, "external stripes PSO");
  createPso(_externalWorleyPso, externalWorleyPs.Get(), overlayRtvFormat, true,
            _externalWorleyReady, "external worley PSO");

  // Glow targets are single-sample (offscreen), not the MSAA scene color.
  auto createGlowPso = [&](ComPtr<ID3D12PipelineState> &out, ID3DBlob *ps, bool &readyFlag,
                           const char *name) {
    if (!ps)
      return;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    fillCommonRasterDepth(psoDesc, dsvFormat, inputLayout);
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
  createGlowPso(_glowPso, glowPs.Get(), _glowReady, "glow PSO");
  createGlowPso(_externalGlowPso, externalGlowPs.Get(), _externalGlowReady, "external glow PSO");
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

  if (!_hulls.ready())
    _hulls.upload(device);

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
  if (!_hulls.ready())
    return;

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
          drawMesh(commandList, _hulls.single, bufs.all);
        else
        {
          drawMesh(commandList, _hulls.single, bufs.single);
          drawMesh(commandList, _hulls.doubleBond, bufs.doubleBond);
          drawMesh(commandList, _hulls.partialDouble, bufs.partialDouble);
          drawMesh(commandList, _hulls.triple, bufs.triple);
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
)foo") + DirectXBondImposter::HullVertexInputStringLiteral + std::string(R"foo(
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
)foo") + DirectXBondImposter::SelectionVaryingsStringLiteral + std::string(R"foo(
};
)foo") + DirectXBondImposter::HullStringLiteral + std::string(R"foo(
VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
  output.specular = lightUniforms.lights[0].specular * structureUniforms.bondSpecularColor;
  float4 diffuseColor = (structureUniforms.bondColorMode == 0) ? structureUniforms.bondDiffuseColor
                                                               : structureUniforms.atomDiffuseColor;
  output.color1 = lightUniforms.lights[0].diffuse * diffuseColor * input.instanceColor1;
  output.color2 = lightUniforms.lights[0].diffuse * diffuseColor * input.instanceColor2;

  // The overlay is a slightly fatter copy of the bond, so it wraps the surface it marks.
  float radiusScale = 1.01 * structureUniforms.bondSelectionScaling;
  BondImposterHull hull = bondImposterHull(pos1, pos2, input.vertexPosition.xyz,
                                           input.vertexNormal.xy, input.vertexPosition.w, radiusScale);
  output.frag_pos = hull.posEye;
  output.pointA = hull.a;
  output.pointB = hull.b;
  output.radius = hull.radius;
  output.axisX = hull.axisX;
  output.axisZ = hull.axisZ;

  float4 clip = mul(frameUniforms.projectionMatrix, float4(hull.posEye, 1.0));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;

  // Bond visibility is encoded in position.w (±1); collapse the whole hull.
  if (pos1.w < 0.0 || pos2.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
  }
  return output;
}
)foo");

std::string DirectXBondSelectionShader::pixelShaderSource(Style style, bool external)
{
  // Shading of the ray-traced overlay surface, shared by the glow and Worley-noise styles.
  const std::string shade = R"foo(
float4 bondSelectionShade(PSInput input, float3 pos, float3 N, float ct)
{
  float3 L = normalize((lightUniforms.lights[0].position - float4(pos, 1.0) * lightUniforms.lights[0].position.w).xyz);
  float3 V = normalize(-pos);
  float3 R = reflect(-L, N);

  float4 ambient = input.ambient;
  float4 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.bondShininess)
                    * input.specular;
  float d = max(dot(N, L), 0.0);
  float4 diffuse = float4(d, d, d, 1.0);

  if (structureUniforms.bondColorMode == 0)
    diffuse *= lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor;
  else if (structureUniforms.bondColorMode == 1)
    diffuse *= (ct < 0.5 ? input.color1 : input.color2);
  else if (structureUniforms.bondColorMode == 2)
    diffuse *= lerp(input.color1, input.color2, smoothstep(0.0, 1.0, ct));

  return float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
}
)foo";

  const std::string trace = external
      ? DirectXBondImposter::ToStructureStringLiteral +
        std::string("  float t = clippedCylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius,\n"
                    "                                     toStructure, N, ct);\n")
      : std::string("  float t = cylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius, N, ct);\n");

  std::string body;
  switch (style)
  {
    case Style::striped:
      body = R"foo(
  float3 t1 = bondImposterModelCoords(input.pointA, input.pointB, input.radius,
                                      input.axisX, input.axisZ, pos, ct);
  float2 st = float2(0.5 + 0.5 * atan2(t1.x, t1.z) / 3.141592653589793, t1.y);
  float uDensity = structureUniforms.bondSelectionStripesDensity;
  float stripesFrequency = structureUniforms.bondSelectionStripesFrequency;
  if (frac(st.x * stripesFrequency) >= uDensity && frac(st.y * stripesFrequency) >= uDensity)
    discard;

  float3 L = normalize((lightUniforms.lights[0].position - float4(pos, 1.0) * lightUniforms.lights[0].position.w).xyz);
  float4 color = max(dot(N, L), 0.0) * float4(1.0, 1.0, 0.0, 1.0);
)foo";
      break;
    case Style::worleyNoise3D:
      body = R"foo(
  float3 t1 = bondImposterModelCoords(input.pointA, input.pointB, input.radius,
                                      input.axisX, input.axisZ, pos, ct);
  float noiseFrequency = structureUniforms.bondSelectionWorleyNoise3DFrequency;
  float jitter = structureUniforms.bondSelectionWorleyNoise3DJitter;
  float2 F = cellular3D(noiseFrequency * t1, jitter);
  float n = F.y - F.x;
  float4 color = n * bondSelectionShade(input, pos, N, ct);
)foo";
      break;
    case Style::glow:
      body = "\n  float4 color = bondSelectionShade(input, pos, N, ct);\n";
      break;
  }

  return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
         DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
         DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
         DirectXUniformStringLiterals::RGBHSVStringLiteral +
         (style == Style::worleyNoise3D ? DirectXUniformStringLiterals::WorleyNoise3DStringLiteral
                                        : std::string()) +
         std::string("\nstruct PSInput\n{\n") + DirectXBondImposter::SelectionVaryingsStringLiteral +
         std::string("};\n") +
         DirectXBondImposter::DepthOutputStringLiteral +
         (external ? DirectXBondImposter::ClippedIntersectStringLiteral
                   : DirectXBondImposter::IntersectStringLiteral) +
         DirectXBondImposter::ModelCoordsStringLiteral +
         shade +
         std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXBondImposter::RayStringLiteral + std::string(R"foo(
  float3 N;
  float ct;
)foo") + trace + std::string(R"foo(
  if (t < 0.0) discard;

  float3 pos = ro + t * rd;
)foo") + DirectXBondImposter::WriteDepthStringLiteral + body + std::string(R"foo(
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
  output.color = bloomLevel * float4(hsv2rgb(hsv), 1.0);
  return output;
}
)foo");
}
