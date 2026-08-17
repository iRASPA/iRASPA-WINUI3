/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxpickingshader.h"
#include <iostream>
#include "directxbondimposter.h"
#include "directxdevicehelpers.h"
#include "directxribbonshader.h"
#include "directxatomsphereshader.h"
#include "directxatomorthographicimpostershader.h"
#include "directxbondshader.h"
#include "directxobjectshader.h"
#include "directxuniformstringliterals.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

DirectXPickingShader::~DirectXPickingShader()
{
  if (_pickFenceEvent)
  {
    CloseHandle(_pickFenceEvent);
    _pickFenceEvent = nullptr;
  }
}

void DirectXPickingShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXPickingShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                      ID3D12CommandQueue *commandQueue)
{
  _device = device;
  _rootSignature = rootSignature;
  _commandQueue = commandQueue;
  if (!device || !rootSignature)
    return;

  ensurePickCommandResources(device);

  {
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
        static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
        D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
      { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
        static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
        D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
      { "INSTANCETAG", 0, DXGI_FORMAT_R32_SINT, 1,
        static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, tag)),
        D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    auto createAtomPso = [&](bool orthographic) {
      ComPtr<ID3DBlob> vs = compileShader(atomVertexShaderSource(orthographic), "VSMain", "vs_5_0");
      ComPtr<ID3DBlob> ps = compileShader(atomPixelShaderSource(orthographic), "PSMain", "ps_5_0");
      if (!vs || !ps)
        return;

      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.pRootSignature = rootSignature;
      psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
      psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
      psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      psoDesc.SampleMask = UINT_MAX;
      psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
      psoDesc.DepthStencilState.DepthEnable = TRUE;
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      psoDesc.NumRenderTargets = 1;
      psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_UINT;
      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.SampleDesc.Count = 1;

      ComPtr<ID3D12PipelineState> &pso = orthographic ? _atomPso : _atomPerspectivePso;
      bool &ready = orthographic ? _atomPsoReady : _atomPerspectivePsoReady;
      if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
        std::cerr << "DirectXPickingShader: failed to create "
                  << (orthographic ? "orthographic" : "perspective") << " atom pick PSO";
      else
        ready = true;
    };

    createAtomPso(true);
    createAtomPso(false);
  }

  {
    ComPtr<ID3DBlob> vs = compileShader(_bondVertexShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_bondPixelShaderSource, "PSMain", "ps_5_0");
    ComPtr<ID3DBlob> externalPs = compileShader(_externalBondPixelShaderSource, "PSMain", "ps_5_0");
    if (vs && ps)
    {
      D3D12_INPUT_ELEMENT_DESC inputLayout[DirectXBondImposter::pickingInputLayoutSize];
      DirectXBondImposter::fillPickingInputLayout(inputLayout);

      auto createBondPso = [&](ID3DBlob *shaderPs, ComPtr<ID3D12PipelineState> &out, bool &ready, const char *name) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSignature;
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { shaderPs->GetBufferPointer(), shaderPs->GetBufferSize() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        // The imposter hull is built in the vertex shader with view-dependent winding.
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.InputLayout = { inputLayout, DirectXBondImposter::pickingInputLayoutSize };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_UINT;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out))))
          std::cerr << "DirectXPickingShader: failed to create" << name;
        else
          ready = true;
      };

      createBondPso(ps.Get(), _bondPso, _bondPsoReady, "bond pick PSO");
      if (externalPs)
        createBondPso(externalPs.Get(), _externalBondPso, _externalBondPsoReady, "external bond pick PSO");
    }
  }

  {
    ComPtr<ID3DBlob> vs = compileShader(_objectVertexShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_objectPixelShaderSource, "PSMain", "ps_5_0");
    if (vs && ps)
    {
      D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
          static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INSTANCETAG", 0, DXGI_FORMAT_R32_SINT, 1,
          static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, tag)),
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
      };

      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
      psoDesc.pRootSignature = rootSignature;
      psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
      psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
      psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      psoDesc.SampleMask = UINT_MAX;
      psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
      psoDesc.DepthStencilState.DepthEnable = TRUE;
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      psoDesc.NumRenderTargets = 1;
      psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_UINT;
      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.SampleDesc.Count = 1;

      if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_objectPso))))
        std::cerr << "DirectXPickingShader: failed to create object pick PSO";
      else
        _objectPsoReady = true;
    }
  }

  {
    ComPtr<ID3DBlob> vs = compileShader(_ribbonVertexShaderSource, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compileShader(_ribbonPixelShaderSource, "PSMain", "ps_5_0");
    if (vs && ps)
    {
      // Same layout as the ribbon's opaque pass: the segment index rides in the normal's w and the
      // residue index in pad.y, which is all the pick shader reads besides the position.
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
      psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
      psoDesc.DepthStencilState.DepthEnable = TRUE;
      psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
      psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
      psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
      psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      psoDesc.NumRenderTargets = 1;
      psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_UINT;
      psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
      psoDesc.SampleDesc.Count = 1;

      if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_ribbonPso))))
        std::cerr << "DirectXPickingShader: failed to create ribbon pick PSO";
      else
        _ribbonPsoReady = true;
    }
  }

  createPickTargets(device, 512, 512);
}

