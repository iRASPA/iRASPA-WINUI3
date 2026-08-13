/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxbackgroundshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/backplanegeometry.h"
#include <cstdio>
#include <cstring>

void DirectXBackgroundShader::setPendingRgba(UINT width, UINT height, const uint8_t *rgba, int srcPitchBytes)
{
  _pendingWidth = width;
  _pendingHeight = height;
  _pendingRgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255);
  if (!rgba || width == 0 || height == 0)
    return;

  for (UINT y = 0; y < height; ++y)
  {
    std::memcpy(_pendingRgba.data() + static_cast<size_t>(y) * width * 4u,
                rgba + static_cast<size_t>(y) * static_cast<size_t>(srcPitchBytes),
                static_cast<size_t>(width) * 4u);
  }
}

void DirectXBackgroundShader::setPendingWhite(UINT width, UINT height)
{
  _pendingWidth = width;
  _pendingHeight = height;
  _pendingRgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255);
}

void DirectXBackgroundShader::loadShader(ID3D12Device * /*device*/)
{
  // PSO created in initialize() once root signature and RTV format are known.
}

void DirectXBackgroundShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                         DXGI_FORMAT rtvFormat, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle)
{
  _srvCpuHandle = srvCpuHandle;

  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.InputLayout = { inputLayout, 1 };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::fprintf(stderr, "DirectXBackgroundShader: failed to create PSO\n");
    return;
  }

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

  setPendingWhite(64, 64);
  _textureDirty = true;
  _initialized = true;
}

void DirectXBackgroundShader::reload(std::shared_ptr<RKRenderDataSource> source)
{
  if (source)
  {
    // Protocol still returns QImage; copy into a raw RGBA buffer for the DX12 upload path.
    const RKImage image = source->renderBackgroundCachedImage().convertToFormat(RKImage::Format_RGBA8888);
    if (!image.isNull())
      setPendingRgba(static_cast<UINT>(image.width()), static_cast<UINT>(image.height()),
                     image.constBits(), image.bytesPerLine());
    else
      setPendingWhite(64, 64);
  }
  else
  {
    setPendingWhite(64, 64);
  }
  _textureDirty = true;
}

bool DirectXBackgroundShader::ensureTextureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *commandList)
{
  if (!_initialized || !_textureDirty || _pendingRgba.empty() || _pendingWidth == 0 || _pendingHeight == 0)
    return false;

  const UINT width = _pendingWidth;
  const UINT height = _pendingHeight;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  _texture.Reset();
  if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&_texture))))
  {
    std::fprintf(stderr, "DirectXBackgroundShader: failed to create texture\n");
    return false;
  }

  UINT64 uploadSize = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, nullptr, nullptr, &uploadSize);

  _textureUpload = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
  if (!_textureUpload)
    return false;

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(_textureUpload->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return false;

  const uint8_t *src = _pendingRgba.data();
  const size_t srcPitch = static_cast<size_t>(width) * 4u;
  for (UINT y = 0; y < height; ++y)
  {
    std::memcpy(mapped + layout.Offset + y * layout.Footprint.RowPitch,
                src + y * srcPitch,
                srcPitch);
  }
  _textureUpload->Unmap(0, nullptr);

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = _texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = _textureUpload.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = layout;

  commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = _texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &barrier);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(_texture.Get(), &srvDesc, _srvCpuHandle);

  _textureDirty = false;
  return true;
}

void DirectXBackgroundShader::paint(ID3D12GraphicsCommandList *commandList,
                                    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle)
{
  if (!_initialized || !_pso || !_texture)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->SetGraphicsRootDescriptorTable(3, srvGpuHandle);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  commandList->IASetVertexBuffers(0, 1, &_vbv);
  commandList->IASetIndexBuffer(&_ibv);
  commandList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
}

const std::string DirectXBackgroundShader::_vertexShaderSource =
std::string(R"foo(
struct VSInput
{
  float4 position : POSITION;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.position = input.position;
  // D3D texture origin is top-left; map NDC to UV without the OpenGL Y flip.
  output.texcoord = float2(input.position.x, input.position.y) * 0.5f + 0.5f;
  return output;
}
)foo");

const std::string DirectXBackgroundShader::_pixelShaderSource =
std::string(R"foo(
Texture2D backgroundTexture : register(t0);
SamplerState backgroundSampler : register(s0);

struct PSInput
{
  float4 position : SV_POSITION;
  float2 texcoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  return backgroundTexture.Sample(backgroundSampler, input.texcoord);
}
)foo");
