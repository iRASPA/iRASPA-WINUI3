/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxribbonselectionshader.h"
#include <iostream>
#include <cstddef>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"

// Both levels of range are gathered here: selecting a segment lights up the segment, selecting a
// residue or one of its atoms lights up that residue, and the two never overlap because selecting a
// segment clears the residues inside it. A range that is hidden is left out, so the overlay cannot
// outline geometry the main pass never drew.
namespace
{
  void appendSelectedRanges(const RKRenderRibbonSource *ribbon,
                            const std::vector<RKRibbonChainDrawRange> &ranges,
                            const std::set<int> &selectedIndices,
                            bool honourVisibility,
                            bool (RKRenderRibbonSource::*isVisible)(int) const,
                            std::vector<RKRibbonChainDrawRange> &out)
  {
    for (const int index : selectedIndices)
    {
      if (index < 0 || index >= static_cast<int>(ranges.size()))
        continue;
      if (ranges[static_cast<size_t>(index)].indexCount <= 0)
        continue;
      if (honourVisibility && !(ribbon->*isVisible)(index))
        continue;
      out.push_back(ranges[static_cast<size_t>(index)]);
    }
  }
}

void DirectXRibbonSelectionShader::loadShader(ID3D12Device * /*device*/)
{
}

ComPtr<ID3D12PipelineState> DirectXRibbonSelectionShader::buildPSO(ID3D12Device *device,
                                                                   ID3D12RootSignature *rootSignature,
                                                                   const std::string &vertexShaderSource,
                                                                   const std::string &pixelShaderSource,
                                                                   DXGI_FORMAT rtvFormat,
                                                                   DXGI_FORMAT dsvFormat,
                                                                   bool blended)
{
  ComPtr<ID3DBlob> vs = compileShader(vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return nullptr;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, position)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, normal)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, st)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, pad)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKRibbonVertex, stripeST)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  if (blended)
  {
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  }
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  // Same reasoning as the ribbon itself: the swept cross-section is not consistently wound.
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
  // The overlays are drawn into the multisampled scene target and the glow into a glow target that
  // carries the same sample count, so that it can be depth-tested against the scene's own depth.
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  ComPtr<ID3D12PipelineState> pso;
  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
    return nullptr;
  return pso;
}

void DirectXRibbonSelectionShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                              DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                              DXGI_FORMAT glowRtvFormat)
{
  if (!device || !rootSignature)
    return;

  _worleyPso = buildPSO(device, rootSignature, _worleyVertexShaderSource, _worleyPixelShaderSource,
                        rtvFormat, dsvFormat, true);
  _stripedPso = buildPSO(device, rootSignature, _stripedVertexShaderSource, _stripedPixelShaderSource,
                         rtvFormat, dsvFormat, true);
  _glowPso = buildPSO(device, rootSignature, _glowVertexShaderSource, _glowPixelShaderSource,
                      glowRtvFormat, dsvFormat, false);

  if (!_worleyPso || !_stripedPso || !_glowPso)
    std::cerr << "DirectXRibbonSelectionShader: failed to create PSO";
}

void DirectXRibbonSelectionShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _buffers.clear();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXRibbonSelectionShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXRibbonSelectionShader::reloadData(ID3D12Device * /*device*/)
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      SelectionBuffers &bufs = _buffers[i][j];
      bufs = SelectionBuffers{};

      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      if (!ribbon || !ribbon->drawRibbon() || !_renderStructures[i][j]->isVisible())
        continue;

      appendSelectedRanges(ribbon, ribbon->ribbonSegmentDrawRanges(),
                           ribbon->renderSelectedRibbonSegmentDrawRangeIndices(),
                           ribbon->ribbonUsesSegmentVisibility(),
                           &RKRenderRibbonSource::isRibbonSegmentDrawRangeVisible, bufs.ranges);
      appendSelectedRanges(ribbon, ribbon->ribbonResidueDrawRanges(),
                           ribbon->renderSelectedRibbonResidueDrawRangeIndices(),
                           ribbon->ribbonUsesResidueVisibility(),
                           &RKRenderRibbonSource::isRibbonResidueDrawRangeVisible, bufs.ranges);
    }
  }
}