void DirectXPickingShader::ensurePickCommandResources(ID3D12Device *device)
{
  if (!_pickAllocator)
  {
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_pickAllocator));
  }
  if (!_pickCommandList && _pickAllocator)
  {
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _pickAllocator.Get(), nullptr,
                              IID_PPV_ARGS(&_pickCommandList));
    _pickCommandList->Close();
  }
  if (!_pickFence)
  {
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_pickFence));
    _pickFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  }
}

void DirectXPickingShader::waitForPickGPU()
{
  if (!_commandQueue || !_pickFence || !_pickFenceEvent)
    return;

  const UINT64 value = ++_pickFenceValue;
  if (FAILED(_commandQueue->Signal(_pickFence.Get(), value)))
    return;
  if (_pickFence->GetCompletedValue() < value)
  {
    _pickFence->SetEventOnCompletion(value, _pickFenceEvent);
    WaitForSingleObject(_pickFenceEvent, INFINITE);
  }
}

void DirectXPickingShader::createPickTargets(ID3D12Device *device, int width, int height)
{
  width = std::max(1, width);
  height = std::max(1, height);
  if (_pickColor && _width == width && _height == height)
    return;

  _width = width;
  _height = height;
  _pickColor.Reset();
  _pickDepth.Reset();
  _readbackBuffer.Reset();
  _pickColorState = D3D12_RESOURCE_STATE_COMMON;

  if (!_rtvHeap)
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = 1;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_rtvHeap));
  }
  if (!_dsvHeap)
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = 1;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_dsvHeap));
  }

  D3D12_HEAP_PROPERTIES heapProp = {};
  heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

  {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&_pickColor))))
    {
      std::cerr << "DirectXPickingShader: failed to create pick color target";
      return;
    }
    device->CreateRenderTargetView(_pickColor.Get(), nullptr, _rtvHeap->GetCPUDescriptorHandleForHeapStart());
  }

  {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                               IID_PPV_ARGS(&_pickDepth))))
    {
      std::cerr << "DirectXPickingShader: failed to create pick depth target";
      return;
    }
    device->CreateDepthStencilView(_pickDepth.Get(), nullptr, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
  }

  // One pixel RGBA32UI = 16 bytes, but pitch must be 256-aligned.
  const UINT64 readbackSize = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
  D3D12_HEAP_PROPERTIES readHeap = {};
  readHeap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC readDesc = {};
  readDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readDesc.Width = readbackSize;
  readDesc.Height = 1;
  readDesc.DepthOrArraySize = 1;
  readDesc.MipLevels = 1;
  readDesc.SampleDesc.Count = 1;
  readDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &readDesc,
                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                  IID_PPV_ARGS(&_readbackBuffer));
}

void DirectXPickingShader::resize(ID3D12Device *device, int width, int height)
{
  if (!device)
    return;
  createPickTargets(device, width, height);
}

void DirectXPickingShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = std::move(structures);
}

void DirectXPickingShader::reloadData(ID3D12Device * /*device*/)
{
  // Atom, bond and object instance/mesh buffers are owned by those shaders (Cocoa pattern).
}


void DirectXPickingShader::drawAtomPick(ID3D12GraphicsCommandList *commandList,
                                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                        UINT structureCBVStride)
{
  ID3D12PipelineState *pso = _orthographic ? (_atomPsoReady ? _atomPso.Get() : nullptr)
                                           : (_atomPerspectivePsoReady ? _atomPerspectivePso.Get() : nullptr);
  if (!pso || !_atomSphereShader || !_atomOrthoImposterShader)
    return;
  if (!_atomOrthoImposterShader->isQuadReady())
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  const D3D12_VERTEX_BUFFER_VIEW quadVbv = _atomOrthoImposterShader->quadVbv();
  const D3D12_INDEX_BUFFER_VIEW quadIbv = _atomOrthoImposterShader->quadIbv();
  const UINT quadIndexCount = _atomOrthoImposterShader->quadIndexCount();

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && _atomSphereShader->isInstanceReady(i, j))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        D3D12_VERTEX_BUFFER_VIEW views[2] = { quadVbv, _atomSphereShader->instanceVbv(i, j) };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&quadIbv);
        commandList->DrawIndexedInstanced(quadIndexCount, _atomSphereShader->instanceCount(i, j), 0, 0, 0);
      }
      ++index;
    }
  }
}

