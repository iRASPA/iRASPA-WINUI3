/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxribbonshader.h"
#include <iostream>
#include <cstddef>
#include "directxdevicehelpers.h"
#include "directxribbonambientocclusionshader.h"
#include "directxuniformstringliterals.h"

void DirectXRibbonShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXRibbonShader::initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  // 'st' is the baked ambient-occlusion lightmap coordinate and 'pad.x' the secondary-structure
  // code that picks the coil, helix or sheet color. 'stripeST' only matters to the selection pass.
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
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  // The swept cross-section is not consistently wound where the spline twists, and both faces of a
  // sheet are meant to be visible, so OpenGL and Metal both draw the ribbon with culling off.
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_opaquePso))))
  {
    std::cerr << "DirectXRibbonShader: failed to create opaque PSO";
    return;
  }
  _opaquePsoReady = true;
}

void DirectXRibbonShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                    DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  if (!device || !rootSignature)
    return;
  initializePSOs(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXRibbonShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXRibbonShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXRibbonShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXRibbonShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      RibbonBuffers &bufs = _buffers[i][j];
      bufs = RibbonBuffers{};

      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      if (!ribbon || !ribbon->drawRibbon() || !_renderStructures[i][j]->isVisible())
        continue;

      const std::vector<RKRibbonVertex> &vertices = ribbon->renderRibbonVertices();
      const std::vector<uint32_t> &indices = ribbon->renderRibbonIndices();
      if (vertices.empty() || indices.empty())
        continue;

      const size_t vbBytes = vertices.size() * sizeof(RKRibbonVertex);
      const size_t ibBytes = indices.size() * sizeof(uint32_t);
      bufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbBytes);
      bufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibBytes);
      if (!bufs.vertexBuffer || !bufs.indexBuffer)
        continue;

      DirectXDeviceHelpers::writeUploadBuffer(bufs.vertexBuffer.Get(), vertices.data(), vbBytes);
      DirectXDeviceHelpers::writeUploadBuffer(bufs.indexBuffer.Get(), indices.data(), ibBytes);
      bufs.vbv = { bufs.vertexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(vbBytes),
                   static_cast<UINT>(sizeof(RKRibbonVertex)) };
      bufs.ibv = { bufs.indexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(ibBytes),
                   DXGI_FORMAT_R32_UINT };
      if (ribbon->ribbonUsesResidueVisibility() && !ribbon->ribbonResidueDrawRanges().empty())
      {
        bufs.drawRanges = ribbon->ribbonResidueDrawRanges();
      }
      else if (ribbon->ribbonUsesSegmentVisibility() && !ribbon->ribbonSegmentDrawRanges().empty())
      {
        bufs.drawRanges = ribbon->ribbonSegmentDrawRanges();
      }
      else
      {
        bufs.drawRanges = ribbon->ribbonChainDrawRanges();
      }
    }
  }
}

void DirectXRibbonShader::paintOpaque(ID3D12GraphicsCommandList *commandList,
                                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                     UINT structureCBVStride,
                                     const DirectXRibbonAmbientOcclusionShader *aoShader)
{
  if (!commandList || !_opaquePsoReady || !_opaquePso)
    return;

  commandList->SetPipelineState(_opaquePso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(_renderStructures[i][j].get());
      const RibbonBuffers &bufs = _buffers[i][j];

      if (ribbon && ribbon->drawRibbon() && _renderStructures[i][j]->isVisible()
          && bufs.vertexBuffer && bufs.indexBuffer && !bufs.drawRanges.empty())
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        commandList->OMSetStencilRef(RKEdgeCueingParameters::stencilValue(ribbon->ribbonEdgeCueing()));

        if (aoShader)
          commandList->SetGraphicsRootDescriptorTable(3, aoShader->aoSrv(i, j));

        commandList->IASetVertexBuffers(0, 1, &bufs.vbv);
        commandList->IASetIndexBuffer(&bufs.ibv);

        // One draw per merged range. Residue/segment ranges overlap by a ring pair at boundaries, so
        // contiguous visible spans collapse to fewer DrawIndexed calls.
        const std::vector<RKRibbonChainDrawRange> &ranges = visibleDrawRanges(bufs, ribbon);
        for (const RKRibbonChainDrawRange &range : ranges)
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

  // Back to what is not a structure, for the passes that follow: the reference outlives this one.
  commandList->OMSetStencilRef(0);
}

const std::vector<RKRibbonChainDrawRange> &DirectXRibbonShader::visibleDrawRanges(
    const RibbonBuffers &buffers, const RKRenderRibbonSource *ribbon)
{
  static const std::vector<RKRibbonChainDrawRange> nothingToDraw;
  if (!ribbon || buffers.drawRanges.empty())
    return nothingToDraw;

  // The structure picks the same level this shader chose its buffers by, so asking it costs one
  // comparison in the frames where nothing has been hidden since the last.
  return ribbon->ribbonDrawRangesForEncoding();
}

bool DirectXRibbonShader::geometryBuffers(size_t sceneIndex, size_t structureIndex,
                                          D3D12_VERTEX_BUFFER_VIEW &vbv,
                                          D3D12_INDEX_BUFFER_VIEW &ibv) const
{
  if (sceneIndex >= _buffers.size() || structureIndex >= _buffers[sceneIndex].size())
    return false;

  const RibbonBuffers &bufs = _buffers[sceneIndex][structureIndex];
  if (!bufs.vertexBuffer || !bufs.indexBuffer)
    return false;

  vbv = bufs.vbv;
  ibv = bufs.ibv;
  return true;
}

bool DirectXRibbonShader::pickGeometry(size_t sceneIndex, size_t structureIndex,
                                       D3D12_VERTEX_BUFFER_VIEW &vbv,
                                       D3D12_INDEX_BUFFER_VIEW &ibv,
                                       std::vector<RKRibbonChainDrawRange> &visibleRanges) const
{
  visibleRanges.clear();
  if (sceneIndex >= _buffers.size() || structureIndex >= _buffers[sceneIndex].size())
    return false;

  const std::shared_ptr<RKRenderObject> &object = _renderStructures[sceneIndex][structureIndex];
  auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(object.get());
  if (!ribbon || !ribbon->drawRibbon() || !object->isVisible())
    return false;

  const RibbonBuffers &bufs = _buffers[sceneIndex][structureIndex];
  if (!bufs.vertexBuffer || !bufs.indexBuffer || bufs.drawRanges.empty())
    return false;

  visibleRanges = visibleDrawRanges(bufs, ribbon);
  if (visibleRanges.empty())
    return false;

  vbv = bufs.vbv;
  ibv = bufs.ibv;
  return true;
}

const std::string DirectXRibbonShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float2 vertexST : TEXCOORD0;
  float2 vertexPad : TEXCOORD1;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD1;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
  float3 specular : COLOR2;
  float2 aoUV : TEXCOORD2;
};

