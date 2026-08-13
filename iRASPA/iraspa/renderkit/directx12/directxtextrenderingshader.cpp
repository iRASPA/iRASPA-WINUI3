/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxtextrenderingshader.h"
#include "rkstring.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include <cstddef>
#include <cstring>

std::unordered_map<std::string, std::unique_ptr<DirectXFontAtlasGpu>> DirectXTextRenderingShader::_fontCache;

void DirectXTextRenderingShader::loadShader(ID3D12Device * /*device*/)
{
}

DirectXFontAtlasGpu *DirectXTextRenderingShader::getOrCreateFontAtlas(const RKString &fontName,
                                                                      ID3D12Device *device)
{
  const std::string key = fontName.toStdString();
  auto it = _fontCache.find(key);
  if (it != _fontCache.end())
    return it->second.get();

  if (!device)
    return nullptr;

  auto entry = std::make_unique<DirectXFontAtlasGpu>();
  entry->cpu = std::make_unique<RKFontAtlas>(fontName, 256);

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 1;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&entry->srvHeap))))
  {
    std::cerr << "DirectXTextRenderingShader: failed to create font SRV heap";
    return nullptr;
  }

  DirectXFontAtlasGpu *raw = entry.get();
  _fontCache.emplace(key, std::move(entry));
  return raw;
}

void DirectXTextRenderingShader::uploadFontAtlasTexture(DirectXFontAtlasGpu *entry, ID3D12Device *device,
                                                        ID3D12GraphicsCommandList *commandList)
{
  if (!entry || !entry->cpu || !device || !commandList || entry->uploaded)
    return;

  const UINT width = 256;
  const UINT height = 256;
  if (entry->cpu->textureData.size() < width * height)
    return;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R8_UNORM;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  entry->texture.Reset();
  if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&entry->texture))))
  {
    std::cerr << "DirectXTextRenderingShader: failed to create font texture";
    return;
  }

  UINT64 uploadSize = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, nullptr, nullptr, &uploadSize);
  entry->uploadBuffer = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
  if (!entry->uploadBuffer)
    return;

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(entry->uploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return;

  for (UINT y = 0; y < height; ++y)
  {
    std::memcpy(mapped + layout.Offset + y * layout.Footprint.RowPitch,
                entry->cpu->textureData.data() + y * width, width);
  }
  entry->uploadBuffer->Unmap(0, nullptr);

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = entry->texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = entry->uploadBuffer.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = layout;

  commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = entry->texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &barrier);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_R8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(entry->texture.Get(), &srvDesc,
                                   entry->srvHeap->GetCPUDescriptorHandleForHeapStart());
  entry->uploaded = true;
}

void DirectXTextRenderingShader::initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                               DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesText, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesText, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEVERTEX", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesText, vertexCoordinatesData)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCETEXCOORDS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesText, textureCoordinatesData)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXTextRenderingShader: failed to create PSO";
    return;
  }
  _psoReady = true;
}

void DirectXTextRenderingShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                            DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  if (!device || !rootSignature)
    return;
  initializePSO(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXTextRenderingShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXTextRenderingShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXTextRenderingShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXTextRenderingShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      StructureBuffers &bufs = _buffers[i][j];
      bufs = StructureBuffers{};

      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      // Avoid building a DirectWrite SDF atlas when labels are off (gallery load path).
      if (source->renderTextType() == RKTextType::none ||
          source->renderTextType() == RKTextType::multiple_values)
        continue;

      bufs.fontName = source->renderTextFont();
      DirectXFontAtlasGpu *atlas = getOrCreateFontAtlas(bufs.fontName, device);
      if (!atlas || !atlas->cpu)
        continue;

      std::vector<RKInPerInstanceAttributesText> textData = source->atomTextData(atlas->cpu.get());
      bufs.instanceCount = static_cast<UINT>(textData.size());
      if (textData.empty())
        continue;

      const size_t bytes = textData.size() * sizeof(RKInPerInstanceAttributesText);
      bufs.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, bytes);
      if (!bufs.instanceBuffer)
        continue;
      DirectXDeviceHelpers::writeUploadBuffer(bufs.instanceBuffer.Get(), textData.data(), bytes);
      bufs.instanceVbv = { bufs.instanceBuffer->GetGPUVirtualAddress(),
                           static_cast<UINT>(bytes),
                           static_cast<UINT>(sizeof(RKInPerInstanceAttributesText)) };
    }
  }
}