void DirectXRibbonSelectionShader::paintStyle(ID3D12GraphicsCommandList *commandList,
                                              D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                              UINT structureCBVStride,
                                              RKSelectionStyle style, ID3D12PipelineState *pso)
{
  if (!commandList || !pso || !_ribbonShader)
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      const SelectionBuffers &bufs = _buffers[i][j];
      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      // One appearance setting covers atoms, bonds and ribbons, so the style is read from the atom
      // source the structure also is.
      auto *atoms = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());

      D3D12_VERTEX_BUFFER_VIEW vbv{};
      D3D12_INDEX_BUFFER_VIEW ibv{};
      if (ribbon && atoms && !bufs.ranges.empty() && atoms->atomSelectionStyle() == style
          && ribbon->drawRibbon() && _renderStructures[i][j]->isVisible()
          && _ribbonShader->geometryBuffers(i, j, vbv, ibv))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->IASetVertexBuffers(0, 1, &vbv);
        commandList->IASetIndexBuffer(&ibv);
        for (const RKRibbonChainDrawRange &range : bufs.ranges)
        {
          if (range.indexCount <= 0)
            continue;
          commandList->DrawIndexedInstanced(static_cast<UINT>(range.indexCount), 1,
                                            static_cast<UINT>(range.indexStart), 0, 0);
        }
      }
      ++index;
    }
  }
}

void DirectXRibbonSelectionShader::paintOverlay(ID3D12GraphicsCommandList *commandList,
                                                D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                UINT structureCBVStride)
{
  paintStyle(commandList, structureCBVBase, structureCBVStride,
             RKSelectionStyle::WorleyNoise3D, _worleyPso.Get());
  paintStyle(commandList, structureCBVBase, structureCBVStride,
             RKSelectionStyle::striped, _stripedPso.Get());
}

void DirectXRibbonSelectionShader::paintGlow(ID3D12GraphicsCommandList *commandList,
                                             D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                             UINT structureCBVStride)
{
  paintStyle(commandList, structureCBVBase, structureCBVStride,
             RKSelectionStyle::glow, _glowPso.Get());
}

bool DirectXRibbonSelectionShader::hasGlowWork() const
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      auto *atoms = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (ribbon && atoms && !_buffers[i][j].ranges.empty()
          && atoms->atomSelectionStyle() == RKSelectionStyle::glow
          && ribbon->drawRibbon() && _renderStructures[i][j]->isVisible())
        return true;
    }
  }
  return false;
}

// The vertex stage is the same for all three styles: push the surface out along its normal by the
// selection scaling, so the overlay sits just outside the ribbon instead of fighting it for depth.
// Worley and glow use a fifth of the scaling, stripes almost half, as in OpenGL and Metal.
static const std::string kRibbonSelectionExpansion = R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float2 vertexST : TEXCOORD0;
  float2 vertexPad : TEXCOORD1;
  float2 vertexStripeST : TEXCOORD2;
};

float4 ribbonSelectionExpandedPosition(float4 vertexPosition, float4 vertexNormal, float expansionScale)
{
  float3 localNormal = normalize(vertexNormal.xyz);
  float expansion = (structureUniforms.atomSelectionScaling - 1.0) * expansionScale;
  return float4(vertexPosition.xyz + localNormal * expansion, 1.0);
}

float4 ribbonSelectionClipPosition(float4 pos)
{
  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  return clip;
}
)foo";

const std::string DirectXRibbonSelectionShader::_worleyVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
kRibbonSelectionExpansion +
std::string(R"foo(
struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
  float3 specular : COLOR2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = ribbonSelectionExpandedPosition(input.vertexPosition, input.vertexNormal, 0.2);
  float3 localNormal = normalize(input.vertexNormal.xyz);

  output.N = mul(frameUniforms.normalMatrix,
                 mul(structureUniforms.modelMatrix, float4(localNormal, 0.0))).xyz;
  output.ModelN = localNormal;

  // Material colours only: the pixel stage sums the rig, so a light colour folded in here would be
  // applied once per light.
  float3 baseColor = float3(1.0, 1.0, 0.0);
  output.ambient = (structureUniforms.ribbonAmbientColor * float4(baseColor, 1.0)).xyz;
  output.diffuse = (structureUniforms.ribbonDiffuseColor * float4(baseColor, 1.0)).xyz;
  output.specular = structureUniforms.ribbonSpecularColor.xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;

  output.position = ribbonSelectionClipPosition(pos);
  return output;
}
)foo");

