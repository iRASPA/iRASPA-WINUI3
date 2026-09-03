/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcomputeenergygrid.h"
#include <iostream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
struct EnergyGridConstants
{
  int numberOfReplicas;
  int startIndexAtoms;
  int endIndexAtoms;
  int pad;
  float cella[4];
  float cellb[4];
  float cellc[4];
  int numberOfBlockingPockets;
  int pad1[3];
  float replicaCorrection[4];
};

void createBufferSrv(ID3D12Device *device, ID3D12Resource *resource, UINT numElements, UINT stride,
                     D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 0;
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
  uav.Buffer.FirstElement = 0;
  uav.Buffer.NumElements = std::max(1u, numElements);
  uav.Buffer.StructureByteStride = stride;
  device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
}
} // namespace

SKComputeEnergyGrid::SKComputeEnergyGrid()
{
  if (!_isDx12Initialized)
    return;

  ComPtr<ID3DBlob> cs = compileComputeShader(_energyGridKernel, "ComputeEnergyGrid");
  if (!cs)
    return;

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 6;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].RegisterSpace = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].RegisterSpace = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 6;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 2;
  params[0].DescriptorTable.pDescriptorRanges = ranges;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants.ShaderRegister = 0;
  params[1].Constants.RegisterSpace = 0;
  params[1].Constants.Num32BitValues = sizeof(EnergyGridConstants) / 4;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> rsBlob;
  ComPtr<ID3DBlob> rsError;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError)))
  {
    if (rsError)
      std::cerr << "SKComputeEnergyGrid: root signature serialize failed:"
               << static_cast<const char *>(rsError->GetBufferPointer());
    return;
  }
  if (FAILED(_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                          IID_PPV_ARGS(&_rootSignature))))
  {
    std::cerr << "SKComputeEnergyGrid: CreateRootSignature failed";
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = _rootSignature.Get();
  psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
  if (FAILED(_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "SKComputeEnergyGrid: CreateComputePipelineState failed";
    return;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 7;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_descriptorHeap))))
  {
    std::cerr << "SKComputeEnergyGrid: CreateDescriptorHeap failed";
    return;
  }
  _descriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  _isDx12Ready = true;
  std::cerr << "SKComputeEnergyGrid: DX12 ready\n";
}

std::vector<bool> SKComputeEnergyGrid::blockedGridPoints(int3 size, double3x3 unitCell,
                                                         const std::vector<double4> &blockingPockets) noexcept
{
  std::vector<bool> blocked(static_cast<size_t>(size.x) * size.y * size.z, false);
  if (blockingPockets.empty())
    return blocked;

  const double denomX = double(std::max(size.x - 1, 1));
  const double denomY = double(std::max(size.y - 1, 1));
  const double denomZ = double(std::max(size.z - 1, 1));

  for (int k = 0; k < size.z; ++k)
  {
    for (int j = 0; j < size.y; ++j)
    {
      for (int i = 0; i < size.x; ++i)
      {
        // The grid spans the unit cell, whatever the replica cell the atoms are wrapped over, and a pocket
        // is a feature of the framework, so the nearest image is taken in unit-cell coordinates.
        const double3 position(double(i) / denomX, double(j) / denomY, double(k) / denomZ);
        for (const double4 &pocket : blockingPockets)
        {
          double3 ds = position - double3(pocket.x, pocket.y, pocket.z);
          ds.x -= std::rint(ds.x);
          ds.y -= std::rint(ds.y);
          ds.z -= std::rint(ds.z);
          if ((unitCell * ds).length() < pocket.w)
          {
            blocked[static_cast<size_t>(i) + static_cast<size_t>(size.x) * (j + static_cast<size_t>(size.y) * k)] = true;
            break;
          }
        }
      }
    }
  }
  return blocked;
}

std::vector<float> SKComputeEnergyGrid::ComputeEnergyGrid(int3 size, double2 probeParameter,
                                                          std::vector<double3> positions,
                                                          std::vector<double2> potentialParameters,
                                                          double3x3 unitCell, int3 numberOfReplicas,
                                                          std::vector<double4> blockingPockets)
{
  if (getInstance()._isDx12Ready)
  {
    try
    {
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<float> result = getInstance().computeEnergyGridGPUImplementation(
          size, probeParameter, positions, potentialParameters, unitCell, numberOfReplicas, blockingPockets);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
      std::cerr << "SKComputeEnergyGrid: GPU " << size.x << "^3 in " << ms << " ms\n";
      return result;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "SKComputeEnergyGrid GPU failed, falling back to CPU: " << ex.what() << "\n";
    }
  }
  else
  {
    std::cerr << "SKComputeEnergyGrid: DX12 not ready, using CPU\n";
  }
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<float> result = computeEnergyGridCPUImplementation(size, probeParameter, positions,
                                                                 potentialParameters, unitCell, numberOfReplicas,
                                                                 blockingPockets);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cerr << "SKComputeEnergyGrid: CPU " << size.x << "^3 in " << ms << " ms\n";
  return result;
}