void DirectXTextRenderingShader::ensureTexturesUploaded(ID3D12Device *device,
                                                        ID3D12GraphicsCommandList *commandList)
{
  for (size_t i = 0; i < _buffers.size(); ++i)
  {
    for (size_t j = 0; j < _buffers[i].size(); ++j)
    {
      if (_buffers[i][j].instanceCount == 0 || _buffers[i][j].fontName.isEmpty())
        continue;
      DirectXFontAtlasGpu *atlas = getOrCreateFontAtlas(_buffers[i][j].fontName, device);
      uploadFontAtlasTexture(atlas, device, commandList);
    }
  }
}

void DirectXTextRenderingShader::paint(ID3D12GraphicsCommandList *commandList,
                                       D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                       UINT structureCBVStride)
{
  if (!_psoReady || !_pso || !commandList)
    return;

  commandList->SetPipelineState(_pso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      const StructureBuffers &bufs = _buffers[i][j];
      if (source && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && bufs.instanceCount > 0 && bufs.instanceBuffer)
      {
        auto it = _fontCache.find(bufs.fontName.toStdString());
        if (it == _fontCache.end() || !it->second->uploaded || !it->second->srvHeap)
        {
          ++index;
          continue;
        }
        DirectXFontAtlasGpu *atlas = it->second.get();

        ID3D12DescriptorHeap *heaps[] = { atlas->srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootDescriptorTable(3, atlas->srvHeap->GetGPUDescriptorHandleForHeapStart());
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->IASetVertexBuffers(0, 1, &bufs.instanceVbv);
        commandList->DrawInstanced(4, bufs.instanceCount, 0, 0);
      }
      ++index;
    }
  }
}

const std::string DirectXTextRenderingShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
  float4 vertexPosition : INSTANCEVERTEX;
  float4 instanceTexCoords : INSTANCETEXCOORDS;
  uint vertexID : SV_VertexID;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  float4 sphere_radius : TEXCOORD2;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 sphere_radius = structureUniforms.atomScaleFactor * input.instanceScale;
  float4 eye = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  float4 c = structureUniforms.atomAnnotationTextScaling * input.vertexPosition;
  float4 d = input.instanceTexCoords;

  float2 offsets[4] = {
    float2(c.x, -c.y),
    float2(c.x, -c.y - c.w),
    float2(c.x + c.z, -c.y),
    float2(c.x + c.z, -c.y - c.w)
  };
  float2 uvs[4] = {
    float2(d.x, d.y),
    float2(d.x, d.y + d.w),
    float2(d.x + d.z, d.y),
    float2(d.x + d.z, d.y + d.w)
  };

  uint corner = input.vertexID % 4;
  float4 pos = eye;
  pos.xy += offsets[corner] + structureUniforms.atomAnnotationTextDisplacement.xy;
  float4 clip = mul(frameUniforms.projectionMatrix, pos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;

  output.position = clip;
  output.eye_position = eye;
  output.texcoords = uvs[corner];
  output.sphere_radius = sphere_radius;
  return output;
}
)foo");

const std::string DirectXTextRenderingShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
Texture2D fontAtlasTexture : register(t0);
SamplerState fontSampler : register(s0);

struct PSInput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  float4 sphere_radius : TEXCOORD2;
};

struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};

PSOutput PSMain(PSInput input)
{
  PSOutput output;
  float4 pos = input.eye_position;
  pos.z += input.sphere_radius.z + structureUniforms.atomAnnotationTextDisplacement.z;
  pos = mul(frameUniforms.projectionMatrix, pos);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;

  float4 color = structureUniforms.atomAnnotationTextColor;
  float edgeDistance = 0.5;
  float sampleDistance = fontAtlasTexture.Sample(fontSampler, input.texcoords).r;
  float edgeWidth = length(float2(ddx(sampleDistance), ddy(sampleDistance)));
  float insideness = smoothstep(edgeDistance - edgeWidth, edgeDistance + edgeWidth, sampleDistance);
  output.color = float4(color.r * insideness, color.g * insideness, color.b * insideness, insideness);
  return output;
}
)foo");
