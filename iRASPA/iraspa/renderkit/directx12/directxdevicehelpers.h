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
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace DirectXDeviceHelpers
{
  inline UINT &sceneSampleCountStorage()
  {
    static UINT count = 1;
    return count;
  }

  inline UINT sceneSampleCount() { return sceneSampleCountStorage(); }
  inline void setSceneSampleCount(UINT count) { sceneSampleCountStorage() = (std::max)(1u, count); }

  inline DXGI_SAMPLE_DESC sceneSampleDesc()
  {
    DXGI_SAMPLE_DESC desc = {};
    desc.Count = sceneSampleCount();
    desc.Quality = 0;
    return desc;
  }

  // Shade the ray-traced imposters per-sample under MSAA, anti-aliasing their silhouettes,
  // clipping and depth. Cleared for a "fast" quality mode that shades once per pixel, in which
  // case MSAA only smooths the hull edges and not the ray-traced ones. The renderer sets this
  // once per frame from the render quality, so every imposter pass of a frame agrees.
  inline bool &perSampleImposterShadingStorage()
  {
    static bool enabled = true;
    return enabled;
  }

  inline bool perSampleImposterShading() { return perSampleImposterShadingStorage(); }
  inline void setPerSampleImposterShading(bool enabled) { perSampleImposterShadingStorage() = enabled; }

  inline uint32_t alignedCBSize(uint32_t size)
  {
    return (size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)
           & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
  }

  inline ComPtr<ID3D12Resource> createUploadBuffer(ID3D12Device *device, uint64_t sizeInBytes)
  {
    ComPtr<ID3D12Resource> buffer;
    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&buffer))))
    {
      return nullptr;
    }
    return buffer;
  }

  inline ComPtr<ID3D12Resource> createDefaultBuffer(ID3D12Device *device, uint64_t sizeInBytes,
                                                    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON)
  {
    ComPtr<ID3D12Resource> buffer;
    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &desc,
                                               initialState, nullptr, IID_PPV_ARGS(&buffer))))
    {
      return nullptr;
    }
    return buffer;
  }

  inline void writeUploadBuffer(ID3D12Resource *buffer, const void *data, size_t size)
  {
    if (!buffer || !data || size == 0)
      return;
    void *mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(buffer->Map(0, &readRange, &mapped)))
    {
      std::memcpy(mapped, data, size);
      buffer->Unmap(0, nullptr);
    }
  }

  // One copy of a static VB/IB, shared by every structure (Cocoa: one sphere, one quad, one cylinder).
  struct IndexedMesh
  {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
  };

  inline bool uploadIndexedMesh(ID3D12Device *device, IndexedMesh &mesh,
                                const void *vertices, size_t vertexBytes, UINT vertexStride,
                                const void *indices, size_t indexBytes)
  {
    mesh = IndexedMesh{};
    if (!device || !vertices || vertexBytes == 0 || !indices || indexBytes == 0)
      return false;
    mesh.vertexBuffer = createUploadBuffer(device, vertexBytes);
    mesh.indexBuffer = createUploadBuffer(device, indexBytes);
    if (!mesh.vertexBuffer || !mesh.indexBuffer)
      return false;
    writeUploadBuffer(mesh.vertexBuffer.Get(), vertices, vertexBytes);
    writeUploadBuffer(mesh.indexBuffer.Get(), indices, indexBytes);
    mesh.vbv = { mesh.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), vertexStride };
    mesh.ibv = { mesh.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes), DXGI_FORMAT_R16_UINT };
    mesh.indexCount = static_cast<UINT>(indexBytes / sizeof(short));
    return true;
  }

  inline bool uploadInstanceBuffer(ID3D12Device *device,
                                   ComPtr<ID3D12Resource> &buffer,
                                   D3D12_VERTEX_BUFFER_VIEW &vbv,
                                   UINT &instanceCount,
                                   const void *data, size_t count, UINT stride)
  {
    instanceCount = static_cast<UINT>(count);
    const size_t bytes = std::max<size_t>(count * stride, 1);
    buffer = createUploadBuffer(device, bytes);
    if (!buffer)
      return false;
    if (count > 0 && data)
      writeUploadBuffer(buffer.Get(), data, count * stride);
    vbv = { buffer->GetGPUVirtualAddress(), static_cast<UINT>(bytes), stride };
    return true;
  }

  // Root signature: b0 frame, b1 structure, b3 lights, t0 SRV, b2 isosurface, b5 globalAxes
  // Root indices 0–4 keep existing SetGraphicsRoot* call sites; globalAxes is Root5 = register b5.
  inline ComPtr<ID3D12RootSignature> createSceneRootSignature(ID3D12Device *device)
  {
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[6] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 3;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &srvRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[4].Descriptor.ShaderRegister = 2;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[5].Descriptor.ShaderRegister = 5;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 6;
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
    {
      if (error)
      {
        OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
      }
      return nullptr;
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature))))
    {
      return nullptr;
    }
    return rootSignature;
  }
}
