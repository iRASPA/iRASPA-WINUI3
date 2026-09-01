/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxlocalaxesshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/axessystemdefaultgeometry.h"
#include "skboundingbox.h"
#include <cstddef>

void DirectXLocalAxesShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXLocalAxesShader::initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                           DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKPrimitiveVertex, position)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKPrimitiveVertex, normal)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKPrimitiveVertex, color)),
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
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

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXLocalAxesShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXLocalAxesShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                        DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  if (!device || !rootSignature)
    return;
  initializePSO(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXLocalAxesShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXLocalAxesShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXLocalAxesShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXLocalAxesShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      MeshBuffers &bufs = _buffers[i][j];
      bufs = MeshBuffers{};

      auto *object = _renderStructures[i][j].get();
      auto *axes = dynamic_cast<RKRenderLocalAxesSource *>(object);
      if (!object || !axes || axes->renderLocalAxes().position() == RKLocalAxes::Position::none)
        continue;

      double length = axes->renderLocalAxes().length();
      double width = axes->renderLocalAxes().width();
      std::shared_ptr<SKCell> unitCell = object->cell();
      if (unitCell)
      {
        SKBoundingBox boundingBox = unitCell->boundingBox();
        if (axes->renderLocalAxes().scalingType() == RKLocalAxes::ScalingType::relative)
          length = boundingBox.shortestEdge() * axes->renderLocalAxes().length() / 100.0;
      }

      AxesSystemDefaultGeometry axesGeometry{};
      switch (axes->renderLocalAxes().style())
      {
      case RKLocalAxes::Style::defaultStyle:
        axesGeometry = AxesSystemDefaultGeometry(
            RKGlobalAxes::CenterType::cube, width, float4(1.0, 1.0, 1.0, 1.0),
            length, width, float4(1.0, 0.4, 0.7, 1.0), float4(0.7, 1.0, 0.4, 1.0), float4(0.4, 0.7, 1.0, 1.0),
            0.0, 1.0, float4(1.0, 0.4, 0.7, 1.0), float4(0.7, 1.0, 0.4, 1.0), float4(0.4, 0.7, 1.0, 1.0),
            false, 1.0, 4);
        break;
      case RKLocalAxes::Style::defaultStyleRGB:
        axesGeometry = AxesSystemDefaultGeometry(
            RKGlobalAxes::CenterType::cube, width, float4(1.0, 1.0, 1.0, 1.0),
            length, width, float4(1.0, 0.0, 0.0, 1.0), float4(0.0, 1.0, 0.0, 1.0), float4(0.0, 0.0, 1.0, 1.0),
            0.0, 1.0, float4(1.0, 0.0, 0.0, 1.0), float4(0.0, 1.0, 0.0, 1.0), float4(0.0, 0.0, 1.0, 1.0),
            false, 1.0, 4);
        break;
      case RKLocalAxes::Style::cylinder:
        axesGeometry = AxesSystemDefaultGeometry(
            RKGlobalAxes::CenterType::cube, width, float4(1.0, 1.0, 1.0, 1.0),
            length, width, float4(1.0, 0.4, 0.7, 1.0), float4(0.7, 1.0, 0.4, 1.0), float4(0.4, 0.7, 1.0, 1.0),
            0.0, 1.0, float4(1.0, 0.4, 0.7, 1.0), float4(0.7, 1.0, 0.4, 1.0), float4(0.4, 0.7, 1.0, 1.0),
            false, 1.0, 41);
        break;
      case RKLocalAxes::Style::cylinderRGB:
        axesGeometry = AxesSystemDefaultGeometry(
            RKGlobalAxes::CenterType::cube, width, float4(1.0, 1.0, 1.0, 1.0),
            length, width, float4(1.0, 0.0, 0.0, 1.0), float4(0.0, 1.0, 0.0, 1.0), float4(0.0, 0.0, 1.0, 1.0),
            0.0, 1.0, float4(1.0, 0.0, 0.0, 1.0), float4(0.0, 1.0, 0.0, 1.0), float4(0.0, 0.0, 1.0, 1.0),
            false, 1.0, 41);
        break;
      }

      const auto &vertices = axesGeometry.vertices();
      const auto &indices = axesGeometry.indices();
      if (vertices.empty() || indices.empty())
        continue;

      const size_t vbBytes = vertices.size() * sizeof(RKPrimitiveVertex);
      const size_t ibBytes = indices.size() * sizeof(short);
      bufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbBytes);
      bufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibBytes);
      if (!bufs.vertexBuffer || !bufs.indexBuffer)
        continue;

      DirectXDeviceHelpers::writeUploadBuffer(bufs.vertexBuffer.Get(), vertices.data(), vbBytes);
      DirectXDeviceHelpers::writeUploadBuffer(bufs.indexBuffer.Get(), indices.data(), ibBytes);
      bufs.vbv = { bufs.vertexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(vbBytes),
                   static_cast<UINT>(sizeof(RKPrimitiveVertex)) };
      bufs.ibv = { bufs.indexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(ibBytes),
                   DXGI_FORMAT_R16_UINT };
      bufs.indexCount = static_cast<UINT>(indices.size());
    }
  }
}

void DirectXLocalAxesShader::paint(ID3D12GraphicsCommandList *commandList,
                                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                   UINT structureCBVStride)
{
  if (!_psoReady || !_pso || !commandList)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *axes = dynamic_cast<RKRenderLocalAxesSource *>(_renderStructures[i][j].get());
      const MeshBuffers &bufs = _buffers[i][j];
      if (axes && _renderStructures[i][j]->isVisible()
          && axes->renderLocalAxes().position() != RKLocalAxes::Position::none
          && bufs.indexCount > 0 && bufs.vertexBuffer && bufs.indexBuffer)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->IASetVertexBuffers(0, 1, &bufs.vbv);
        commandList->IASetIndexBuffer(&bufs.ibv);
        commandList->DrawIndexedInstanced(bufs.indexCount, 1, 0, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXLocalAxesShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 vertexColor : COLOR;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
  float4 diffuse : COLOR0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = input.vertexPosition + structureUniforms.localAxisPosition;
  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;
  output.diffuse = input.vertexColor;
  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;
  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXLocalAxesShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD0;
  float4 diffuse : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  // Unshadowed: the axes are a guide, drawn to be read rather than lit realistically.
  LightingWeights lighting = accumulateLighting(N, normalize(input.V), float4(-input.V, 1.0), 0.0);
  float3 shade = (guideGeometryAmbient * lighting.ambient + lighting.diffuse) * input.diffuse.xyz;
  float3 ldr = 1.0 - exp2(-shade * 1.5);
  return float4(ldr, 1.0);
}
)foo");
