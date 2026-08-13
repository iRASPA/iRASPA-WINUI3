/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxblurshader.h"
#include <algorithm>
#include <iostream>
#include "directxdevicehelpers.h"
#include "geometry/backplanegeometry.h"

DirectXBlurShader::~DirectXBlurShader() = default;

void DirectXBlurShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBlurShader::createFullscreenRootSignature(ID3D12Device *device)
{
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER param = {};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &srvRange;
  param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.ShaderRegister = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &param;
  rootDesc.NumStaticSamplers = 1;
  rootDesc.pStaticSamplers = &sampler;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
  {
    if (error)
      OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
    return;
  }
  device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                              IID_PPV_ARGS(&_rootSignature));
}

void DirectXBlurShader::createFullscreenQuad(ID3D12Device *device)
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

void DirectXBlurShader::createPipelines(ID3D12Device *device, DXGI_FORMAT rtvFormat)
{
  auto makePso = [&](ComPtr<ID3D12PipelineState> &out, const std::string &vsSrc, const std::string &psSrc,
                     const char *name) {
    ComPtr<ID3DBlob> vs = compileShader(vsSrc, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(psSrc, "PSMain", "ps_5_0");
    if (!vs || !ps)
      return;

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
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

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out))))
      std::cerr << "DirectXBlurShader: failed to create" << name;
  };

  makePso(_copyPso, _fullscreenVertexShaderSource, _copyPixelShaderSource, "copy PSO");
  makePso(_horizontalPso, _blurHorizontalVertexShaderSource, _blurPixelShaderSource, "horizontal PSO");
  makePso(_verticalPso, _blurVerticalVertexShaderSource, _blurPixelShaderSource, "vertical PSO");
}

void DirectXBlurShader::createTargets(ID3D12Device *device, int width, int height)
{
  _width = std::max(1, width);
  _height = std::max(1, height);

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 3;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap));

  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
  srvHeapDesc.NumDescriptors = 4;
  srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_srvHeap));

  const UINT rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  const UINT srvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpuStart = _srvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart = _srvHeap->GetGPUDescriptorHandleForHeapStart();

  auto createTex = [&](ComPtr<ID3D12Resource> &tex, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                       D3D12_CPU_DESCRIPTOR_HANDLE srvCpu, int slot) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(_width);
    desc.Height = static_cast<UINT>(_height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = _rtvFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = _rtvFormat;
    clear.Color[0] = clear.Color[1] = clear.Color[2] = clear.Color[3] = 0.0f;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&tex));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = _rtvFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(tex.Get(), &rtvDesc, rtv);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = _rtvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex.Get(), &srvDesc, srvCpu);
    (void)(slot);
  };

  _down.rtv = rtvStart;
  _down.srvCpu = srvCpuStart;
  _down.srvGpu = srvGpuStart;
  createTex(_down.texture, _down.rtv, _down.srvCpu, 0);
  _down.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

  _horizontal.rtv = { rtvStart.ptr + rtvInc };
  _horizontal.srvCpu = { srvCpuStart.ptr + srvInc };
  _horizontal.srvGpu = { srvGpuStart.ptr + srvInc };
  createTex(_horizontal.texture, _horizontal.rtv, _horizontal.srvCpu, 1);
  _horizontal.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

  _verticalRtv = { rtvStart.ptr + 2 * rtvInc };
  _verticalSrvCpu = { srvCpuStart.ptr + 2 * srvInc };
  _verticalSrvGpu = { srvGpuStart.ptr + 2 * srvInc };
  createTex(_vertical, _verticalRtv, _verticalSrvCpu, 2);
  _verticalState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  _glowSrvCpu = { srvCpuStart.ptr + 3 * srvInc };
  _glowSrvGpu = { srvGpuStart.ptr + 3 * srvInc };
}

void DirectXBlurShader::initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat)
{
  _rtvFormat = rtvFormat;
  createFullscreenRootSignature(device);
  if (!_rootSignature)
    return;
  createFullscreenQuad(device);
  createPipelines(device, rtvFormat);
  createTargets(device, 1, 1);
  _ready = _copyPso && _horizontalPso && _verticalPso && _down.texture && _horizontal.texture && _vertical;
}

void DirectXBlurShader::resize(ID3D12Device *device, int width, int height)
{
  if (!_ready || !device)
    return;
  if (width == _width && height == _height)
    return;
  createTargets(device, width, height);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXBlurShader::blurredSrv() const
{
  return _verticalSrvGpu;
}

void DirectXBlurShader::drawFullscreen(ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pso,
                                       D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_GPU_DESCRIPTOR_HANDLE srv)
{
  commandList->SetPipelineState(pso);
  commandList->SetGraphicsRootSignature(_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _srvHeap.Get() };
  commandList->SetDescriptorHeaps(1, heaps);
  commandList->SetGraphicsRootDescriptorTable(0, srv);
  commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(_width);
  viewport.Height = static_cast<float>(_height);
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);
  D3D12_RECT scissor = { 0, 0, _width, _height };
  commandList->RSSetScissorRects(1, &scissor);

  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  commandList->IASetVertexBuffers(0, 1, &_vbv);
  commandList->IASetIndexBuffer(&_ibv);
  commandList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
}

