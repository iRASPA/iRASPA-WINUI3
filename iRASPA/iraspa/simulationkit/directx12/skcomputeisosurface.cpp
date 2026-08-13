/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcomputeisosurface.h"
#include <iostream>
#include "marchingcubes.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
struct IsoConstants
{
  int dimensions[4];
  float isolevel;
  int sumTriangles;
  int gridSize;
  int levelSize;
};

void createBufSrv(ID3D12Device *device, ID3D12Resource *res, UINT n, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = std::max(1u, n);
  srv.Buffer.StructureByteStride = stride;
  device->CreateShaderResourceView(res, &srv, h);
}

void createBufUav(ID3D12Device *device, ID3D12Resource *res, UINT n, UINT stride, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = std::max(1u, n);
  uav.Buffer.StructureByteStride = stride;
  device->CreateUnorderedAccessView(res, nullptr, &uav, h);
}

UINT dispatchAxis(UINT count, UINT group)
{
  return (count + group - 1) / group;
}
} // namespace

bool SKComputeIsosurface::createPso(const char *entry, ComPtr<ID3D12RootSignature> &rs,
                                    ComPtr<ID3D12PipelineState> &pso,
                                    const D3D12_ROOT_PARAMETER *params, UINT numParams)
{
  ComPtr<ID3DBlob> cs = compileComputeShader(_marchingCubesKernel, entry);
  if (!cs)
    return false;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = numParams;
  rsDesc.pParameters = params;
  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError)))
  {
    std::cerr << "SKComputeIsosurface: RS serialize failed for" << entry;
    return false;
  }
  if (FAILED(_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&rs))))
    return false;

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = rs.Get();
  psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
  if (FAILED(_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "SKComputeIsosurface: PSO failed for" << entry;
    return false;
  }
  return true;
}

SKComputeIsosurface::SKComputeIsosurface()
{
  if (!_isDx12Initialized)
    return;

  // classify: table(t0,u0) + constants
  {
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
    params[1].Constants.Num32BitValues = sizeof(IsoConstants) / 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    if (!createPso("classifyCubes", _classifyRs, _classifyPso, params, 2))
      return;
  }

  // constructFromBase: t1 + u3 + constants
  {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 1;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 3;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = sizeof(IsoConstants) / 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    if (!createPso("constructHPLevelFromBase", _constructBaseRs, _constructBasePso, params, 2))
      return;
  }

  // construct: t2 + u3 + constants
  {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 2;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 3;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = sizeof(IsoConstants) / 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    if (!createPso("constructHPLevel", _constructRs, _constructPso, params, 2))
      return;
  }

  // traverse: t0 + t10..t18 (9) + u2 — compile only the root signature here;
  // traverse PSOs are created lazily for the needed grid size (avoids 6× D3DCompile of the huge MC kernel).
  {
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 9;
    ranges[1].BaseShaderRegister = 10;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 2;
    ranges[2].OffsetInDescriptorsFromTableStart = 10;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 3;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.Num32BitValues = sizeof(IsoConstants) / 4;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = params;
    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsError;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError)))
      return;
    if (FAILED(_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&_traverseRs))))
      return;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 64;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_heap))))
    return;
  _descriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  _isDx12Ready = true;
  std::cerr << "SKComputeIsosurface: DX12 ready (traverse PSOs compiled on demand)\n";
}

bool SKComputeIsosurface::ensureTraversePso(int powerOfTwo)
{
  if (powerOfTwo < 4 || powerOfTwo > 9)
    return false;
  if (_traversePso[powerOfTwo])
    return true;

  const char *entries[] = { nullptr, nullptr, nullptr, nullptr,
                            "traverseHP16", "traverseHP32", "traverseHP64",
                            "traverseHP128", "traverseHP256", "traverseHP512" };
  ComPtr<ID3DBlob> cs = compileComputeShader(_marchingCubesKernel, entries[powerOfTwo]);
  if (!cs)
    return false;
  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = _traverseRs.Get();
  psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
  if (FAILED(_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&_traversePso[powerOfTwo]))))
  {
    std::cerr << "SKComputeIsosurface: PSO failed for " << entries[powerOfTwo] << "\n";
    return false;
  }
  std::cerr << "SKComputeIsosurface: compiled " << entries[powerOfTwo] << "\n";
  return true;
}

