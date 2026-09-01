/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#include "directxtracedpresentshader.h"

#include <iostream>

#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/backplanegeometry.h"

void DirectXTracedPresentShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXTracedPresentShader::createFullscreenQuad(ID3D12Device *device)
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

void DirectXTracedPresentShader::initialize(ID3D12Device *device,
                                            ID3D12RootSignature *sceneRootSignature,
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
  // The traced image is the whole frame, the rasterized part of the scene having already been mixed
  // into it by the resolve kernel, so it replaces what is there rather than blending with it.
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
    std::cerr << "DirectXTracedPresentShader: failed to create PSO";
    return;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 1;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap))))
  {
    std::cerr << "DirectXTracedPresentShader: failed to create SRV heap";
    return;
  }

  createFullscreenQuad(device);
  _ready = true;
}

void DirectXTracedPresentShader::release()
{
  _pso.Reset();
  _srvHeap.Reset();
  _vertexBuffer.Reset();
  _indexBuffer.Reset();
  _indexCount = 0;
  _ready = false;
}

void DirectXTracedPresentShader::paint(ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
                                       ID3D12Resource *traced)
{
  if (!_ready || !_pso || !traced || !device)
    return;

  // The tracer always writes RGBA, whichever way round the swap chain stores its channels.
  D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
  view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(traced, &view,
                                   _srvHeap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {_srvHeap.Get()};
  commandList->SetDescriptorHeaps(1, heaps);

  commandList->SetPipelineState(_pso.Get());
  commandList->SetGraphicsRootDescriptorTable(3, _srvHeap->GetGPUDescriptorHandleForHeapStart());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  commandList->IASetVertexBuffers(0, 1, &_vbv);
  commandList->IASetIndexBuffer(&_ibv);
  commandList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
}

const std::string DirectXTracedPresentShader::_vertexShaderSource = R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = input.position;
  return o;
}
)foo";

// Read by pixel rather than sampled: the tracer writes its image from a compute kernel indexed by
// pixel, whose first row is the top one, which is the same way round SV_POSITION counts. Sampling
// would mean deciding what a texture coordinate means here, and getting it upside down.
const std::string DirectXTracedPresentShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
std::string(R"foo(
Texture2D tracedTexture : register(t0);
struct PSInput { float4 position : SV_POSITION; };
float4 PSMain(PSInput input) : SV_TARGET
{
  return tracedTexture.Load(int3(int2(input.position.xy), 0));
}
)foo");
