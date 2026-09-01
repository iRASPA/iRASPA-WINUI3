/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxglobalaxesshader.h"
#include "rkstring.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxtextrenderingshader.h"
#include "directxuniformstringliterals.h"
#include "geometry/axessystemdefaultgeometry.h"
#include "geometry/backplanegeometry.h"
#include "rkglobalaxes.h"
#include <algorithm>
#include <cstddef>

#if defined(_WIN32)
const RKString kGlobalAxesFont = RKString("Segoe UI");
#else
const RKString kGlobalAxesFont = RKString("Helvetica");
#endif

void DirectXGlobalAxesShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXGlobalAxesShader::initializeBackgroundPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_backgroundVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_backgroundPixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_backgroundPso))))
  {
    std::cerr << "DirectXGlobalAxesShader: failed to create background PSO";
    return;
  }
  _backgroundPsoReady = true;
}

void DirectXGlobalAxesShader::initializeSystemPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_systemVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_systemPixelShaderSource, "PSMain", "ps_5_0");
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
  psoDesc.RasterizerState.DepthClipEnable = FALSE; // match OpenGL GL_DEPTH_CLAMP
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

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_systemPso))))
  {
    std::cerr << "DirectXGlobalAxesShader: failed to create system PSO";
    return;
  }
  _systemPsoReady = true;
}

void DirectXGlobalAxesShader::initializeTextPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_textVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_textPixelShaderSource, "PSMain", "ps_5_0");
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
  psoDesc.RasterizerState.DepthClipEnable = FALSE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_textPso))))
  {
    std::cerr << "DirectXGlobalAxesShader: failed to create text PSO";
    return;
  }
  _textPsoReady = true;
}

void DirectXGlobalAxesShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  if (!device || !rootSignature)
    return;
  initializeBackgroundPSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeSystemPSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeTextPSO(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXGlobalAxesShader::setRenderDataSource(std::shared_ptr<RKRenderDataSource> source)
{
  _dataSource = std::move(source);
}

void DirectXGlobalAxesShader::reloadBackground(ID3D12Device *device)
{
  _backgroundBuffers = MeshBuffers{};
  if (!device)
    return;

  BackPlaneGeometry plane;
  const auto &vertices = plane.vertices();
  const auto &indices = plane.indices();
  const size_t vbBytes = vertices.size() * sizeof(RKVertex);
  const size_t ibBytes = indices.size() * sizeof(short);
  _backgroundBuffers.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbBytes, 1));
  _backgroundBuffers.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibBytes, 1));
  DirectXDeviceHelpers::writeUploadBuffer(_backgroundBuffers.vertexBuffer.Get(), vertices.data(), vbBytes);
  DirectXDeviceHelpers::writeUploadBuffer(_backgroundBuffers.indexBuffer.Get(), indices.data(), ibBytes);
  _backgroundBuffers.vbv = { _backgroundBuffers.vertexBuffer->GetGPUVirtualAddress(),
                             static_cast<UINT>(std::max<size_t>(vbBytes, 1)), sizeof(RKVertex) };
  _backgroundBuffers.ibv = { _backgroundBuffers.indexBuffer->GetGPUVirtualAddress(),
                             static_cast<UINT>(std::max<size_t>(ibBytes, 1)), DXGI_FORMAT_R16_UINT };
  _backgroundBuffers.indexCount = static_cast<UINT>(indices.size());
}

