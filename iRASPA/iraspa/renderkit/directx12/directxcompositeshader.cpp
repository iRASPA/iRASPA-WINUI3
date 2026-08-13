/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxcompositeshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/backplanegeometry.h"

void DirectXCompositeShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXCompositeShader::createFullscreenQuad(ID3D12Device *device)
{
  BackPlaneGeometry quad;
  const auto &vertices = quad.vertices();
  const auto &indices = quad.indices();
  _indexCount = static_cast<UINT>(indices.size());
  const size_t vbSize = vertices.size() * sizeof(RKVertex);
  const size_t ibSize = indices.size() * sizeof(short);
  _vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbSize);
  _indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibSize);
  DirectXDeviceHelpers::writeUploadBuffer(_vertexBuffer.Get(), vertices.data(), vbSize);
  DirectXDeviceHelpers::writeUploadBuffer(_indexBuffer.Get(), indices.data(), ibSize);
  _vbv.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
  _vbv.SizeInBytes = static_cast<UINT>(vbSize);
  _vbv.StrideInBytes = sizeof(RKVertex);
  _ibv.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
  _ibv.SizeInBytes = static_cast<UINT>(ibSize);
  _ibv.Format = DXGI_FORMAT_R16_UINT;
}

void DirectXCompositeShader::initialize(ID3D12Device *device, ID3D12RootSignature *sceneRootSignature,
                                        DXGI_FORMAT rtvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = sceneRootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.InputLayout = { inputLayout, 1 };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.SampleDesc.Count = 1;

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXCompositeShader: failed to create PSO";
    return;
  }

  createFullscreenQuad(device);
  _ready = true;
}

void DirectXCompositeShader::paint(ID3D12GraphicsCommandList *commandList,
                                   D3D12_GPU_DESCRIPTOR_HANDLE blurredSrv)
{
  if (!_ready || !_pso)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->SetGraphicsRootDescriptorTable(3, blurredSrv);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  commandList->IASetVertexBuffers(0, 1, &_vbv);
  commandList->IASetIndexBuffer(&_ibv);
  commandList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
}

const std::string DirectXCompositeShader::_vertexShaderSource = R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = input.position;
  o.texcoord = input.position.xy * 0.5 + 0.5;
  return o;
}
)foo";

const std::string DirectXCompositeShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
std::string(R"foo(
Texture2D blurredTexture : register(t0);
SamplerState blurredSampler : register(s0);
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };
float4 PSMain(PSInput input) : SV_TARGET
{
  float4 bloom = blurredTexture.Sample(blurredSampler, input.texcoord);
  return frameUniforms.bloomPulse * frameUniforms.bloomLevel * bloom;
}
)foo");
