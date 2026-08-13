/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcomputevoidfraction.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
void createBufferSrv(ID3D12Device *device, ID3D12Resource *resource, UINT numElements, UINT stride,
                     D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = std::max(1u, numElements);
  srv.Buffer.StructureByteStride = stride;
  device->CreateShaderResourceView(resource, &srv, handle);
}

void createBufferUav(ID3D12Device *device, ID3D12Resource *resource, UINT numElements, UINT stride,
                     D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = std::max(1u, numElements);
  uav.Buffer.StructureByteStride = stride;
  device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
}
} // namespace

SKComputeVoidFraction::SKComputeVoidFraction()
{
  if (!_isDx12Initialized)
    return;

  ComPtr<ID3DBlob> cs = compileComputeShader(_voidFractionKernel, "ComputeVoidFraction");
  if (!cs)
    return;

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 2;
  params[0].DescriptorTable.pDescriptorRanges = ranges;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants.Num32BitValues = 2; // inputCount, pad
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;

  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError)))
  {
    std::cerr << "SKComputeVoidFraction: root signature serialize failed";
    return;
  }
  if (FAILED(_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                          IID_PPV_ARGS(&_rootSignature))))
    return;

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = _rootSignature.Get();
  psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
  if (FAILED(_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
    return;

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 2;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_descriptorHeap))))
    return;
  _descriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  _isDx12Ready = true;
}

double SKComputeVoidFraction::ComputeVoidFraction(std::vector<float> *voxels)
{
  if (!voxels || voxels->empty())
    return 0.0;
  if (getInstance()._isDx12Ready)
  {
    try
    {
      return getInstance().computeVoidFractionGPU(voxels);
    }
    catch (const std::exception &ex)
    {
      std::cerr << "SKComputeVoidFraction GPU failed, falling back to CPU:" << ex.what();
    }
  }
  return computeVoidFractionCPU(voxels);
}

double SKComputeVoidFraction::computeVoidFractionCPU(std::vector<float> *voxels)
{
  double fraction = 0.0;
  for (float value : *voxels)
    fraction += std::exp(-(1.0 / 298.0) * double(value));
  return fraction / (128.0 * 128.0 * 128.0);
}

double SKComputeVoidFraction::computeVoidFractionGPU(std::vector<float> *voxels)
{
  std::lock_guard<std::mutex> lock(_gpuMutex);

  const size_t inputCount = voxels->size();
  const size_t numberOfGridPoints =
      (inputCount + kThreadGroupSize - 1) & ~(static_cast<size_t>(kThreadGroupSize) - 1);
  const size_t nWorkGroups = numberOfGridPoints / kThreadGroupSize;

  std::vector<float> paddedInput(numberOfGridPoints, 0.0f);
  std::memcpy(paddedInput.data(), voxels->data(), inputCount * sizeof(float));
  // Zero-pad energies beyond input so exp(0)=1 would inflate the sum — use a large energy instead.
  for (size_t i = inputCount; i < numberOfGridPoints; ++i)
    paddedInput[i] = 1.0e7f;

  std::vector<float> partialSums(nWorkGroups, 0.0f);

  ComPtr<ID3D12Resource> inputBuf = uploadToDefaultBuffer(
      paddedInput.data(), paddedInput.size() * sizeof(float), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ComPtr<ID3D12Resource> partialBuf = uploadToDefaultBuffer(
      partialSums.data(), partialSums.size() * sizeof(float), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!inputBuf || !partialBuf)
    throw std::runtime_error("SKComputeVoidFraction: buffer creation failed");

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  createBufferSrv(_device.Get(), inputBuf.Get(), static_cast<UINT>(numberOfGridPoints), sizeof(float), cpu);
  cpu.ptr += _descriptorSize;
  createBufferUav(_device.Get(), partialBuf.Get(), static_cast<UINT>(nWorkGroups), sizeof(float), cpu);

  int constants[2] = { static_cast<int>(inputCount), 0 };

  resetCommandList();
  _commandList->SetPipelineState(_pso.Get());
  _commandList->SetComputeRootSignature(_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _descriptorHeap.Get() };
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetComputeRootDescriptorTable(0, _descriptorHeap->GetGPUDescriptorHandleForHeapStart());
  _commandList->SetComputeRoot32BitConstants(1, 2, constants, 0);
  _commandList->Dispatch(static_cast<UINT>(nWorkGroups), 1, 1);
  executeAndWait();

  readbackBuffer(partialBuf.Get(), partialSums.data(), partialSums.size() * sizeof(float),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  double fraction = 0.0;
  for (float v : partialSums)
    fraction += double(v);
  return fraction / (128.0 * 128.0 * 128.0);
}

const std::string SKComputeVoidFraction::_voidFractionKernel = R"foo(
cbuffer Constants : register(b0)
{
  int inputCount;
  int pad;
};

StructuredBuffer<float> inputEnergy : register(t0);
RWStructuredBuffer<float> partialSums : register(u0);

groupshared float localSums[256];

[numthreads(256, 1, 1)]
void ComputeVoidFraction(uint3 dtid : SV_DispatchThreadID, uint gidx : SV_GroupIndex, uint3 gid : SV_GroupID)
{
  uint global_id = dtid.x;
  float value = 0.0f;
  if (global_id < (uint)inputCount)
    value = exp(-(1.0f / 298.0f) * inputEnergy[global_id]);
  localSums[gidx] = value;

  GroupMemoryBarrierWithGroupSync();
  for (uint stride = 256 / 2; stride > 0; stride >>= 1)
  {
    if (gidx < stride)
      localSums[gidx] += localSums[gidx + stride];
    GroupMemoryBarrierWithGroupSync();
  }

  if (gidx == 0)
    partialSums[gid.x] = localSums[0];
}
)foo";