void DirectXGlobalAxesShader::reloadSystem(ID3D12Device *device)
{
  _systemBuffers = MeshBuffers{};
  if (!device || !_dataSource || !_dataSource->axes())
    return;

  auto *axes = _dataSource->axes().get();
  AxesSystemDefaultGeometry geometry(
      axes->centerType(), axes->centerScale(),
      float4(axes->centerDiffuseColor().redF(), axes->centerDiffuseColor().greenF(),
             axes->centerDiffuseColor().blueF(), axes->centerDiffuseColor().alphaF()),
      axes->shaftLength(), axes->shaftWidth(),
      float4(axes->axisXDiffuseColor().redF(), axes->axisXDiffuseColor().greenF(),
             axes->axisXDiffuseColor().blueF(), axes->axisXDiffuseColor().alphaF()),
      float4(axes->axisYDiffuseColor().redF(), axes->axisYDiffuseColor().greenF(),
             axes->axisYDiffuseColor().blueF(), axes->axisYDiffuseColor().alphaF()),
      float4(axes->axisZDiffuseColor().redF(), axes->axisZDiffuseColor().greenF(),
             axes->axisZDiffuseColor().blueF(), axes->axisZDiffuseColor().alphaF()),
      axes->tipLength(), axes->tipWidth(),
      float4(axes->axisXDiffuseColor().redF(), axes->axisXDiffuseColor().greenF(),
             axes->axisXDiffuseColor().blueF(), axes->axisXDiffuseColor().alphaF()),
      float4(axes->axisYDiffuseColor().redF(), axes->axisYDiffuseColor().greenF(),
             axes->axisYDiffuseColor().blueF(), axes->axisYDiffuseColor().alphaF()),
      float4(axes->axisZDiffuseColor().redF(), axes->axisZDiffuseColor().greenF(),
             axes->axisZDiffuseColor().blueF(), axes->axisZDiffuseColor().alphaF()),
      axes->tipVisibility(), axes->aspectRatio(), axes->NumberOfSectors());

  const auto &vertices = geometry.vertices();
  const auto &indices = geometry.indices();
  if (vertices.empty() || indices.empty())
    return;

  const size_t vbBytes = vertices.size() * sizeof(RKPrimitiveVertex);
  const size_t ibBytes = indices.size() * sizeof(short);
  _systemBuffers.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbBytes);
  _systemBuffers.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibBytes);
  DirectXDeviceHelpers::writeUploadBuffer(_systemBuffers.vertexBuffer.Get(), vertices.data(), vbBytes);
  DirectXDeviceHelpers::writeUploadBuffer(_systemBuffers.indexBuffer.Get(), indices.data(), ibBytes);
  _systemBuffers.vbv = { _systemBuffers.vertexBuffer->GetGPUVirtualAddress(),
                         static_cast<UINT>(vbBytes), sizeof(RKPrimitiveVertex) };
  _systemBuffers.ibv = { _systemBuffers.indexBuffer->GetGPUVirtualAddress(),
                         static_cast<UINT>(ibBytes), DXGI_FORMAT_R16_UINT };
  _systemBuffers.indexCount = static_cast<UINT>(indices.size());
}

void DirectXGlobalAxesShader::reloadText(ID3D12Device *device)
{
  _textBuffers = TextBuffers{};
  if (!device || !_dataSource)
    return;

  _textBuffers.fontName = kGlobalAxesFont;
  DirectXFontAtlasGpu *atlas = DirectXTextRenderingShader::getOrCreateFontAtlas(kGlobalAxesFont, device);
  if (!atlas || !atlas->cpu)
    return;

  auto x = atlas->cpu->buildMeshWithString(float4(1.0, 0.0, 0.0, 1.0), float4(3, 3, 3, 3),
                                           RKString("X"), RKTextAlignment::center);
  auto y = atlas->cpu->buildMeshWithString(float4(0.0, 1.0, 0.0, 1.0), float4(3, 3, 3, 3),
                                           RKString("Y"), RKTextAlignment::center);
  auto z = atlas->cpu->buildMeshWithString(float4(0.0, 0.0, 1.0, 1.0), float4(3, 3, 3, 3),
                                           RKString("Z"), RKTextAlignment::center);

  // Match OpenGL: one glyph instance per axis (X/Y/Z).
  std::vector<RKInPerInstanceAttributesText> instances;
  if (!x.empty()) instances.push_back(x.front());
  if (!y.empty()) instances.push_back(y.front());
  if (!z.empty()) instances.push_back(z.front());
  if (instances.empty())
    return;

  const size_t bytes = instances.size() * sizeof(RKInPerInstanceAttributesText);
  _textBuffers.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, bytes);
  DirectXDeviceHelpers::writeUploadBuffer(_textBuffers.instanceBuffer.Get(), instances.data(), bytes);
  _textBuffers.instanceVbv = { _textBuffers.instanceBuffer->GetGPUVirtualAddress(),
                               static_cast<UINT>(bytes),
                               static_cast<UINT>(sizeof(RKInPerInstanceAttributesText)) };
  _textBuffers.instanceCount = static_cast<UINT>(instances.size());
}