float3 ribbonColorForStructureType(float structureType)
{
  if (structureType < 0.5) { return structureUniforms.ribbonCoilColor.xyz; }
  if (structureType < 1.5) { return structureUniforms.ribbonHelixColor.xyz; }
  if (structureType < 2.5) { return structureUniforms.ribbonSheetColor.xyz; }
  // Nucleic-acid cartoons carry their own codes and are colored the way PyMOL colors bases.
  if (structureType < 3.5) { return float3(1.0, 1.0, 0.0); }
  if (structureType < 4.5) { return float3(1.0, 1.0, 0.0); }
  if (structureType < 5.5) { return float3(1.0, 0.0, 0.0); }
  if (structureType < 6.5) { return float3(0.4, 0.4, 0.4); }
  if (structureType < 7.5) { return float3(0.6, 1.0, 0.94); }
  return structureUniforms.ribbonCoilColor.xyz;
}

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = input.vertexPosition;

  output.N = mul(frameUniforms.normalMatrix,
                 mul(structureUniforms.modelMatrix, float4(input.vertexNormal.xyz, 0.0))).xyz;

  // The material alone: the light rig is summed over in the pixel shader, since a sum over lights
  // that a shadow mask gates per pixel cannot be folded into one interpolated colour here.
  float3 baseColor = ribbonColorForStructureType(input.vertexPad.x);
  output.ambient = (structureUniforms.ribbonAmbientColor * float4(baseColor, 1.0)).xyz;
  output.diffuse = (structureUniforms.ribbonDiffuseColor * float4(baseColor, 1.0)).xyz;
  output.specular = structureUniforms.ribbonSpecularColor.xyz;
  output.aoUV = input.vertexST;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXRibbonShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
Texture2D<float> ambientOcclusionTexture : register(t0);
SamplerState ambientOcclusionSampler : register(s0);

struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD1;
  float3 ambient : COLOR0;
  float3 diffuse : COLOR1;
  float3 specular : COLOR2;
  float2 aoUV : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 V = normalize(input.V);

  // V is the direction from the surface back to the eye, so negating it recovers the eye-space
  // position the light rig needs.
  LightingWeights lighting = accumulateLighting(N, V, float4(-input.V, 1.0),
                                                structureUniforms.ribbonShininess,
                                                shadowMaskAtFragment(input.position));

  float3 ambient = lighting.ambient * input.ambient;
  float3 diffuse = lighting.diffuse * input.diffuse;
  float3 specular = lighting.specular * input.specular;

  // The lightmap coordinate the mesh was built with indexes the baked atlas directly, and the atlas
  // is blurred before it is uploaded, so one bilinear tap is enough. Occlusion dims the specular
  // highlight along with everything else, the way the ribbon is shaded in Qt and Cocoa.
  float ao = 1.0;
  if (structureUniforms.ribbonAmbientOcclusion != 0)
  {
    ao = ambientOcclusionTexture.Sample(ambientOcclusionSampler, input.aoUV);
  }

  // Occlusion says how much of the environment a point can see, so physically it belongs on the
  // ambient term alone. The strength blends it back into the direct terms to recover the contrast of
  // the older "Fancy" look, which a camera light cannot otherwise produce since it casts no shadows.
  float aoDirect = lerp(1.0, ao, clamp(structureUniforms.ambientOcclusionStrength, 0.0, 1.0));
  float4 color = float4(ao * ambient + aoDirect * (diffuse + specular), 1.0);

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
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");
