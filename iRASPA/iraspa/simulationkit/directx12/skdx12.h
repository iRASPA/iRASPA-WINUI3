/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

class SKDx12
{
public:
  SKDx12();
  virtual ~SKDx12();

protected:
  bool _isDx12Initialized = false;
  bool _isDx12Ready = false;

  ComPtr<ID3D12Device> _device;
  ComPtr<ID3D12CommandQueue> _commandQueue;
  ComPtr<ID3D12CommandAllocator> _commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> _commandList;
  ComPtr<ID3D12Fence> _fence;
  HANDLE _fenceEvent = nullptr;
  UINT64 _fenceValue = 0;
  std::mutex _gpuMutex;

  static ComPtr<ID3DBlob> compileComputeShader(const std::string &source, const char *entryPoint,
                                               const D3D_SHADER_MACRO *defines = nullptr);
  static ComPtr<ID3D12Resource> createUploadBuffer(ID3D12Device *device, UINT64 sizeInBytes);
  static ComPtr<ID3D12Resource> createDefaultBuffer(ID3D12Device *device, UINT64 sizeInBytes,
                                                    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
  static ComPtr<ID3D12Resource> createReadbackBuffer(ID3D12Device *device, UINT64 sizeInBytes);
  static void writeUploadBuffer(ID3D12Resource *buffer, const void *data, size_t size);

  void resetCommandList();
  void executeAndWait();
  void waitForGpu();

  // Immediate upload (own command list + fence). Prefer recordUpload + executeAndWait for batches.
  ComPtr<ID3D12Resource> uploadToDefaultBuffer(const void *data, UINT64 size,
                                               D3D12_RESOURCE_STATES finalState);

  // Record copy into the open command list. Keep returned uploadAlive until after executeAndWait.
  struct StagedUpload
  {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Resource> uploadAlive;
  };
  StagedUpload recordUpload(const void *data, UINT64 size, D3D12_RESOURCE_STATES finalState);

  void readbackBuffer(ID3D12Resource *defaultBuffer, void *dst, UINT64 size,
                      D3D12_RESOURCE_STATES currentState);
};