void DirectXGlobalAxesShader::reloadData(ID3D12Device *device)
{
  reloadBackground(device);
  reloadSystem(device);
  reloadText(device);
}

void DirectXGlobalAxesShader::ensureTexturesUploaded(ID3D12Device *device,
                                                     ID3D12GraphicsCommandList *commandList)
{
  if (_textBuffers.instanceCount == 0)
    return;
  DirectXFontAtlasGpu *atlas = DirectXTextRenderingShader::getOrCreateFontAtlas(kGlobalAxesFont, device);
  DirectXTextRenderingShader::uploadFontAtlasTexture(atlas, device, commandList);
}

void DirectXGlobalAxesShader::setAxesViewport(ID3D12GraphicsCommandList *commandList, int width, int height)
{
  if (!_dataSource || !_dataSource->axes() || !commandList)
    return;

  auto *axes = _dataSource->axes().get();
  if (axes->position() == RKGlobalAxes::Position::none)
    return;

  const double minSize = std::min(width, height);
  const double border = minSize * axes->borderOffsetScreenFraction();
  const double size = minSize * axes->sizeScreenFraction();

  // OpenGL viewport Y is from bottom; D3D12 TopLeftY is from top.
  double glX = 0.0;
  double glY = 0.0;
  switch (axes->position())
  {
  case RKGlobalAxes::Position::none:
    return;
  case RKGlobalAxes::Position::bottomLeft:
    glX = border; glY = border; break;
  case RKGlobalAxes::Position::midLeft:
    glX = border; glY = 0.5 * minSize - 0.5 * size; break;
  case RKGlobalAxes::Position::topLeft:
    glX = border; glY = height - (border + size); break;
  case RKGlobalAxes::Position::midTop:
    glX = 0.5 * width - 0.5 * size; glY = height - (border + size); break;
  case RKGlobalAxes::Position::topRight:
    glX = width - (border + size); glY = height - (border + size); break;
  case RKGlobalAxes::Position::midRight:
    glX = width - (border + size); glY = 0.5 * minSize - 0.5 * size; break;
  case RKGlobalAxes::Position::bottomRight:
    glX = width - (border + size); glY = border; break;
  case RKGlobalAxes::Position::midBottom:
    glX = 0.5 * width - 0.5 * size; glY = border; break;
  case RKGlobalAxes::Position::center:
    glX = 0.5 * width - 0.5 * size; glY = 0.5 * minSize - 0.5 * size; break;
  }

  D3D12_VIEWPORT viewport = {};
  viewport.TopLeftX = static_cast<float>(glX);
  viewport.TopLeftY = static_cast<float>(height - glY - size);
  viewport.Width = static_cast<float>(size);
  viewport.Height = static_cast<float>(size);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissor = {};
  scissor.left = static_cast<LONG>(viewport.TopLeftX);
  scissor.top = static_cast<LONG>(viewport.TopLeftY);
  scissor.right = static_cast<LONG>(viewport.TopLeftX + viewport.Width);
  scissor.bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height);
  commandList->RSSetScissorRects(1, &scissor);
}