void DirectXPickingShader::drawBondPick(ID3D12GraphicsCommandList *commandList,
                                        D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                        UINT structureCBVStride,
                                        ID3D12PipelineState *pso, bool psoReady, bool internal)
{
  if (!psoReady || !pso || !_bondShader)
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _bondShader->drawPickGeometry(commandList, structureCBVBase, structureCBVStride, internal);
}

void DirectXPickingShader::drawObjectPick(ID3D12GraphicsCommandList *commandList,
                                          D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                          UINT structureCBVStride)
{
  if (!_objectPsoReady || !_objectPso || !_objectShader)
    return;

  commandList->SetPipelineState(_objectPso.Get());
  _objectShader->drawPickGeometry(commandList, structureCBVBase, structureCBVStride);
}

void DirectXPickingShader::drawRibbonPick(ID3D12GraphicsCommandList *commandList,
                                          D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                          UINT structureCBVStride)
{
  if (!_ribbonPsoReady || !_ribbonPso || !_ribbonShader)
    return;

  commandList->SetPipelineState(_ribbonPso.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  std::vector<RKRibbonChainDrawRange> visibleRanges;
  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      D3D12_VERTEX_BUFFER_VIEW vbv = {};
      D3D12_INDEX_BUFFER_VIEW ibv = {};
      if (_ribbonShader->pickGeometry(i, j, vbv, ibv, visibleRanges))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->IASetVertexBuffers(0, 1, &vbv);
        commandList->IASetIndexBuffer(&ibv);
        for (const RKRibbonChainDrawRange &range : visibleRanges)
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

void DirectXPickingShader::drawPickContents(ID3D12GraphicsCommandList *commandList,
                                            D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                                            D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                            UINT structureCBVStride)
{
  if (!_pickColor || !_pickDepth || !_rootSignature)
    return;

  if (_pickColorState != D3D12_RESOURCE_STATE_RENDER_TARGET)
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _pickColor.Get();
    barrier.Transition.StateBefore = _pickColorState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    _pickColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
  commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

  const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
  commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
  commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(_width);
  viewport.Height = static_cast<float>(_height);
  viewport.MaxDepth = 1.0f;
  commandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissor = {};
  scissor.right = _width;
  scissor.bottom = _height;
  commandList->RSSetScissorRects(1, &scissor);

  commandList->SetGraphicsRootSignature(_rootSignature);
  commandList->SetGraphicsRootConstantBufferView(0, frameCBV);
  commandList->SetGraphicsRootConstantBufferView(1, structureCBVBase);

  drawAtomPick(commandList, structureCBVBase, structureCBVStride);
  drawBondPick(commandList, structureCBVBase, structureCBVStride, _bondPso.Get(), _bondPsoReady, true);
  drawBondPick(commandList, structureCBVBase, structureCBVStride, _externalBondPso.Get(),
               _externalBondPsoReady, false);
  drawObjectPick(commandList, structureCBVBase, structureCBVStride);
  drawRibbonPick(commandList, structureCBVBase, structureCBVStride);

  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _pickColor.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    _pickColorState = D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
}

void DirectXPickingShader::paintPickPass(ID3D12GraphicsCommandList *commandList,
                                         D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                                         D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                         UINT structureCBVStride)
{
  drawPickContents(commandList, frameCBV, structureCBVBase, structureCBVStride);
}

std::array<int, 4> DirectXPickingShader::pickTexture(D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
                                                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                     UINT structureCBVStride,
                                                     int x, int y, int width, int height)
{
  std::array<int, 4> pixel{0, 0, 0, 0};
  if (!_device || !_commandQueue || !_pickAllocator || !_pickCommandList || !_readbackBuffer)
    return pixel;

  createPickTargets(_device, width, height);
  if (!_pickColor || !_pickDepth)
    return pixel;

  const int clampedX = std::clamp(x, 0, _width - 1);
  // D3D / WinUI Y origin is top-left (same as mouse). Do not apply OpenGL's height-y flip.
  const int clampedY = std::clamp(y, 0, _height - 1);

  _pickAllocator->Reset();
  _pickCommandList->Reset(_pickAllocator.Get(), nullptr);

  // After prior paintPickPass, color may already be COPY_SOURCE; reset tracking for this dedicated pass.
  if (_pickColorState == D3D12_RESOURCE_STATE_COPY_SOURCE)
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = _pickColor.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _pickCommandList->ResourceBarrier(1, &barrier);
    _pickColorState = D3D12_RESOURCE_STATE_COMMON;
  }

  drawPickContents(_pickCommandList.Get(), frameCBV, structureCBVBase, structureCBVStride);

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = _pickColor.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = _readbackBuffer.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Offset = 0;
  dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  dst.PlacedFootprint.Footprint.Width = 1;
  dst.PlacedFootprint.Footprint.Height = 1;
  dst.PlacedFootprint.Footprint.Depth = 1;
  dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

  D3D12_BOX box = {};
  box.left = static_cast<UINT>(clampedX);
  box.top = static_cast<UINT>(clampedY);
  box.front = 0;
  box.right = static_cast<UINT>(clampedX + 1);
  box.bottom = static_cast<UINT>(clampedY + 1);
  box.back = 1;

  _pickCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
  _pickCommandList->Close();

  ID3D12CommandList *lists[] = { _pickCommandList.Get() };
  _commandQueue->ExecuteCommandLists(1, lists);
  waitForPickGPU();

  void *mapped = nullptr;
  D3D12_RANGE readRange = { 0, sizeof(uint32_t) * 4 };
  if (SUCCEEDED(_readbackBuffer->Map(0, &readRange, &mapped)) && mapped)
  {
    uint32_t values[4] = {};
    std::memcpy(values, mapped, sizeof(values));
    _readbackBuffer->Unmap(0, nullptr);
    pixel = { static_cast<int>(values[0]), static_cast<int>(values[1]),
              static_cast<int>(values[2]), static_cast<int>(values[3]) };
  }
  return pixel;
}

namespace
{
// The identifier of the atom rides along with the geometry the scene pass rasterizes, so the
// silhouette and the depth written here are the same surface the user sees and clicks on.
const char *kAtomPickVaryings = R"foo(
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  float2 texcoords : TEXCOORD1;
  float3 frag_pos : TEXCOORD2;
  nointerpolation float3 frag_center : TEXCOORD3;
  nointerpolation float4 sphere_radius : TEXCOORD4;
  nointerpolation int instanceId : TEXCOORD5;
)foo";
} // namespace