std::vector<float4> SKComputeIsosurface::computeIsosurface(int3 dimensions, std::vector<float> *voxels, double isoValue)
{
  if (getInstance()._isDx12Ready && voxels && !voxels->empty())
  {
    try
    {
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<float4> result =
          getInstance().computeIsosurfaceGPUImplementation(dimensions, voxels, isoValue);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
      std::cerr << "SKComputeIsosurface: GPU " << dimensions.x << "^3 -> " << (result.size() / 9)
                << " tris in " << ms << " ms\n";
      return result;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "SKComputeIsosurface GPU failed, falling back to CPU: " << ex.what() << "\n";
    }
  }
  else
  {
    std::cerr << "SKComputeIsosurface: DX12 not ready, using CPU\n";
  }
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<float4> result = computeIsosurfaceCPUImplementation(dimensions, voxels, isoValue);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cerr << "SKComputeIsosurface: CPU " << dimensions.x << "^3 in " << ms << " ms\n";
  return result;
}

std::vector<float4> SKComputeIsosurface::computeIsosurfaceGPUImplementation(int3 dimensions, std::vector<float> *voxels,
                                                                            double isoValue)
{
  std::lock_guard<std::mutex> lock(_gpuMutex);

  int largestSize = std::max({dimensions.x, dimensions.y, dimensions.z});
  int powerOfTwo = 1;
  while (largestSize > (1 << powerOfTwo))
    ++powerOfTwo;
  if (powerOfTwo < 4 || powerOfTwo > 9)
    throw std::runtime_error("SKComputeIsosurface: unsupported grid size");
  if (!ensureTraversePso(powerOfTwo))
    throw std::runtime_error("SKComputeIsosurface: traverse PSO unavailable");

  const int size = 1 << powerOfTwo;
  const size_t volume = static_cast<size_t>(size) * size * size;

  if (voxels->size() < volume)
    throw std::runtime_error("SKComputeIsosurface: voxel buffer too small for padded size");

  IsoConstants constants{};
  constants.dimensions[0] = dimensions.x;
  constants.dimensions[1] = dimensions.y;
  constants.dimensions[2] = dimensions.z;
  constants.dimensions[3] = 1;
  constants.isolevel = static_cast<float>(isoValue);
  constants.sumTriangles = 0;
  constants.gridSize = size;
  constants.levelSize = size / 2;

  // Level buffers: images[0]=base uint4, then uint levels down to 2^3
  std::vector<ComPtr<ID3D12Resource>> levelBuffers;
  std::vector<int> levelSizes;
  levelSizes.push_back(size); // base

  ComPtr<ID3D12Resource> rawBuf = uploadToDefaultBuffer(
      voxels->data(), volume * sizeof(float), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ComPtr<ID3D12Resource> baseBuf = createDefaultBuffer(_device.Get(), volume * sizeof(UINT) * 4,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!rawBuf || !baseBuf)
    throw std::runtime_error("SKComputeIsosurface: buffer alloc failed");
  levelBuffers.push_back(baseBuf);

  int bufSize = size / 2;
  for (int i = 1; i < powerOfTwo; ++i)
  {
    const UINT64 bytes = static_cast<UINT64>(bufSize) * bufSize * bufSize * sizeof(UINT);
    ComPtr<ID3D12Resource> lvl = createDefaultBuffer(_device.Get(), bytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!lvl)
      throw std::runtime_error("SKComputeIsosurface: level buffer alloc failed");
    levelBuffers.push_back(lvl);
    levelSizes.push_back(bufSize);
    bufSize /= 2;
  }

  auto heapAt = [&](UINT index) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = _heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * _descriptorSize;
    return h;
  };
  auto gpuAt = [&](UINT index) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = _heap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(index) * _descriptorSize;
    return h;
  };

  // --- classify + construct HP levels in one submit ---
  // Unique consecutive SRV/UAV pairs per dispatch (D3D12 resolves tables at GPU execute time).
  createBufSrv(_device.Get(), rawBuf.Get(), static_cast<UINT>(volume), sizeof(float), heapAt(0));
  createBufUav(_device.Get(), baseBuf.Get(), static_cast<UINT>(volume), sizeof(UINT) * 4, heapAt(1));
  for (int i = 0; i < powerOfTwo - 1; ++i)
  {
    const UINT pair = static_cast<UINT>(2 + 2 * i);
    const int writeSize = levelSizes[i + 1];
    if (i == 0)
      createBufSrv(_device.Get(), levelBuffers[0].Get(), static_cast<UINT>(volume), sizeof(UINT) * 4, heapAt(pair));
    else
    {
      const int readSize = levelSizes[i];
      createBufSrv(_device.Get(), levelBuffers[i].Get(),
                   static_cast<UINT>(readSize * readSize * readSize), sizeof(UINT), heapAt(pair));
    }
    createBufUav(_device.Get(), levelBuffers[i + 1].Get(),
                 static_cast<UINT>(writeSize * writeSize * writeSize), sizeof(UINT), heapAt(pair + 1));
  }

  resetCommandList();
  ID3D12DescriptorHeap *heaps[] = { _heap.Get() };
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetPipelineState(_classifyPso.Get());
  _commandList->SetComputeRootSignature(_classifyRs.Get());
  _commandList->SetComputeRootDescriptorTable(0, gpuAt(0));
  _commandList->SetComputeRoot32BitConstants(1, sizeof(IsoConstants) / 4, &constants, 0);
  _commandList->Dispatch(dispatchAxis(size, 8), dispatchAxis(size, 8), dispatchAxis(size, 8));
  {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = baseBuf.Get();
    _commandList->ResourceBarrier(1, &b);
  }
  {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = baseBuf.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    _commandList->ResourceBarrier(1, &b);
  }

  for (int i = 0; i < powerOfTwo - 1; ++i)
  {
    const int writeSize = levelSizes[i + 1];
    constants.levelSize = writeSize;
    const UINT pair = static_cast<UINT>(2 + 2 * i);

    if (i == 0)
    {
      _commandList->SetPipelineState(_constructBasePso.Get());
      _commandList->SetComputeRootSignature(_constructBaseRs.Get());
    }
    else
    {
      _commandList->SetPipelineState(_constructPso.Get());
      _commandList->SetComputeRootSignature(_constructRs.Get());
    }
    _commandList->SetComputeRootDescriptorTable(0, gpuAt(pair));
    _commandList->SetComputeRoot32BitConstants(1, sizeof(IsoConstants) / 4, &constants, 0);
    _commandList->Dispatch(dispatchAxis(writeSize, 8), dispatchAxis(writeSize, 8), dispatchAxis(writeSize, 8));
    {
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      b.UAV.pResource = levelBuffers[i + 1].Get();
      _commandList->ResourceBarrier(1, &b);
    }
    {
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = levelBuffers[i + 1].Get();
      b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      _commandList->ResourceBarrier(1, &b);
    }
  }
  executeAndWait();

  // Read top level 2x2x2
  const int topSize = levelSizes.back();
  std::vector<UINT> topData(static_cast<size_t>(topSize) * topSize * topSize, 0);
  readbackBuffer(levelBuffers.back().Get(), topData.data(), topData.size() * sizeof(UINT),
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  int numberOfTriangles = 0;
  for (UINT v : topData)
    numberOfTriangles += static_cast<int>(v);

  std::vector<float4> triangleData(static_cast<size_t>(numberOfTriangles) * 3 * 3);
  if (numberOfTriangles == 0)
    return triangleData;

  constants.sumTriangles = numberOfTriangles;

  // Allocate VBO as UAV only (no multi-MB zero upload).
  ComPtr<ID3D12Resource> vboBuf = createDefaultBuffer(
      _device.Get(), triangleData.size() * sizeof(float4), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!vboBuf)
    throw std::runtime_error("SKComputeIsosurface: VBO alloc failed");

  // Traverse descriptors: [0]=raw, [1..9]=hp0..hp8, [10]=vbo
  createBufSrv(_device.Get(), rawBuf.Get(), static_cast<UINT>(volume), sizeof(float), heapAt(0));
  createBufSrv(_device.Get(), levelBuffers[0].Get(), static_cast<UINT>(volume), sizeof(UINT) * 4, heapAt(1));
  for (int i = 1; i < powerOfTwo; ++i)
  {
    const int s = levelSizes[i];
    createBufSrv(_device.Get(), levelBuffers[i].Get(), static_cast<UINT>(s * s * s), sizeof(UINT), heapAt(1 + i));
  }
  for (int i = powerOfTwo; i < 9; ++i)
  {
    const int s = levelSizes.back();
    createBufSrv(_device.Get(), levelBuffers.back().Get(), static_cast<UINT>(s * s * s), sizeof(UINT), heapAt(1 + i));
  }
  createBufUav(_device.Get(), vboBuf.Get(), static_cast<UINT>(triangleData.size()), sizeof(float) * 4, heapAt(10));

  const UINT tg = 64;
  const UINT dispatchX = dispatchAxis(static_cast<UINT>(numberOfTriangles), tg);

  resetCommandList();
  _commandList->SetPipelineState(_traversePso[powerOfTwo].Get());
  _commandList->SetComputeRootSignature(_traverseRs.Get());
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetComputeRootDescriptorTable(0, gpuAt(0));
  _commandList->SetComputeRoot32BitConstants(1, sizeof(IsoConstants) / 4, &constants, 0);
  _commandList->Dispatch(dispatchX, 1, 1);
  executeAndWait();

  readbackBuffer(vboBuf.Get(), triangleData.data(), triangleData.size() * sizeof(float4),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return triangleData;
}

std::vector<float4> SKComputeIsosurface::computeIsosurfaceCPUImplementation(int3 dimensions, std::vector<float> *voxels,
                                                                            double isoValue) noexcept
{
  int largestSize = std::max({dimensions.x, dimensions.y, dimensions.z});
  int powerOfTwo = 1;
  while (largestSize > pow(2, powerOfTwo))
  {
    powerOfTwo += 1;
  }
  size_t size = pow(2, powerOfTwo);

  MarchingCubes cube(dimensions.x, dimensions.y, dimensions.z);
  cube.init_all();

  for (int i = 0; i < dimensions.x; i++)
  {
    for (int j = 0; j < dimensions.y; j++)
    {
      for (int k = 0; k < dimensions.z; k++)
      {
        double value = (*voxels)[i + size * j + k * size * size];
        cube.set_data(value, i, j, k);
      }
    }
  }

  cube.run(isoValue);

  int numberOfTriangles = cube.ntrigs();
  std::vector<float4> data{};
  data.reserve(3 * 3 * numberOfTriangles);

  for (int i = 0; i < cube.ntrigs(); i++)
  {
    const Triangle *tri = cube.trig(i);

    const Vertex *vertex1 = cube.vert(tri->v1);
    data.push_back(double4(vertex1->x / dimensions.x, vertex1->y / dimensions.y, vertex1->z / dimensions.z, 1.0));
    data.push_back(double4(vertex1->nx, vertex1->ny, vertex1->nz, 0.0));
    data.push_back(double4(0.0, 0.0, 0.0, 0.0));

    const Vertex *vertex2 = cube.vert(tri->v2);
    data.push_back(double4(vertex2->x / dimensions.x, vertex2->y / dimensions.y, vertex2->z / dimensions.z, 1.0));
    data.push_back(double4(vertex2->nx, vertex2->ny, vertex2->nz, 0.0));
    data.push_back(double4(0.0, 0.0, 0.0, 0.0));

    const Vertex *vertex3 = cube.vert(tri->v3);
    data.push_back(double4(vertex3->x / dimensions.x, vertex3->y / dimensions.y, vertex3->z / dimensions.z, 1.0));
    data.push_back(double4(vertex3->nx, vertex3->ny, vertex3->nz, 0.0));
    data.push_back(double4(0.0, 0.0, 0.0, 0.0));
  }
  return data;
}

const std::string SKComputeIsosurface::_marchingCubesKernel =
#include "skcomputeisosurface_kernel_string.inc"
;
