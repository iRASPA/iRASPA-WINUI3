/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxedgecueingshader.h"

#include <algorithm>
#include <iostream>

#include "directxdevicehelpers.h"
#include "directxedgecueingstringliterals.h"
#include "geometry/backplanegeometry.h"
#include "rkrenderuniforms.h"

void DirectXEdgeCueingShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXEdgeCueingShader::createRootSignature(ID3D12Device *device)
{
  // Colour, depth and stencil in one table, in that order, so one heap serves the pass.
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 3;
  srvRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 2;
  rootDesc.pParameters = params;
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

void DirectXEdgeCueingShader::createFullscreenQuad(ID3D12Device *device)
{
  BackPlaneGeometry quad;
  const auto &vertices = quad.vertices();
  const auto &indices = quad.indices();
  _indexCount = static_cast<UINT>(indices.size());
  const size_t vbSize = vertices.size() * sizeof(RKVertex);
  const size_t ibSize = indices.size() * sizeof(short);
  _vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbSize);
  _indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibSize);
  if (!_vertexBuffer || !_indexBuffer)
    return;
  DirectXDeviceHelpers::writeUploadBuffer(_vertexBuffer.Get(), vertices.data(), vbSize);
  DirectXDeviceHelpers::writeUploadBuffer(_indexBuffer.Get(), indices.data(), ibSize);
  _vbv.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
  _vbv.SizeInBytes = static_cast<UINT>(vbSize);
  _vbv.StrideInBytes = sizeof(RKVertex);
  _ibv.BufferLocation = _indexBuffer->GetGPUVirtualAddress();
  _ibv.SizeInBytes = static_cast<UINT>(ibSize);
  _ibv.Format = DXGI_FORMAT_R16_UINT;
}

void DirectXEdgeCueingShader::createHeap(ID3D12Device *device)
{
  // Two sets of three: the rasterized sources in 0 to 2 and the traced ones in 3 to 5. A frame is
  // one or the other, but the views differ in kind and keeping them apart costs three descriptors.
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 6;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap));
  _srvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXEdgeCueingShader::cpuHandle(UINT slot) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(slot) * _srvStride;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXEdgeCueingShader::gpuHandle(UINT slot) const
{
  D3D12_GPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(slot) * _srvStride;
  return handle;
}

void DirectXEdgeCueingShader::initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat,
                                         UINT sceneSampleCount)
{
  if (!device)
    return;

  _rtvFormat = rtvFormat;
  _sceneSampleCount = (std::max)(1u, sceneSampleCount);

  createRootSignature(device);
  if (!_rootSignature)
    return;

  using Source = DirectXEdgeCueingStringLiterals::Source;
  const Source rasterSource =
      _sceneSampleCount > 1 ? Source::rasterizedMultisampled : Source::rasterized;

  ComPtr<ID3DBlob> vs =
      compileShader(DirectXEdgeCueingStringLiterals::vertexShaderSource(), "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps =
      compileShader(DirectXEdgeCueingStringLiterals::pixelShaderSource(rasterSource), "PSMain",
                    "ps_5_0");
  ComPtr<ID3DBlob> tracedPs =
      compileShader(DirectXEdgeCueingStringLiterals::pixelShaderSource(Source::traced), "PSMain",
                    "ps_5_0");
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

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXEdgeCueingShader: failed to create PSO";
    return;
  }

  // A traced frame is cued from the tracer's own buffers. Failing to make this one leaves the
  // rasterized cues working and a traced image uncued, which is worth having over neither.
  if (tracedPs)
  {
    psoDesc.PS = { tracedPs->GetBufferPointer(), tracedPs->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_tracedPso))))
      std::cerr << "DirectXEdgeCueingShader: failed to create the traced PSO";
  }

  createFullscreenQuad(device);
  createHeap(device);
  _ready = _pso && _srvHeap && _vertexBuffer && _indexBuffer && _indexCount > 0;
}

ID3D12Resource *DirectXEdgeCueingShader::sceneTexture(ID3D12Device *device, int width, int height)
{
  if (!device)
    return nullptr;

  const int w = (std::max)(1, width);
  const int h = (std::max)(1, height);
  if (_sceneTexture && _sceneWidth == w && _sceneHeight == h)
    return _sceneTexture.Get();

  _sceneTexture.Reset();
  _sceneWidth = w;
  _sceneHeight = h;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(w);
  desc.Height = static_cast<UINT>(h);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = _rtvFormat;
  desc.SampleDesc.Count = 1;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                             IID_PPV_ARGS(&_sceneTexture))))
  {
    _sceneWidth = 0;
    _sceneHeight = 0;
    return nullptr;
  }
  return _sceneTexture.Get();
}