void DirectXGlobalAxesShader::paint(ID3D12GraphicsCommandList *commandList,
                                    D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                                    D3D12_GPU_VIRTUAL_ADDRESS lightsCBV,
                                    D3D12_GPU_VIRTUAL_ADDRESS globalAxesCBV,
                                    int width, int height)
{
  if (!commandList || !_dataSource || !_dataSource->axes())
    return;
  if (_dataSource->axes()->position() == RKGlobalAxes::Position::none)
    return;

  setAxesViewport(commandList, width, height);
  commandList->SetGraphicsRootConstantBufferView(0, frameCBV);
  commandList->SetGraphicsRootConstantBufferView(2, lightsCBV);
  commandList->SetGraphicsRootConstantBufferView(5, globalAxesCBV);

  if (_backgroundPsoReady && _backgroundPso && _backgroundBuffers.indexCount > 0)
  {
    commandList->SetPipelineState(_backgroundPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &_backgroundBuffers.vbv);
    commandList->IASetIndexBuffer(&_backgroundBuffers.ibv);
    commandList->DrawIndexedInstanced(_backgroundBuffers.indexCount, 1, 0, 0, 0);
  }

  if (_systemPsoReady && _systemPso && _systemBuffers.indexCount > 0)
  {
    commandList->SetPipelineState(_systemPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &_systemBuffers.vbv);
    commandList->IASetIndexBuffer(&_systemBuffers.ibv);
    commandList->DrawIndexedInstanced(_systemBuffers.indexCount, 1, 0, 0, 0);
  }

  if (_textPsoReady && _textPso && _textBuffers.instanceCount > 0 && _textBuffers.instanceBuffer)
  {
    DirectXFontAtlasGpu *atlas = DirectXTextRenderingShader::getOrCreateFontAtlas(kGlobalAxesFont, nullptr);
    if (atlas && atlas->uploaded && atlas->srvHeap)
    {
      ID3D12DescriptorHeap *heaps[] = { atlas->srvHeap.Get() };
      commandList->SetDescriptorHeaps(1, heaps);
      commandList->SetGraphicsRootDescriptorTable(3, atlas->srvHeap->GetGPUDescriptorHandleForHeapStart());
      commandList->SetPipelineState(_textPso.Get());
      commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      commandList->IASetVertexBuffers(0, 1, &_textBuffers.instanceVbv);
      commandList->DrawInstanced(4, _textBuffers.instanceCount, 0, 0);
    }
  }
}

const std::string DirectXGlobalAxesShader::_backgroundVertexShaderSource =
std::string(R"foo(
struct VSInput { float4 vertexPosition : POSITION; };
struct VSOutput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };
VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.position = input.vertexPosition;
  output.texcoord = input.vertexPosition.xy * 0.5 + 0.5;
  return output;
}
)foo");

const std::string DirectXGlobalAxesShader::_backgroundPixelShaderSource =
DirectXUniformStringLiterals::GlobalAxesUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };

float Sphere(float2 p, float s) { return length(p) - s; }
float RoundedBox(float2 p, float2 b, float r) { return length(max(abs(p) - b, 0.0)) - r; }
float Rectangle(float2 uv, float2 pos, float2 size)
{
  return (step(pos.x, uv.x) - step(pos.x + size.x, uv.x))
       * (step(pos.y - size.y, uv.y) - step(pos.y, uv.y));
}

float4 PSMain(PSInput input) : SV_TARGET
{
  float alpha = globalAxesUniforms.axesBackgroundColor.w;
  float2 texcoord = input.texcoord;
  switch (globalAxesUniforms.axesBackGroundStyle)
  {
  case 0: alpha = 0.0; break;
  case 1:
    if (Sphere(texcoord - float2(0.5, 0.5), 0.5) > 0.0) alpha = 0.0;
    break;
  case 2: break;
  case 3:
    if (RoundedBox(texcoord - float2(0.5, 0.5), float2(0.3, 0.3), 0.2) > 0.0) alpha = 0.0;
    break;
  case 4:
    if (max(-Sphere(texcoord - float2(0.5, 0.5), 0.48), Sphere(texcoord - float2(0.5, 0.5), 0.5)) > 0.0)
      alpha = 0.0;
    break;
  case 5:
    if (Rectangle(texcoord - float2(0.5, 0.5), float2(-0.48, 0.48), float2(0.96, 0.96)) > 0.0)
      alpha = 0.0;
    break;
  case 6:
    if (max(-RoundedBox(texcoord - float2(0.5, 0.5), float2(0.30, 0.30), 0.17),
            RoundedBox(texcoord - float2(0.5, 0.5), float2(0.3, 0.3), 0.2)) > 0.0)
      alpha = 0.0;
    break;
  default: break;
  }
  return float4(globalAxesUniforms.axesBackgroundColor.xyz * alpha, alpha);
}
)foo");