void DirectXBlurShader::paint(ID3D12GraphicsCommandList *commandList,
                              ID3D12Resource *glowTexture,
                              D3D12_RESOURCE_STATES &glowState)
{
  if (!_ready || !glowTexture)
    return;

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = _rtvFormat;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels = 1;

  // Recreate glow SRV each frame (glow may be recreated on resize).
  ID3D12Device *device = nullptr;
  glowTexture->GetDevice(IID_PPV_ARGS(&device));
  if (device)
  {
    device->CreateShaderResourceView(glowTexture, &srvDesc, _glowSrvCpu);
    device->Release();
  }

  auto transition = [&](ID3D12Resource *res, D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES after) {
    if (state == after)
      return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    state = after;
  };

  // Copy/downsample glow -> down
  transition(glowTexture, glowState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  transition(_down.texture.Get(), _down.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  drawFullscreen(commandList, _copyPso.Get(), _down.rtv, _glowSrvGpu);

  // Horizontal blur: down -> horizontal
  transition(_down.texture.Get(), _down.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  transition(_horizontal.texture.Get(), _horizontal.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
  drawFullscreen(commandList, _horizontalPso.Get(), _horizontal.rtv, _down.srvGpu);

  // Vertical blur: horizontal -> vertical
  transition(_horizontal.texture.Get(), _horizontal.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  transition(_vertical.Get(), _verticalState, D3D12_RESOURCE_STATE_RENDER_TARGET);
  drawFullscreen(commandList, _verticalPso.Get(), _verticalRtv, _horizontal.srvGpu);

  // Blurred result ready as SRV
  transition(_vertical.Get(), _verticalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

const std::string DirectXBlurShader::_fullscreenVertexShaderSource = R"foo(
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

const std::string DirectXBlurShader::_copyPixelShaderSource = R"foo(
Texture2D inputTexture : register(t0);
SamplerState inputSampler : register(s0);
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };
float4 PSMain(PSInput input) : SV_TARGET
{
  return inputTexture.Sample(inputSampler, input.texcoord);
}
)foo";

const std::string DirectXBlurShader::_blurHorizontalVertexShaderSource = R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput
{
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
  float2 blurTexCoords[14] : TEXCOORD1;
};
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = input.position;
  o.texCoord = input.position.xy * 0.5 + 0.5;
  o.blurTexCoords[ 0] = o.texCoord + float2(-0.028, 0.0);
  o.blurTexCoords[ 1] = o.texCoord + float2(-0.024, 0.0);
  o.blurTexCoords[ 2] = o.texCoord + float2(-0.020, 0.0);
  o.blurTexCoords[ 3] = o.texCoord + float2(-0.016, 0.0);
  o.blurTexCoords[ 4] = o.texCoord + float2(-0.012, 0.0);
  o.blurTexCoords[ 5] = o.texCoord + float2(-0.008, 0.0);
  o.blurTexCoords[ 6] = o.texCoord + float2(-0.004, 0.0);
  o.blurTexCoords[ 7] = o.texCoord + float2( 0.004, 0.0);
  o.blurTexCoords[ 8] = o.texCoord + float2( 0.008, 0.0);
  o.blurTexCoords[ 9] = o.texCoord + float2( 0.012, 0.0);
  o.blurTexCoords[10] = o.texCoord + float2( 0.016, 0.0);
  o.blurTexCoords[11] = o.texCoord + float2( 0.020, 0.0);
  o.blurTexCoords[12] = o.texCoord + float2( 0.024, 0.0);
  o.blurTexCoords[13] = o.texCoord + float2( 0.028, 0.0);
  return o;
}
)foo";

const std::string DirectXBlurShader::_blurVerticalVertexShaderSource = R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput
{
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
  float2 blurTexCoords[14] : TEXCOORD1;
};
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = input.position;
  o.texCoord = input.position.xy * 0.5 + 0.5;
  o.blurTexCoords[ 0] = o.texCoord + float2(0.0, -0.028);
  o.blurTexCoords[ 1] = o.texCoord + float2(0.0, -0.024);
  o.blurTexCoords[ 2] = o.texCoord + float2(0.0, -0.020);
  o.blurTexCoords[ 3] = o.texCoord + float2(0.0, -0.016);
  o.blurTexCoords[ 4] = o.texCoord + float2(0.0, -0.012);
  o.blurTexCoords[ 5] = o.texCoord + float2(0.0, -0.008);
  o.blurTexCoords[ 6] = o.texCoord + float2(0.0, -0.004);
  o.blurTexCoords[ 7] = o.texCoord + float2(0.0,  0.004);
  o.blurTexCoords[ 8] = o.texCoord + float2(0.0,  0.008);
  o.blurTexCoords[ 9] = o.texCoord + float2(0.0,  0.012);
  o.blurTexCoords[10] = o.texCoord + float2(0.0,  0.016);
  o.blurTexCoords[11] = o.texCoord + float2(0.0,  0.020);
  o.blurTexCoords[12] = o.texCoord + float2(0.0,  0.024);
  o.blurTexCoords[13] = o.texCoord + float2(0.0,  0.028);
  return o;
}
)foo";

const std::string DirectXBlurShader::_blurPixelShaderSource = R"foo(
Texture2D s_texture : register(t0);
SamplerState s_sampler : register(s0);
struct PSInput
{
  float4 position : SV_POSITION;
  float2 texCoord : TEXCOORD0;
  float2 blurTexCoords[14] : TEXCOORD1;
};
float4 PSMain(PSInput input) : SV_TARGET
{
  float4 color = float4(0.0, 0.0, 0.0, 0.0);
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 0]) * 0.0044299121055113265;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 1]) * 0.00895781211794;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 2]) * 0.0215963866053;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 3]) * 0.0443683338718;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 4]) * 0.0776744219933;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 5]) * 0.115876621105;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 6]) * 0.147308056121;
  color += s_texture.Sample(s_sampler, input.texCoord) * 0.159576912161;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 7]) * 0.147308056121;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 8]) * 0.115876621105;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[ 9]) * 0.0776744219933;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[10]) * 0.0443683338718;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[11]) * 0.0215963866053;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[12]) * 0.00895781211794;
  color += s_texture.Sample(s_sampler, input.blurTexCoords[13]) * 0.0044299121055113265;
  return color;
}
)foo";