void DirectXEdgeCueingShader::paint(ID3D12GraphicsCommandList *commandList,
                                    D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
                                    D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                                    ID3D12Resource *sceneColor, ID3D12Resource *sceneDepth,
                                    ID3D12Resource *sceneStencil, int width, int height)
{
  if (!_ready || !commandList || !sceneColor || !sceneDepth || !sceneStencil)
    return;

  ID3D12Device *device = nullptr;
  if (FAILED(sceneColor->GetDevice(IID_PPV_ARGS(&device))) || !device)
    return;

  // The views are made again for every frame drawn: all three resources are destroyed and remade
  // whenever the window is resized or a picture is exported at another size.
  D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv = {};
  colorSrv.Format = sceneColor->GetDesc().Format;
  colorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  colorSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(sceneColor, &colorSrv, cpuHandle(0));

  // The depth half of the buffer, the resolved one: the cues judge a step in depth, and a step read
  // from one sample of a silhouette is the aliasing rather than the step.
  D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
  depthSrv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depthSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(sceneDepth, &depthSrv, cpuHandle(1));

  // The stencil half, as the scene drew it. Multisampled it stays multisampled, only sample zero
  // being read: a silhouette one sample ragged is not visible under a cue several pixels wide.
  D3D12_SHADER_RESOURCE_VIEW_DESC stencilSrv = {};
  stencilSrv.Format = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
  stencilSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (_sceneSampleCount > 1)
  {
    stencilSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  }
  else
  {
    stencilSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stencilSrv.Texture2D.MipLevels = 1;
    stencilSrv.Texture2D.PlaneSlice = 1;
  }
  device->CreateShaderResourceView(sceneStencil, &stencilSrv, cpuHandle(2));
  device->Release();

  drawFullscreen(commandList, _pso.Get(), destinationRtv, frameConstants, 0, width, height);
}

void DirectXEdgeCueingShader::paintTraced(ID3D12GraphicsCommandList *commandList,
                                          D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
                                          D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                                          ID3D12Resource *compositeColor,
                                          ID3D12Resource *tracedDepth,
                                          ID3D12Resource *tracedCueMask, int width, int height)
{
  if (!canPaintTraced() || !commandList || !compositeColor || !tracedDepth || !tracedCueMask)
    return;

  ID3D12Device *device = nullptr;
  if (FAILED(compositeColor->GetDevice(IID_PPV_ARGS(&device))) || !device)
    return;

  D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv = {};
  colorSrv.Format = compositeColor->GetDesc().Format;
  colorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  colorSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(compositeColor, &colorSrv, cpuHandle(3));

  const UINT pixels = static_cast<UINT>((std::max)(1, width)) * static_cast<UINT>((std::max)(1, height));

  D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
  depthSrv.Format = DXGI_FORMAT_UNKNOWN;
  depthSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depthSrv.Buffer.NumElements = pixels;
  depthSrv.Buffer.StructureByteStride = sizeof(float);
  device->CreateShaderResourceView(tracedDepth, &depthSrv, cpuHandle(4));

  D3D12_SHADER_RESOURCE_VIEW_DESC maskSrv = depthSrv;
  maskSrv.Buffer.StructureByteStride = sizeof(uint32_t);
  device->CreateShaderResourceView(tracedCueMask, &maskSrv, cpuHandle(5));
  device->Release();

  drawFullscreen(commandList, _tracedPso.Get(), destinationRtv, frameConstants, 3, width, height);
}

void DirectXEdgeCueingShader::drawFullscreen(ID3D12GraphicsCommandList *commandList,
                                             ID3D12PipelineState *pso,
                                             D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv,
                                             D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
                                             UINT firstSrvSlot, int width, int height)
{
  commandList->SetPipelineState(pso);
  commandList->SetGraphicsRootSignature(_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _srvHeap.Get() };
  commandList->SetDescriptorHeaps(1, heaps);
  commandList->SetGraphicsRootConstantBufferView(0, frameConstants);
  commandList->SetGraphicsRootDescriptorTable(1, gpuHandle(firstSrvSlot));
  commandList->OMSetRenderTargets(1, &destinationRtv, FALSE, nullptr);

  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>((std::max)(1, width));
  viewport.Height = static_cast<float>((std::max)(1, height));
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);
  D3D12_RECT scissor = { 0, 0, (std::max)(1, width), (std::max)(1, height) };
  commandList->RSSetScissorRects(1, &scissor);

  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  commandList->IASetVertexBuffers(0, 1, &_vbv);
  commandList->IASetIndexBuffer(&_ibv);
  commandList->DrawIndexedInstanced(_indexCount, 1, 0, 0, 0);
}