const std::string DirectXGlobalAxesShader::_systemVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::GlobalAxesUniformBlockStringLiteral +
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
  float4 scale = float4(globalAxesUniforms.axesScale, globalAxesUniforms.axesScale,
                        globalAxesUniforms.axesScale, 1.0);
  float4 pos = scale * input.vertexPosition + float4(0.0, 0.0, 0.0, 1.0);
  output.N = mul(frameUniforms.normalMatrix, input.vertexNormal).xyz;
  output.diffuse = input.vertexColor;
  // The widget has its own view, so this eye space is the axes' rather than the scene's, which is
  // the convention the lights were already read in here.
  float4 P = mul(frameUniforms.axesViewMatrix, pos);
  output.V = -P.xyz;
  float4 clip = mul(frameUniforms.axesMvpMatrix, pos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXGlobalAxesShader::_systemPixelShaderSource =
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
  // Unshadowed: the widget sits outside the scene the mask was traced for.
  LightingWeights lighting = accumulateLighting(N, normalize(input.V), float4(-input.V, 1.0), 0.0);
  float3 shade = (guideGeometryAmbient * lighting.ambient + lighting.diffuse) * input.diffuse.xyz;
  float3 ldr = 1.0 - exp2(-shade * 1.5);
  return float4(ldr, 1.0);
}
)foo");

const std::string DirectXGlobalAxesShader::_textVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::GlobalAxesUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
  float4 vertexPosition : INSTANCEVERTEX;
  float4 instanceTexCoords : INSTANCETEXCOORDS;
  uint vertexID : SV_VertexID;
  uint instanceID : SV_InstanceID;
};
struct VSOutput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  float4 sphere_radius : TEXCOORD2;
  nointerpolation uint instanceID : TEXCOORD3;
};
VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float axisScale = globalAxesUniforms.textScale[input.instanceID];
  float4 sphere_radius = axisScale * input.instanceScale;
  float4 textPosition = input.instancePosition;
  float scale = globalAxesUniforms.axesScale + 2.0 * globalAxesUniforms.centerScale
                + globalAxesUniforms.textOffset + axisScale;
  textPosition.xyz *= scale;
  float4 eye = mul(frameUniforms.axesViewMatrix, textPosition);

  float4 c = axisScale * input.instanceScale * input.vertexPosition;
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
  pos.xy += offsets[corner] + globalAxesUniforms.textDisplacement[input.instanceID].xy;
  float4 clip = mul(frameUniforms.axesProjectionMatrix, pos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;

  output.position = clip;
  output.eye_position = eye;
  output.texcoords = uvs[corner];
  output.sphere_radius = sphere_radius;
  output.instanceID = input.instanceID;
  return output;
}
)foo");

const std::string DirectXGlobalAxesShader::_textPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::GlobalAxesUniformBlockStringLiteral +
std::string(R"foo(
Texture2D fontAtlasTexture : register(t0);
SamplerState fontSampler : register(s0);
struct PSInput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  float4 sphere_radius : TEXCOORD2;
  nointerpolation uint instanceID : TEXCOORD3;
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
  pos.z += input.sphere_radius.z + globalAxesUniforms.textDisplacement[input.instanceID].z;
  pos = mul(frameUniforms.axesProjectionMatrix, pos);
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;

  float4 color = globalAxesUniforms.textColor[input.instanceID];
  float edgeDistance = 0.5;
  float sampleDistance = fontAtlasTexture.Sample(fontSampler, input.texcoords).r;
  float edgeWidth = length(float2(ddx(sampleDistance), ddy(sampleDistance)));
  float insideness = smoothstep(edgeDistance - edgeWidth, edgeDistance + edgeWidth, sampleDistance);
  output.color = float4(color.r * insideness, color.g * insideness, color.b * insideness, insideness);
  return output;
}
)foo");