std::vector<float> SKComputeEnergyGrid::computeEnergyGridGPUImplementation(
    int3 size, double2 probeParameter, std::vector<double3> positions,
    std::vector<double2> potentialParameters, double3x3 unitCell, int3 numberOfReplicas,
    const std::vector<double4> &blockingPockets)
{
  std::lock_guard<std::mutex> lock(_gpuMutex);

  const size_t numberOfAtoms = positions.size();
  const int temp = size.x * size.y * size.z;
  const size_t numberOfGridPoints =
      (static_cast<size_t>(temp) + kThreadGroupSize - 1) & ~(static_cast<size_t>(kThreadGroupSize) - 1);

  std::vector<float> outputData(numberOfGridPoints, 0.0f);
  if (numberOfAtoms == 0)
  {
    outputData.resize(static_cast<size_t>(temp));
    return outputData;
  }

  std::vector<float> pos(numberOfAtoms * 4);
  std::vector<float> epsilon(numberOfAtoms);
  std::vector<float> sigma(numberOfAtoms);
  std::vector<float> gridPositions(numberOfGridPoints * 4, 0.0f);

  const double3 correction = double3(1.0 / double(numberOfReplicas.x), 1.0 / double(numberOfReplicas.y),
                                     1.0 / double(numberOfReplicas.z));
  const double denomX = double(std::max(size.x - 1, 1));
  const double denomY = double(std::max(size.y - 1, 1));
  const double denomZ = double(std::max(size.z - 1, 1));

  for (size_t i = 0; i < numberOfAtoms; ++i)
  {
    const double3 position = correction * positions[i];
    const double2 currentPotentialParameters = potentialParameters[i];
    pos[i * 4 + 0] = static_cast<float>(position.x);
    pos[i * 4 + 1] = static_cast<float>(position.y);
    pos[i * 4 + 2] = static_cast<float>(position.z);
    pos[i * 4 + 3] = 0.0f;
    epsilon[i] = static_cast<float>(4.0 * std::sqrt(currentPotentialParameters.x * probeParameter.x));
    sigma[i] = static_cast<float>(0.5 * (currentPotentialParameters.y + probeParameter.y));
  }

  int index = 0;
  for (int k = 0; k < size.z; ++k)
  {
    for (int j = 0; j < size.y; ++j)
    {
      for (int i = 0; i < size.x; ++i)
      {
        const double3 position =
            correction * double3(double(i) / denomX, double(j) / denomY, double(k) / denomZ);
        gridPositions[index * 4 + 0] = static_cast<float>(position.x);
        gridPositions[index * 4 + 1] = static_cast<float>(position.y);
        gridPositions[index * 4 + 2] = static_cast<float>(position.z);
        gridPositions[index * 4 + 3] = 0.0f;
        ++index;
      }
    }
  }

  const size_t totalNumberOfReplicas =
      static_cast<size_t>(numberOfReplicas.x * numberOfReplicas.y * numberOfReplicas.z);
  std::vector<float> replicaVector(totalNumberOfReplicas * 4);
  index = 0;
  for (int i = 0; i < numberOfReplicas.x; ++i)
  {
    for (int j = 0; j < numberOfReplicas.y; ++j)
    {
      for (int k = 0; k < numberOfReplicas.z; ++k)
      {
        replicaVector[index * 4 + 0] = static_cast<float>(double(i) / double(numberOfReplicas.x));
        replicaVector[index * 4 + 1] = static_cast<float>(double(j) / double(numberOfReplicas.y));
        replicaVector[index * 4 + 2] = static_cast<float>(double(k) / double(numberOfReplicas.z));
        replicaVector[index * 4 + 3] = 0.0f;
        ++index;
      }
    }
  }

  const double3x3 replicaCell = double3x3(double(numberOfReplicas.x) * unitCell[0],
                                          double(numberOfReplicas.y) * unitCell[1],
                                          double(numberOfReplicas.z) * unitCell[2]);

  EnergyGridConstants constants{};
  constants.numberOfReplicas = static_cast<int>(totalNumberOfReplicas);
  constants.cella[0] = static_cast<float>(replicaCell[0][0]);
  constants.cella[1] = static_cast<float>(replicaCell[1][0]);
  constants.cella[2] = static_cast<float>(replicaCell[2][0]);
  constants.cella[3] = 0.0f;
  constants.cellb[0] = static_cast<float>(replicaCell[0][1]);
  constants.cellb[1] = static_cast<float>(replicaCell[1][1]);
  constants.cellb[2] = static_cast<float>(replicaCell[2][1]);
  constants.cellb[3] = 0.0f;
  constants.cellc[0] = static_cast<float>(replicaCell[0][2]);
  constants.cellc[1] = static_cast<float>(replicaCell[1][2]);
  constants.cellc[2] = static_cast<float>(replicaCell[2][2]);
  constants.cellc[3] = 0.0f;
  constants.numberOfBlockingPockets = static_cast<int>(blockingPockets.size());
  constants.replicaCorrection[0] = static_cast<float>(correction.x);
  constants.replicaCorrection[1] = static_cast<float>(correction.y);
  constants.replicaCorrection[2] = static_cast<float>(correction.z);
  constants.replicaCorrection[3] = 0.0f;

  // A structured buffer cannot be zero-length, so a structure without pockets is still given one element;
  // the count in the constants keeps the kernel out of it.
  std::vector<float> pocketVector(std::max<size_t>(blockingPockets.size(), 1) * 4, 0.0f);
  for (size_t i = 0; i < blockingPockets.size(); ++i)
  {
    pocketVector[i * 4 + 0] = static_cast<float>(blockingPockets[i].x);
    pocketVector[i * 4 + 1] = static_cast<float>(blockingPockets[i].y);
    pocketVector[i * 4 + 2] = static_cast<float>(blockingPockets[i].z);
    pocketVector[i * 4 + 3] = static_cast<float>(blockingPockets[i].w);
  }

  // One upload pass for all buffers (was 6 separate GPU fences).
  resetCommandList();
  StagedUpload posStaged = recordUpload(pos.data(), pos.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload gridStaged = recordUpload(gridPositions.data(), gridPositions.size() * sizeof(float),
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload epsStaged = recordUpload(epsilon.data(), epsilon.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload sigStaged = recordUpload(sigma.data(), sigma.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload repStaged = recordUpload(replicaVector.data(), replicaVector.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload pocketStaged = recordUpload(pocketVector.data(), pocketVector.size() * sizeof(float),
                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload outStaged = recordUpload(outputData.data(), outputData.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!posStaged.resource || !gridStaged.resource || !epsStaged.resource || !sigStaged.resource
      || !repStaged.resource || !pocketStaged.resource || !outStaged.resource)
    throw std::runtime_error("SKComputeEnergyGrid: buffer creation failed");
  executeAndWait();

  ComPtr<ID3D12Resource> posBuf = posStaged.resource;
  ComPtr<ID3D12Resource> gridBuf = gridStaged.resource;
  ComPtr<ID3D12Resource> epsBuf = epsStaged.resource;
  ComPtr<ID3D12Resource> sigBuf = sigStaged.resource;
  ComPtr<ID3D12Resource> repBuf = repStaged.resource;
  ComPtr<ID3D12Resource> pocketBuf = pocketStaged.resource;
  ComPtr<ID3D12Resource> outBuf = outStaged.resource;

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  createBufferSrv(_device.Get(), posBuf.Get(), static_cast<UINT>(numberOfAtoms), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), gridBuf.Get(), static_cast<UINT>(numberOfGridPoints), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), epsBuf.Get(), static_cast<UINT>(numberOfAtoms), sizeof(float), cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), sigBuf.Get(), static_cast<UINT>(numberOfAtoms), sizeof(float), cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), repBuf.Get(), static_cast<UINT>(totalNumberOfReplicas), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), pocketBuf.Get(), static_cast<UINT>(pocketVector.size() / 4), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferUav(_device.Get(), outBuf.Get(), static_cast<UINT>(numberOfGridPoints), sizeof(float), cpu);

  const UINT dispatchX = static_cast<UINT>(numberOfGridPoints / kThreadGroupSize);
  // All atom batches in one command list (OpenCL waited per batch for older WDDM watchdogs;
  // a single DX12 submit for a 128^3 grid stays well under the TDR limit).
  resetCommandList();
  _commandList->SetPipelineState(_pso.Get());
  _commandList->SetComputeRootSignature(_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _descriptorHeap.Get() };
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetComputeRootDescriptorTable(0, _descriptorHeap->GetGPUDescriptorHandleForHeapStart());

  size_t unitsOfWorkDone = 0;
  const size_t sizeOfWorkBatch = 4096;
  while (unitsOfWorkDone < numberOfAtoms)
  {
    const size_t numberOfAtomsPerBatch = std::min(sizeOfWorkBatch, numberOfAtoms - unitsOfWorkDone);
    constants.startIndexAtoms = static_cast<int>(unitsOfWorkDone);
    constants.endIndexAtoms = static_cast<int>(unitsOfWorkDone + numberOfAtomsPerBatch);
    _commandList->SetComputeRoot32BitConstants(1, sizeof(EnergyGridConstants) / 4, &constants, 0);
    _commandList->Dispatch(dispatchX, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = outBuf.Get();
    _commandList->ResourceBarrier(1, &uavBarrier);

    unitsOfWorkDone += sizeOfWorkBatch;
  }
  executeAndWait();

  readbackBuffer(outBuf.Get(), outputData.data(), outputData.size() * sizeof(float),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  outputData.resize(static_cast<size_t>(temp));
  return outputData;
}

std::vector<float> SKComputeEnergyGrid::computeEnergyGridCPUImplementation(
    int3 size, double2 probeParameter, std::vector<double3> positions,
    std::vector<double2> potentialParameters, double3x3 unitCell, int3 numberOfReplicas,
    std::vector<double4> blockingPockets) noexcept
{
  size_t numberOfAtoms = positions.size();
  int temp = size.x * size.y * size.z;

  std::vector<float> outputData = std::vector<float>(temp);

  double3 correction = double3(1.0 / double(numberOfReplicas.x), 1.0 / double(numberOfReplicas.y),
                               1.0 / double(numberOfReplicas.z));

  double3x3 replicaCell = double3x3(double(numberOfReplicas.x) * unitCell[0],
                                    double(numberOfReplicas.y) * unitCell[1],
                                    double(numberOfReplicas.z) * unitCell[2]);

  int totalNumberOfReplicas = numberOfReplicas.x * numberOfReplicas.y * numberOfReplicas.z;
  std::vector<double3> replicaVector(totalNumberOfReplicas);
  int index = 0;
  for (int i = 0; i < numberOfReplicas.x; i++)
  {
    for (int j = 0; j < numberOfReplicas.y; j++)
    {
      for (int k = 0; k < numberOfReplicas.z; k++)
      {
        replicaVector[index] = double3((double(i) / double(numberOfReplicas.x)),
                                       (double(j) / double(numberOfReplicas.y)),
                                       (double(k) / double(numberOfReplicas.z)));
        index += 1;
      }
    }
  }

  const double denomX = double(std::max(size.x - 1, 1));
  const double denomY = double(std::max(size.y - 1, 1));
  const double denomZ = double(std::max(size.z - 1, 1));

  const unsigned threadCount = std::max(1u, std::thread::hardware_concurrency());
  std::atomic<int> nextZ{0};
  std::vector<std::thread> workers;
  workers.reserve(threadCount);

  auto computeSlice = [&](int z) {
    for (int y = 0; y < size.y; y++)
    {
      for (int x = 0; x < size.x; x++)
      {
        double3 gridPosition = correction * double3(double(x) / denomX, double(y) / denomY, double(z) / denomZ);

        // A blocked pocket is pore the probe is not allowed into, so the point counts as an overlap and no
        // atom is summed into it. The energy ramps with the depth rather than being one flat overlap value,
        // so that a level set can follow the sphere rather than the grid planes.
        double nearestPocket = 1.0e10;
        for (const double4 &pocket : blockingPockets)
        {
          double3 ds = double3(double(x) / denomX, double(y) / denomY, double(z) / denomZ)
                       - double3(pocket.x, pocket.y, pocket.z);
          ds.x -= std::rint(ds.x);
          ds.y -= std::rint(ds.y);
          ds.z -= std::rint(ds.z);
          nearestPocket = std::min(nearestPocket, (unitCell * ds).length() - pocket.w);
        }
        if (nearestPocket < 0.0)
        {
          outputData[x + y * size.x + z * size.x * size.y] =
              static_cast<float>(std::min(-nearestPocket * double(blockedEnergyPerAngstrom), double(overlapEnergy)));
          continue;
        }

        double value = 0.0;
        for (size_t i = 0; i < numberOfAtoms; i++)
        {
          double3 position = correction * positions[i];
          double2 currentPotentialParameters = potentialParameters[i];

          double epsilon = 4.0 * sqrt(currentPotentialParameters.x * probeParameter.x);
          double sigma = 0.5 * (currentPotentialParameters.y + probeParameter.y);

          for (int j = 0; j < totalNumberOfReplicas; j++)
          {
            double3 ds = gridPosition - position - replicaVector[j];
            ds.x -= std::rint(ds.x);
            ds.y -= std::rint(ds.y);
            ds.z -= std::rint(ds.z);
            double3 dr = replicaCell * ds;

            double rr = dr.x * dr.x + dr.y * dr.y + dr.z * dr.z;
            if (rr < 12.0 * 12.0)
            {
              double sigma2rr = sigma * sigma / rr;
              double rri3 = sigma2rr * sigma2rr * sigma2rr;
              value += epsilon * (rri3 * (rri3 - 1.0));
            }
          }
        }

        outputData[x + y * size.x + z * size.x * size.y] = static_cast<float>(std::min(value, 10000000.0));
      }
    }
  };

  for (unsigned t = 0; t < threadCount; ++t)
  {
    workers.emplace_back([&]() {
      for (;;)
      {
        const int z = nextZ.fetch_add(1, std::memory_order_relaxed);
        if (z >= size.z)
          break;
        computeSlice(z);
      }
    });
  }

  for (std::thread &worker : workers)
    worker.join();

  return outputData;
}

const std::string SKComputeEnergyGrid::_energyGridKernel = R"foo(
cbuffer Constants : register(b0)
{
  int numberOfReplicas;
  int startIndexAtoms;
  int endIndexAtoms;
  int pad;
  float4 cella;
  float4 cellb;
  float4 cellc;
  int numberOfBlockingPockets;
  int3 pad1;
  float4 replicaCorrection;
};

StructuredBuffer<float4> position : register(t0);
StructuredBuffer<float4> gridposition : register(t1);
StructuredBuffer<float> epsilon : register(t2);
StructuredBuffer<float> sigma : register(t3);
StructuredBuffer<float4> replicaCell : register(t4);
StructuredBuffer<float4> blockingPockets : register(t5);
RWStructuredBuffer<float> outputEnergy : register(u0);

// Mirrors SKComputeEnergyGrid::overlapEnergy and ::blockedEnergyPerAngstrom.
static const float overlapEnergy = 10000000.0f;
static const float blockedEnergyPerAngstrom = 1000.0f;

// How far a grid point is, in angstrom, from the surface of the nearest blocking pocket: negative inside
// one, and a large positive number when the structure has no pockets.
//
// A pocket is a sphere given as a fractional position of the unit cell with a radius in angstrom, and it is
// a feature of the framework, so it repeats with the unit cell. The atoms are wrapped over the replica cell
// instead, which is a whole number of unit cells wide, so the nearest image here has to be taken in
// unit-cell coordinates: the grid position is scaled up out of the replica cell, wrapped, and scaled back to
// measure the distance with the replica cell matrix the kernels already carry.
float blockingPocketDistance(float3 gridpos)
{
  float nearest = 1.0e10f;
  const float3 unitCellPosition = gridpos / replicaCorrection.xyz;
  for (int i = 0; i < numberOfBlockingPockets; ++i)
  {
    float3 ds = unitCellPosition - blockingPockets[i].xyz;
    ds -= round(ds);
    float3 t = ds * replicaCorrection.xyz;
    float3 dr = float3(dot(cella.xyz, t), dot(cellb.xyz, t), dot(cellc.xyz, t));
    nearest = min(nearest, length(dr) - blockingPockets[i].w);
  }
  return nearest;
}

[numthreads(64, 1, 1)]
void ComputeEnergyGrid(uint3 dtid : SV_DispatchThreadID)
{
  uint igrid = dtid.x;
  float value = 0.0f;
  float4 gridpos = gridposition[igrid];

  // The energy is assigned rather than accumulated, which keeps it the same value however many atom batches
  // the caller runs over the same grid.
  const float pocket = blockingPocketDistance(gridpos.xyz);
  if (pocket < 0.0f)
  {
    outputEnergy[igrid] = min(-pocket * blockedEnergyPerAngstrom, overlapEnergy);
    return;
  }

  for (int j = 0; j < numberOfReplicas; ++j)
  {
    for (int iatom = startIndexAtoms; iatom < endIndexAtoms; ++iatom)
    {
      float4 pos = position[iatom];
      float4 dr = gridpos - pos - replicaCell[j];
      float4 t = dr - round(dr);

      dr.x = dot(cella, t);
      dr.y = dot(cellb, t);
      dr.z = dot(cellc, t);
      dr.w = 0.0f;

      float size = sigma[iatom];
      float rr = dot(dr, dr);
      if (rr < 12.0f * 12.0f)
      {
        float temp = size * size / rr;
        float rri3 = temp * temp * temp;
        value += epsilon[iatom] * (rri3 * (rri3 - 1.0f));
      }
    }
  }

  outputEnergy[igrid] += min(value, overlapEnergy);
}
)foo";