std::string DirectXPickingShader::atomVertexShaderSource(bool orthographic)
{
  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
  int instanceTag : INSTANCETAG;
};

struct VSOutput
{
)foo") + kAtomPickVaryings + std::string(R"foo(};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.instanceId = input.instanceTag;

  float4 scale = structureUniforms.atomScaleFactor * input.instanceScale;
  output.eye_position = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.frag_center = output.eye_position.xyz;
  output.texcoords = input.vertexPosition.xy;
  output.sphere_radius = scale;

  float4 pos2 = output.eye_position;
  pos2.xy += )foo") + (orthographic ? "" : "1.5 * ") + std::string(R"foo(scale.xy * input.vertexPosition.xy;
  output.frag_pos = pos2.xyz;

  float4 clip = mul(frameUniforms.projectionMatrix, pos2);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");
}

std::string DirectXPickingShader::atomPixelShaderSource(bool orthographic)
{
  const std::string hit = orthographic ? R"foo(
  float x = input.texcoords.x;
  float y = input.texcoords.y;
  float zz = 1.0 - x * x - y * y;
  if (zz <= 0.0)
    discard;

  float4 pos = input.eye_position;
  pos.z += input.sphere_radius.z * sqrt(zz);
  pos = mul(frameUniforms.projectionMatrix, pos);
)foo" : R"foo(
  float3 rij = -input.frag_center;
  float3 vij = input.frag_pos;

  float A = dot(vij, vij);
  float B = dot(rij, vij);
  float C = dot(rij, rij) - input.sphere_radius.z * input.sphere_radius.z;
  float argument = B * B - A * C;
  if (argument < 0.0)
    discard;

  float3 hit = (-C / (B - sqrt(argument))) * vij;
  float4 pos = mul(frameUniforms.projectionMatrix, float4(hit, 1.0));
)foo";

  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
)foo") + kAtomPickVaryings + std::string(R"foo(};

struct PSOutput
{
  uint4 color : SV_TARGET;
  float depth : SV_Depth;
};

PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + hit + std::string(R"foo(
  output.depth = 0.5 * (pos.z / pos.w) + 0.5;
  output.color = uint4(1, structureUniforms.sceneIdentifier, structureUniforms.MovieIdentifier, input.instanceId);
  return output;
}
)foo");
}

const std::string DirectXPickingShader::_bondVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
)foo") + DirectXBondImposter::HullVertexInputStringLiteral + std::string(R"foo(
  int instanceTag : INSTANCETAG;
};

struct VSOutput
{
)foo") + DirectXBondImposter::PickingVaryingsStringLiteral + std::string(R"foo(
};
)foo") + DirectXBondImposter::HullStringLiteral + std::string(R"foo(
VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.instanceId = input.instanceTag;

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  BondImposterHull hull = bondImposterHull(pos1, pos2, input.vertexPosition.xyz,
                                           input.vertexNormal.xy, input.vertexPosition.w, 1.0);
  output.frag_pos = hull.posEye;
  output.pointA = hull.a;
  output.pointB = hull.b;
  output.radius = hull.radius;

  float4 clip = mul(frameUniforms.projectionMatrix, float4(hull.posEye, 1.0));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;

  // Bond visibility is encoded in position.w (±1); collapse the whole hull.
  if (pos1.w < 0.0 || pos2.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
  }
  return output;
}
)foo");

const std::string DirectXPickingShader::_bondPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
)foo") + DirectXBondImposter::PickingVaryingsStringLiteral + std::string(R"foo(
};
)foo") + DirectXBondImposter::PickingOutputStringLiteral + DirectXBondImposter::IntersectStringLiteral +
std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXBondImposter::RayStringLiteral + std::string(R"foo(
  float3 N;
  float ct;
  float t = cylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius, N, ct);
  if (t < 0.0) discard;

  float3 pos = ro + t * rd;
)foo") + DirectXBondImposter::WriteDepthStringLiteral + std::string(R"foo(
  output.color = uint4(2, structureUniforms.sceneIdentifier, structureUniforms.MovieIdentifier, input.instanceId);
  return output;
}
)foo");

const std::string DirectXPickingShader::_externalBondPixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
)foo") + DirectXBondImposter::PickingVaryingsStringLiteral + std::string(R"foo(
};
)foo") + DirectXBondImposter::PickingOutputStringLiteral + DirectXBondImposter::ClippedIntersectStringLiteral +
std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXBondImposter::RayStringLiteral + DirectXBondImposter::ToStructureStringLiteral +
std::string(R"foo(
  float3 N;
  float ct;
  float t = clippedCylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius, toStructure, N, ct);
  if (t < 0.0) discard;

  float3 pos = ro + t * rd;
)foo") + DirectXBondImposter::WriteDepthStringLiteral + std::string(R"foo(
  output.color = uint4(2, structureUniforms.sceneIdentifier, structureUniforms.MovieIdentifier, input.instanceId);
  return output;
}
)foo");

const std::string DirectXPickingShader::_objectVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  int instanceTag : INSTANCETAG;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation int instanceId : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.instanceId = input.instanceTag;
  float4 pos = input.instancePosition
               + mul(structureUniforms.transformationMatrix, input.vertexPosition);
  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXPickingShader::_objectPixelShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation int instanceId : TEXCOORD0;
};

uint4 PSMain(PSInput input) : SV_TARGET
{
  return uint4(1, structureUniforms.sceneIdentifier, structureUniforms.MovieIdentifier, input.instanceId);
}
)foo");

const std::string DirectXPickingShader::_ribbonVertexShaderSource =
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
  nointerpolation int segmentIndex : TEXCOORD0;
  nointerpolation int residueIndex : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, input.vertexPosition));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  output.segmentIndex = int(input.vertexNormal.w);
  output.residueIndex = int(input.vertexPad.y);
  return output;
}
)foo");

// A ribbon pick needs five numbers where the target holds four, so the scene and movie share one
// channel and the segment and residue keep their own. Atoms and bonds are left untouched.
const std::string DirectXPickingShader::_ribbonPixelShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation int segmentIndex : TEXCOORD0;
  nointerpolation int residueIndex : TEXCOORD1;
};

uint4 PSMain(PSInput input) : SV_TARGET
{
  uint scene = uint(structureUniforms.sceneIdentifier) & 0xFFFFu;
  uint movie = uint(structureUniforms.MovieIdentifier) & 0xFFFFu;
  return uint4(3, (scene << 16) | movie, uint(input.segmentIndex), uint(input.residueIndex));
}
)foo");