const std::string DirectXRibbonSelectionShader::_worleyPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
DirectXUniformStringLiterals::WorleyNoise3DStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
  float3 specular : COLOR2;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  // Unshadowed, as the overlay marks a selection rather than describing the scene's light. The eye
  // position comes back out of the view vector the vertex stage negated.
  LightingWeights lighting = accumulateLighting(N, normalize(input.V), float4(-input.V, 1.0),
                                                structureUniforms.ribbonShininess);

  float4 ambient = float4(lighting.ambient * input.ambient, 1.0);
  float4 diffuse = float4(lighting.diffuse * input.diffuse, 1.0);
  float4 specular = float4(lighting.specular * input.specular, 1.0);

  // The noise is sampled in the ribbon's own frame, so the cells stay put on the surface as the
  // camera moves. The y and z axes are swapped as in OpenGL and Metal, which keeps the cells the
  // same size along the sweep as across it.
  float3 t1 = input.ModelN;
  float frequency = structureUniforms.atomSelectionWorleyNoise3DFrequency;
  float jitter = structureUniforms.atomSelectionWorleyNoise3DJitter;
  float2 F = cellular3D(frequency * float3(t1.x, t1.z, t1.y), jitter);
  float n = F.y - F.x;

  float4 color = n * (ambient + diffuse + specular);
  if (structureUniforms.ribbonHDR != 0)
  {
    float4 ldr = 1.0 - exp2(-color * structureUniforms.ribbonHDRExposure);
    ldr.a = 1.0;
    color = ldr;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.ribbonHue;
  hsv.y = hsv.y * structureUniforms.ribbonSaturation;
  hsv.z = hsv.z * structureUniforms.ribbonValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.atomSelectionIntensity;
  return float4(hsv2rgb(hsv) * bloomLevel, bloomLevel);
}
)foo");

const std::string DirectXRibbonSelectionShader::_stripedVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
kRibbonSelectionExpansion +
std::string(R"foo(
struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
  float2 stripeST : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = ribbonSelectionExpandedPosition(input.vertexPosition, input.vertexNormal, 0.45);
  float3 localNormal = normalize(input.vertexNormal.xyz);

  output.N = mul(frameUniforms.normalMatrix,
                 mul(structureUniforms.modelMatrix, float4(localNormal, 0.0))).xyz;
  output.stripeST = input.vertexStripeST;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;

  output.position = ribbonSelectionClipPosition(pos);
  return output;
}
)foo");

const std::string DirectXRibbonSelectionShader::_stripedPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
  float2 stripeST : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  // Unshadowed, as the overlay marks a selection rather than describing the scene's light.
  LightingWeights lighting = accumulateLighting(N, normalize(input.V), float4(-input.V, 1.0),
                                                structureUniforms.ribbonShininess);
  float4 color = float4(lighting.diffuse, 1.0) * float4(1.0, 1.0, 0.0, 1.0);

  // 'stripeST' runs along the residue and around the cross-section, both baked per residue, so the
  // band ends where the residue does and the stripes cross it at a constant angle whatever the
  // residue's length.
  float2 stripeST = input.stripeST;
  float uDensity = structureUniforms.atomSelectionStripesDensity;
  float frequency = structureUniforms.atomSelectionStripesFrequency;

  float bandAlong = smoothstep(0.0, 0.06, stripeST.x) * smoothstep(0.0, 0.06, 1.0 - stripeST.x);
  float bandAround = smoothstep(0.0, 0.10, stripeST.y) * smoothstep(0.0, 0.10, 1.0 - stripeST.y);
  float bandMask = bandAlong * bandAround;
  if (bandMask < 0.01) { discard; }

  float stripeU = frac(stripeST.x * frequency);
  float stripeV = frac(stripeST.y * frequency);
  bool inStripe = (stripeU < uDensity) != (stripeV < uDensity);
  if (!inStripe) { discard; }

  color *= bandMask;

  if (structureUniforms.ribbonHDR != 0)
  {
    float4 ldr = 1.0 - exp2(-color * structureUniforms.ribbonHDRExposure);
    ldr.a = 1.0;
    color = ldr;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.ribbonHue;
  hsv.y = hsv.y * structureUniforms.ribbonSaturation;
  hsv.z = hsv.z * structureUniforms.ribbonValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.atomSelectionIntensity;
  return float4(hsv2rgb(hsv) * bloomLevel, bloomLevel);
}
)foo");

const std::string DirectXRibbonSelectionShader::_glowVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
kRibbonSelectionExpansion +
std::string(R"foo(
struct VSOutput
{
  float4 position : SV_POSITION;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = ribbonSelectionExpandedPosition(input.vertexPosition, input.vertexNormal, 0.2);

  // Unlit by design, as for the atom glow: the blur pass turns this silhouette into a halo, so it
  // carries the material colours and never consults the light rig.
  float3 baseColor = float3(1.0, 1.0, 0.0);
  output.ambient = (structureUniforms.ribbonAmbientColor * float4(baseColor, 1.0)).xyz;
  output.diffuse = (structureUniforms.ribbonDiffuseColor * float4(baseColor, 1.0)).xyz;

  output.position = ribbonSelectionClipPosition(pos);
  return output;
}
)foo");

const std::string DirectXRibbonSelectionShader::_glowPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  // Flat, unshaded and bright: the blur pass turns this into the halo, so all this has to be is the
  // silhouette of the selected geometry.
  return float4(structureUniforms.atomSelectionIntensity * (input.ambient + input.diffuse), 1.0);
}
)foo");
