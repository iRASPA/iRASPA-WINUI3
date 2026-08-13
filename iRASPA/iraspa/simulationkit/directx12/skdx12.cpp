/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skdx12.h"
#include <iostream>
#include <algorithm>
#include <cstring>

SKDx12::SKDx12()
{
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
  {
    std::cerr << "SKDx12: CreateDXGIFactory1 failed";
    return;
  }

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIAdapter1> bestAdapter;
  SIZE_T bestVideoMemory = 0;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
  {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;
    if (desc.DedicatedVideoMemory > bestVideoMemory)
    {
      bestVideoMemory = desc.DedicatedVideoMemory;
      bestAdapter = adapter;
    }
  }

  if (!bestAdapter)
  {
    if (FAILED(factory->EnumAdapters1(0, &bestAdapter)))
    {
      std::cerr << "SKDx12: no DXGI adapter";
      return;
    }
  }

  if (FAILED(D3D12CreateDevice(bestAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device))))
  {
    std::cerr << "SKDx12: D3D12CreateDevice failed";
    return;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  if (FAILED(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_commandQueue))))
  {
    std::cerr << "SKDx12: CreateCommandQueue failed";
    return;
  }

  if (FAILED(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&_commandAllocator))))
  {
    std::cerr << "SKDx12: CreateCommandAllocator failed";
    return;
  }

  if (FAILED(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, _commandAllocator.Get(),
                                         nullptr, IID_PPV_ARGS(&_commandList))))
  {
    std::cerr << "SKDx12: CreateCommandList failed";
    return;
  }
  _commandList->Close();

  if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
  {
    std::cerr << "SKDx12: CreateFence failed";
    return;
  }
  _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!_fenceEvent)
  {
    std::cerr << "SKDx12: CreateEvent failed";
    return;
  }

  _isDx12Initialized = true;
}

SKDx12::~SKDx12()
{
  if (_fenceEvent)
  {
    if (_isDx12Initialized)
      waitForGpu();
    CloseHandle(_fenceEvent);
    _fenceEvent = nullptr;
  }
}

ComPtr<ID3DBlob> SKDx12::compileComputeShader(const std::string &source, const char *entryPoint,
                                              const D3D_SHADER_MACRO *defines)
{
  ComPtr<ID3DBlob> shaderBlob;
  ComPtr<ID3DBlob> errorBlob;
  HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, defines, nullptr,
                          entryPoint, "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                          &shaderBlob, &errorBlob);
  if (FAILED(hr))
  {
    if (errorBlob)
      std::cerr << "SKDx12 CS compile failed:" << static_cast<const char *>(errorBlob->GetBufferPointer());
    else
      std::cerr << "SKDx12 CS compile failed for" << entryPoint;
    return nullptr;
  }
  if (errorBlob && errorBlob->GetBufferSize() > 1)
    std::cerr << "SKDx12 CS warnings:" << static_cast<const char *>(errorBlob->GetBufferPointer());
  return shaderBlob;
}

ComPtr<ID3D12Resource> SKDx12::createUploadBuffer(ID3D12Device *device, UINT64 sizeInBytes)
{
  ComPtr<ID3D12Resource> buffer;
  D3D12_HEAP_PROPERTIES heapProp{};
  heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = std::max<UINT64>(sizeInBytes, 1);
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&buffer))))
    return nullptr;
  return buffer;
}

ComPtr<ID3D12Resource> SKDx12::createDefaultBuffer(ID3D12Device *device, UINT64 sizeInBytes,
                                                   D3D12_RESOURCE_STATES initialState)
{
  ComPtr<ID3D12Resource> buffer;
  D3D12_HEAP_PROPERTIES heapProp{};
  heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = std::max<UINT64>(sizeInBytes, 1);
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                             initialState, nullptr, IID_PPV_ARGS(&buffer))))
    return nullptr;
  return buffer;
}

ComPtr<ID3D12Resource> SKDx12::createReadbackBuffer(ID3D12Device *device, UINT64 sizeInBytes)
{
  ComPtr<ID3D12Resource> buffer;
  D3D12_HEAP_PROPERTIES heapProp{};
  heapProp.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = std::max<UINT64>(sizeInBytes, 1);
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&buffer))))
    return nullptr;
  return buffer;
}

void SKDx12::writeUploadBuffer(ID3D12Resource *buffer, const void *data, size_t size)
{
  if (!buffer || !data || size == 0)
    return;
  void *mapped = nullptr;
  D3D12_RANGE readRange{0, 0};
  if (SUCCEEDED(buffer->Map(0, &readRange, &mapped)))
  {
    std::memcpy(mapped, data, size);
    buffer->Unmap(0, nullptr);
  }
}

void SKDx12::resetCommandList()
{
  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);
}

void SKDx12::executeAndWait()
{
  _commandList->Close();
  ID3D12CommandList *lists[] = { _commandList.Get() };
  _commandQueue->ExecuteCommandLists(1, lists);
  waitForGpu();
}

void SKDx12::waitForGpu()
{
  const UINT64 fence = ++_fenceValue;
  if (FAILED(_commandQueue->Signal(_fence.Get(), fence)))
    return;
  if (_fence->GetCompletedValue() < fence)
  {
    _fence->SetEventOnCompletion(fence, _fenceEvent);
    WaitForSingleObject(_fenceEvent, INFINITE);
  }
}

SKDx12::StagedUpload SKDx12::recordUpload(const void *data, UINT64 size,
                                          D3D12_RESOURCE_STATES finalState)
{
  StagedUpload staged{};
  staged.uploadAlive = createUploadBuffer(_device.Get(), size);
  staged.resource = createDefaultBuffer(_device.Get(), size, D3D12_RESOURCE_STATE_COPY_DEST);
  if (!staged.uploadAlive || !staged.resource)
    return {};
  writeUploadBuffer(staged.uploadAlive.Get(), data, static_cast<size_t>(size));
  _commandList->CopyBufferRegion(staged.resource.Get(), 0, staged.uploadAlive.Get(), 0, size);
  if (finalState != D3D12_RESOURCE_STATE_COPY_DEST)
  {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = staged.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);
  }
  return staged;
}

ComPtr<ID3D12Resource> SKDx12::uploadToDefaultBuffer(const void *data, UINT64 size,
                                                     D3D12_RESOURCE_STATES finalState)
{
  resetCommandList();
  StagedUpload staged = recordUpload(data, size, finalState);
  if (!staged.resource)
    return nullptr;
  executeAndWait();
  return staged.resource;
}

void SKDx12::readbackBuffer(ID3D12Resource *defaultBuffer, void *dst, UINT64 size,
                            D3D12_RESOURCE_STATES currentState)
{
  ComPtr<ID3D12Resource> readback = createReadbackBuffer(_device.Get(), size);
  if (!readback || !defaultBuffer || !dst)
    return;

  resetCommandList();
  if (currentState != D3D12_RESOURCE_STATE_COPY_SOURCE)
  {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = defaultBuffer;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &barrier);
  }
  _commandList->CopyBufferRegion(readback.Get(), 0, defaultBuffer, 0, size);
  executeAndWait();

  void *mapped = nullptr;
  D3D12_RANGE range{0, static_cast<SIZE_T>(size)};
  if (SUCCEEDED(readback->Map(0, &range, &mapped)))
  {
    std::memcpy(dst, mapped, static_cast<size_t>(size));
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
  }
}
