/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

/// Linear allocator over one shader-visible CBV/SRV/UAV heap.
///
/// The ray-tracing passes reach their acceleration structure, primitive buffers and per-pixel
/// buffers through descriptor tables, which can only be filled from a shader-visible heap. The
/// raster passes have no such heap to share: they bind their constant buffers as root views and
/// their one texture from a heap of a single descriptor. So the tracer keeps its own, rewritten
/// from scratch each time it encodes, which is why allocation is a bump pointer and freeing is
/// reset() rather than anything per-descriptor.
///
/// Descriptors handed out stay valid until the next reset, so a reset must not happen while the
/// GPU is still reading them. The tracer resets once per frame, before it encodes.
class DirectXShaderVisibleHeap
{
public:
  DirectXShaderVisibleHeap() = default;

  bool create(ID3D12Device *device, UINT descriptorCount)
  {
    release();
    if (!device || descriptorCount == 0)
      return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = descriptorCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap))))
      return false;

    m_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_capacity = descriptorCount;
    m_used = 0;
    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    return true;
  }

  void release()
  {
    m_heap.Reset();
    m_capacity = 0;
    m_used = 0;
    m_stride = 0;
    m_cpuStart = {};
    m_gpuStart = {};
  }

  /// Hands the whole heap back out. Only safe once the GPU is done with the descriptors of
  /// the previous encode.
  void reset() { m_used = 0; }

  bool isValid() const { return m_heap != nullptr; }
  ID3D12DescriptorHeap *heap() const { return m_heap.Get(); }
  UINT capacity() const { return m_capacity; }
  UINT used() const { return m_used; }

  /// Reserves \a count adjacent descriptors, which is what a descriptor table spanning a
  /// range needs. \a cpuStart is where the views are written, \a gpuStart what the table is
  /// bound to. False when the heap has no room left, and nothing is reserved.
  bool allocateRange(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE &cpuStart, D3D12_GPU_DESCRIPTOR_HANDLE &gpuStart)
  {
    if (!m_heap || count == 0 || m_used + count > m_capacity)
      return false;

    cpuStart.ptr = m_cpuStart.ptr + static_cast<SIZE_T>(m_used) * m_stride;
    gpuStart.ptr = m_gpuStart.ptr + static_cast<UINT64>(m_used) * m_stride;
    m_used += count;
    return true;
  }

  /// The handle \a index descriptors into a range that started at \a cpuStart.
  D3D12_CPU_DESCRIPTOR_HANDLE offsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuStart, UINT index) const
  {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_stride;
    return handle;
  }

private:
  ComPtr<ID3D12DescriptorHeap> m_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
  UINT m_capacity = 0;
  UINT m_used = 0;
  UINT m_stride = 0;
};
