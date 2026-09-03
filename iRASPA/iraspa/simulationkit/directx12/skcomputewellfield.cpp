/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skcomputewellfield.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
// The probe-atom contact optimum as a multiple of sigma: the minimum of the Lennard-Jones pair, 2^(1/6).
constexpr double contactOptimum = 1.12246204831;

// The softmin temperature of the direction average, shared by the field and the refinement: creases between
// atoms this close in distance blend rather than switch.
constexpr double softminTemperature = 0.4;

struct WellFieldConstants
{
  int numberOfReplicas;
  int startIndexAtoms;
  int endIndexAtoms;
  int numberOfBlockingPockets;
  float cella[4];
  float cellb[4];
  float cellc[4];
  float replicaCorrection[4];
};

struct RefineConstants
{
  int numberOfAtoms;
  int numberOfReplicas;
  int numberOfVertices;
  int numberOfBlockingPockets;
  float cella[4];
  float cellb[4];
  float cellc[4];
  float inverseCella[4];
  float inverseCellb[4];
  float inverseCellc[4];
  float replicaCorrection[4];
  float isovalue;
  float pad[3];
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

// Row r of the matrix as the kernels read it: the cell is stored by columns, and a kernel multiplies by
// taking the dot product of a row with the fractional vector.
void writeMatrixRows(const double3x3 &matrix, float rowA[4], float rowB[4], float rowC[4])
{
  const float rows[3][4] = {
    { float(matrix[0][0]), float(matrix[1][0]), float(matrix[2][0]), 0.0f },
    { float(matrix[0][1]), float(matrix[1][1]), float(matrix[2][1]), 0.0f },
    { float(matrix[0][2]), float(matrix[1][2]), float(matrix[2][2]), 0.0f },
  };
  std::memcpy(rowA, rows[0], sizeof(rows[0]));
  std::memcpy(rowB, rows[1], sizeof(rows[1]));
  std::memcpy(rowC, rows[2], sizeof(rows[2]));
}

// How far a point in replica-cell fractional coordinates is, in angstrom, from the surface of the nearest
// blocking pocket. Mirrors blockingPocketDistance in the kernels.
double blockingPocketDistance(const double3 &point, const std::vector<float> &blockingPockets,
                              size_t numberOfBlockingPockets, const double3x3 &cell, const double3 &correction)
{
  double nearest = 1.0e10;
  const double3 unitCellPosition(point.x / correction.x, point.y / correction.y, point.z / correction.z);
  for (size_t i = 0; i < numberOfBlockingPockets; ++i)
  {
    double3 ds = unitCellPosition - double3(blockingPockets[4 * i], blockingPockets[4 * i + 1],
                                            blockingPockets[4 * i + 2]);
    ds.x -= std::rint(ds.x);
    ds.y -= std::rint(ds.y);
    ds.z -= std::rint(ds.z);
    nearest = std::min(nearest, (cell * (ds * correction)).length() - double(blockingPockets[4 * i + 3]));
  }
  return nearest;
}
} // namespace

SKComputeWellField::SKComputeWellField()
{
  if (!_isDx12Initialized)
    return;

  _descriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  if (!createPipeline(_wellFieldKernel, "ComputeWellFieldGrid", 5, 2,
                      sizeof(WellFieldConstants) / 4, _fieldRootSignature, _fieldPso, _fieldHeap))
    return;
  if (!createPipeline(_refineKernel, "RefineWellSurfaceVertices", 4, 1,
                      sizeof(RefineConstants) / 4, _refineRootSignature, _refinePso, _refineHeap))
    return;

  _isDx12Ready = true;
  std::cerr << "SKComputeWellField: DX12 ready\n";
}

bool SKComputeWellField::createPipeline(const std::string &source, const char *entryPoint, UINT numberOfSrvs,
                                        UINT numberOfUavs, UINT numberOfConstants,
                                        ComPtr<ID3D12RootSignature> &rootSignature,
                                        ComPtr<ID3D12PipelineState> &pso, ComPtr<ID3D12DescriptorHeap> &heap)
{
  ComPtr<ID3DBlob> cs = compileComputeShader(source, entryPoint);
  if (!cs)
    return false;

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = numberOfSrvs;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = numberOfUavs;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = numberOfSrvs;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable.NumDescriptorRanges = 2;
  params[0].DescriptorTable.pDescriptorRanges = ranges;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants.ShaderRegister = 0;
  params[1].Constants.Num32BitValues = numberOfConstants;
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
      std::cerr << "SKComputeWellField: root signature serialize failed: "
                << static_cast<const char *>(rsError->GetBufferPointer()) << "\n";
    return false;
  }
  if (FAILED(_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                          IID_PPV_ARGS(&rootSignature))))
  {
    std::cerr << "SKComputeWellField: CreateRootSignature failed\n";
    return false;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = rootSignature.Get();
  psoDesc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
  if (FAILED(_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "SKComputeWellField: CreateComputePipelineState failed\n";
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = numberOfSrvs + numberOfUavs;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap))))
  {
    std::cerr << "SKComputeWellField: CreateDescriptorHeap failed\n";
    return false;
  }
  return true;
}

SKComputeWellField::FieldInputs SKComputeWellField::prepareInputs(
    double2 probeParameter, const std::vector<double3> &positions,
    const std::vector<double2> &potentialParameters, double3x3 unitCell, int3 numberOfReplicas,
    const std::vector<double4> &blockingPockets)
{
  FieldInputs inputs;
  inputs.numberOfAtoms = positions.size();
  inputs.numberOfBlockingPockets = blockingPockets.size();
  inputs.correction = double3(1.0 / double(numberOfReplicas.x), 1.0 / double(numberOfReplicas.y),
                              1.0 / double(numberOfReplicas.z));
  inputs.replicaCell = double3x3(double(numberOfReplicas.x) * unitCell[0],
                                 double(numberOfReplicas.y) * unitCell[1],
                                 double(numberOfReplicas.z) * unitCell[2]);

  inputs.atomPositions.resize(inputs.numberOfAtoms * 4);
  inputs.potentialParameters.resize(inputs.numberOfAtoms * 2);
  for (size_t i = 0; i < inputs.numberOfAtoms; ++i)
  {
    const double3 position = inputs.correction * positions[i];
    inputs.atomPositions[4 * i + 0] = float(position.x);
    inputs.atomPositions[4 * i + 1] = float(position.y);
    inputs.atomPositions[4 * i + 2] = float(position.z);
    inputs.atomPositions[4 * i + 3] = 0.0f;
    // 4 x epsilon, Lorentz-Berthelot mixed with the probe, exactly as the energy grid
    inputs.potentialParameters[2 * i + 0] =
        float(4.0 * std::sqrt(potentialParameters[i].x * probeParameter.x));
    inputs.potentialParameters[2 * i + 1] = float(0.5 * (potentialParameters[i].y + probeParameter.y));
  }

  inputs.numberOfReplicas =
      static_cast<size_t>(numberOfReplicas.x) * numberOfReplicas.y * numberOfReplicas.z;
  inputs.replicas.resize(inputs.numberOfReplicas * 4, 0.0f);
  size_t index = 0;
  for (int i = 0; i < numberOfReplicas.x; ++i)
  {
    for (int j = 0; j < numberOfReplicas.y; ++j)
    {
      for (int k = 0; k < numberOfReplicas.z; ++k)
      {
        inputs.replicas[4 * index + 0] = float(double(i) / double(numberOfReplicas.x));
        inputs.replicas[4 * index + 1] = float(double(j) / double(numberOfReplicas.y));
        inputs.replicas[4 * index + 2] = float(double(k) / double(numberOfReplicas.z));
        ++index;
      }
    }
  }

  // A structured buffer cannot be zero-length, so a structure without pockets is still given one element;
  // the count in the constants keeps the kernels out of it.
  inputs.blockingPockets.assign(std::max<size_t>(blockingPockets.size(), 1) * 4, 0.0f);
  for (size_t i = 0; i < blockingPockets.size(); ++i)
  {
    inputs.blockingPockets[4 * i + 0] = float(blockingPockets[i].x);
    inputs.blockingPockets[4 * i + 1] = float(blockingPockets[i].y);
    inputs.blockingPockets[4 * i + 2] = float(blockingPockets[i].z);
    inputs.blockingPockets[4 * i + 3] = float(blockingPockets[i].w);
  }

  return inputs;
}

std::vector<float> SKComputeWellField::computeWellFieldGrid(int3 size, double2 probeParameter,
                                                            std::vector<double3> positions,
                                                            std::vector<double2> potentialParameters,
                                                            double3x3 unitCell, int3 numberOfReplicas,
                                                            std::vector<double4> blockingPockets)
{
  if (positions.empty() || size.x <= 0 || size.y <= 0 || size.z <= 0)
    return {};

  const FieldInputs inputs = prepareInputs(probeParameter, positions, potentialParameters, unitCell,
                                           numberOfReplicas, blockingPockets);

  if (getInstance()._isDx12Ready)
  {
    try
    {
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<float> result = getInstance().computeWellFieldGridGPUImplementation(size, inputs);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();
      std::cerr << "SKComputeWellField: GPU " << size.x << "^3 in " << ms << " ms\n";
      return result;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "SKComputeWellField GPU failed, falling back to CPU: " << ex.what() << "\n";
    }
  }

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<float> result = computeWellFieldGridCPUImplementation(size, inputs);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::cerr << "SKComputeWellField: CPU " << size.x << "^3 in " << ms << " ms\n";
  return result;
}

std::vector<float> SKComputeWellField::computeWellFieldGridGPUImplementation(int3 size,
                                                                             const FieldInputs &inputs)
{
  std::lock_guard<std::mutex> lock(_gpuMutex);

  const size_t temp = static_cast<size_t>(size.x) * size.y * size.z;
  const size_t numberOfGridPoints = (temp + kThreadGroupSize - 1) & ~(static_cast<size_t>(kThreadGroupSize) - 1);

  std::vector<float> gridPositions(numberOfGridPoints * 4, 0.0f);
  size_t index = 0;
  for (int k = 0; k < size.z; ++k)
  {
    for (int j = 0; j < size.y; ++j)
    {
      for (int i = 0; i < size.x; ++i)
      {
        // Spacing 1/size and not 1/(size-1): the field is periodic with the cell, and marching cubes reads
        // the grid back at index/size, wrapping index+1 past the end round to 0.
        const double3 position = inputs.correction * double3(double(i) / double(size.x),
                                                             double(j) / double(size.y),
                                                             double(k) / double(size.z));
        gridPositions[4 * index + 0] = float(position.x);
        gridPositions[4 * index + 1] = float(position.y);
        gridPositions[4 * index + 2] = float(position.z);
        ++index;
      }
    }
  }

  // The energy accumulates from zero, the distance accumulates through min() from +large, and the softmin
  // sums from zero.
  std::vector<float> accumulated(numberOfGridPoints * 2);
  for (size_t i = 0; i < numberOfGridPoints; ++i)
  {
    accumulated[2 * i + 0] = 0.0f;
    accumulated[2 * i + 1] = 1.0e10f;
  }
  std::vector<float> softmin(numberOfGridPoints * 4, 0.0f);

  WellFieldConstants constants{};
  constants.numberOfReplicas = static_cast<int>(inputs.numberOfReplicas);
  constants.numberOfBlockingPockets = static_cast<int>(inputs.numberOfBlockingPockets);
  writeMatrixRows(inputs.replicaCell, constants.cella, constants.cellb, constants.cellc);
  constants.replicaCorrection[0] = float(inputs.correction.x);
  constants.replicaCorrection[1] = float(inputs.correction.y);
  constants.replicaCorrection[2] = float(inputs.correction.z);

  resetCommandList();
  StagedUpload posStaged = recordUpload(inputs.atomPositions.data(),
                                        inputs.atomPositions.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload gridStaged = recordUpload(gridPositions.data(), gridPositions.size() * sizeof(float),
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload parStaged = recordUpload(inputs.potentialParameters.data(),
                                        inputs.potentialParameters.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload repStaged = recordUpload(inputs.replicas.data(), inputs.replicas.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload pocketStaged = recordUpload(inputs.blockingPockets.data(),
                                           inputs.blockingPockets.size() * sizeof(float),
                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload accStaged = recordUpload(accumulated.data(), accumulated.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  StagedUpload softStaged = recordUpload(softmin.data(), softmin.size() * sizeof(float),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!posStaged.resource || !gridStaged.resource || !parStaged.resource || !repStaged.resource
      || !pocketStaged.resource || !accStaged.resource || !softStaged.resource)
    throw std::runtime_error("SKComputeWellField: buffer creation failed");
  executeAndWait();

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = _fieldHeap->GetCPUDescriptorHandleForHeapStart();
  createBufferSrv(_device.Get(), posStaged.resource.Get(), static_cast<UINT>(inputs.numberOfAtoms),
                  sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), gridStaged.resource.Get(), static_cast<UINT>(numberOfGridPoints),
                  sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), parStaged.resource.Get(), static_cast<UINT>(inputs.numberOfAtoms),
                  sizeof(float) * 2, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), repStaged.resource.Get(), static_cast<UINT>(inputs.numberOfReplicas),
                  sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), pocketStaged.resource.Get(),
                  static_cast<UINT>(inputs.blockingPockets.size() / 4), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferUav(_device.Get(), accStaged.resource.Get(), static_cast<UINT>(numberOfGridPoints),
                  sizeof(float) * 2, cpu);
  cpu.ptr += _descriptorSize;
  createBufferUav(_device.Get(), softStaged.resource.Get(), static_cast<UINT>(numberOfGridPoints),
                  sizeof(float) * 4, cpu);

  resetCommandList();
  _commandList->SetPipelineState(_fieldPso.Get());
  _commandList->SetComputeRootSignature(_fieldRootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _fieldHeap.Get() };
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetComputeRootDescriptorTable(0, _fieldHeap->GetGPUDescriptorHandleForHeapStart());

  // Batched over atoms so no single dispatch outlives the GPU watchdog, as in the energy grid.
  const size_t sizeOfWorkBatch = 4096;
  for (size_t done = 0; done < inputs.numberOfAtoms; done += sizeOfWorkBatch)
  {
    constants.startIndexAtoms = static_cast<int>(done);
    constants.endIndexAtoms = static_cast<int>(std::min(done + sizeOfWorkBatch, inputs.numberOfAtoms));
    _commandList->SetComputeRoot32BitConstants(1, sizeof(WellFieldConstants) / 4, &constants, 0);
    _commandList->Dispatch(static_cast<UINT>(numberOfGridPoints / kThreadGroupSize), 1, 1);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = accStaged.resource.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = softStaged.resource.Get();
    _commandList->ResourceBarrier(2, barriers);
  }
  executeAndWait();

  readbackBuffer(accStaged.resource.Get(), accumulated.data(), accumulated.size() * sizeof(float),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  readbackBuffer(softStaged.resource.Get(), softmin.data(), softmin.size() * sizeof(float),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // Collapse to three floats per grid point: (U, d, rel). The softmin weights share no per-point reference,
  // so they accumulate across atom batches; the common factor cancels in the ratio.
  std::vector<float> output(3 * temp);
  for (size_t i = 0; i < temp; ++i)
  {
    output[3 * i + 0] = accumulated[2 * i + 0];
    output[3 * i + 1] = accumulated[2 * i + 1];
    const float weightSum = softmin[4 * i + 3];
    output[3 * i + 2] =
        weightSum > 0.0f
            ? float3(softmin[4 * i + 0], softmin[4 * i + 1], softmin[4 * i + 2]).length() / weightSum
            : 1.0f;
  }
  return output;
}

std::vector<float> SKComputeWellField::computeWellFieldGridCPUImplementation(int3 size,
                                                                             const FieldInputs &inputs) noexcept
{
  const size_t temp = static_cast<size_t>(size.x) * size.y * size.z;
  std::vector<float> output(3 * temp, 0.0f);

  const unsigned threadCount = std::max(1u, std::thread::hardware_concurrency());
  std::atomic<int> nextZ{0};
  std::vector<std::thread> workers;
  workers.reserve(threadCount);

  auto computeSlice = [&](int z) {
    for (int y = 0; y < size.y; ++y)
    {
      for (int x = 0; x < size.x; ++x)
      {
        const double3 gridPosition = inputs.correction * double3(double(x) / double(size.x),
                                                                 double(y) / double(size.y),
                                                                 double(z) / double(size.z));

        double value = 0.0;
        double distance = 1.0e10;
        double3 directionSum(0.0, 0.0, 0.0);
        double weightSum = 0.0;

        const double pocket = blockingPocketDistance(gridPosition, inputs.blockingPockets,
                                                     inputs.numberOfBlockingPockets, inputs.replicaCell,
                                                     inputs.correction);

        for (size_t j = 0; j < inputs.numberOfReplicas; ++j)
        {
          const double3 replica(inputs.replicas[4 * j], inputs.replicas[4 * j + 1], inputs.replicas[4 * j + 2]);
          for (size_t iatom = 0; iatom < inputs.numberOfAtoms; ++iatom)
          {
            const double3 atom(inputs.atomPositions[4 * iatom], inputs.atomPositions[4 * iatom + 1],
                               inputs.atomPositions[4 * iatom + 2]);
            double3 ds = gridPosition - atom - replica;
            ds.x -= std::rint(ds.x);
            ds.y -= std::rint(ds.y);
            ds.z -= std::rint(ds.z);
            const double3 dr = inputs.replicaCell * ds;
            // An atom sitting exactly on a grid point would make r*r zero and the energy NaN; a floor of
            // 1e-8 puts such a point deep inside the repulsive core instead.
            const double rr = std::max(double3::dot(dr, dr), 1.0e-8);
            if (rr < 12.0 * 12.0)
            {
              const double sigma = double(inputs.potentialParameters[2 * iatom + 1]);
              const double sigma2rr = sigma * sigma / rr;
              const double rri3 = sigma2rr * sigma2rr * sigma2rr;
              value += double(inputs.potentialParameters[2 * iatom]) * (rri3 * (rri3 - 1.0));
              const double r = std::sqrt(rr);
              const double contact = r - contactOptimum * sigma;
              distance = std::min(distance, contact);
              const double w = std::exp(-contact / softminTemperature);
              directionSum = directionSum + (-w / r) * dr;
              weightSum += w;
            }
          }
        }

        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(size.x) * (y + static_cast<size_t>(size.y) * z);
        output[3 * i + 0] = pocket < 0.0
                                ? float(std::min(-pocket * 1000.0, 10000000.0))
                                : float(std::min(value, 10000000.0));
        output[3 * i + 1] = float(std::min(distance, pocket));
        output[3 * i + 2] = weightSum > 0.0 ? float(directionSum.length() / weightSum) : 1.0f;
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

  return output;
}

void SKComputeWellField::refineWellSurfaceVertices(std::vector<float4> &triangleData, double2 probeParameter,
                                                   std::vector<double3> positions,
                                                   std::vector<double2> potentialParameters,
                                                   double3x3 unitCell, int3 numberOfReplicas,
                                                   std::vector<double4> blockingPockets, float isovalue)
{
  if (positions.empty() || triangleData.empty())
    return;

  const FieldInputs inputs = prepareInputs(probeParameter, positions, potentialParameters, unitCell,
                                           numberOfReplicas, blockingPockets);

  if (getInstance()._isDx12Ready)
  {
    try
    {
      const auto t0 = std::chrono::steady_clock::now();
      getInstance().refineWellSurfaceVerticesGPUImplementation(triangleData, inputs, isovalue);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();
      std::cerr << "SKComputeWellField: refined " << (triangleData.size() / 3) << " vertices on the GPU in "
                << ms << " ms\n";
      return;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "SKComputeWellField refinement GPU failed, falling back to CPU: " << ex.what() << "\n";
    }
  }

  refineWellSurfaceVerticesCPUImplementation(triangleData, inputs, isovalue);
}

void SKComputeWellField::refineWellSurfaceVerticesGPUImplementation(std::vector<float4> &triangleData,
                                                                    const FieldInputs &inputs, float isovalue)
{
  std::lock_guard<std::mutex> lock(_gpuMutex);

  const size_t numberOfVertices = triangleData.size() / 3;

  RefineConstants constants{};
  constants.numberOfAtoms = static_cast<int>(inputs.numberOfAtoms);
  constants.numberOfReplicas = static_cast<int>(inputs.numberOfReplicas);
  constants.numberOfVertices = static_cast<int>(numberOfVertices);
  constants.numberOfBlockingPockets = static_cast<int>(inputs.numberOfBlockingPockets);
  writeMatrixRows(inputs.replicaCell, constants.cella, constants.cellb, constants.cellc);
  double3x3 inverseCell = inputs.replicaCell;
  writeMatrixRows(double3x3::inverse(inverseCell), constants.inverseCella, constants.inverseCellb,
                  constants.inverseCellc);
  constants.replicaCorrection[0] = float(inputs.correction.x);
  constants.replicaCorrection[1] = float(inputs.correction.y);
  constants.replicaCorrection[2] = float(inputs.correction.z);
  constants.isovalue = isovalue;

  resetCommandList();
  StagedUpload posStaged = recordUpload(inputs.atomPositions.data(),
                                        inputs.atomPositions.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload parStaged = recordUpload(inputs.potentialParameters.data(),
                                        inputs.potentialParameters.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload repStaged = recordUpload(inputs.replicas.data(), inputs.replicas.size() * sizeof(float),
                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload pocketStaged = recordUpload(inputs.blockingPockets.data(),
                                           inputs.blockingPockets.size() * sizeof(float),
                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  StagedUpload vboStaged = recordUpload(triangleData.data(), triangleData.size() * sizeof(float4),
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!posStaged.resource || !parStaged.resource || !repStaged.resource || !pocketStaged.resource
      || !vboStaged.resource)
    throw std::runtime_error("SKComputeWellField: refinement buffer creation failed");
  executeAndWait();

  D3D12_CPU_DESCRIPTOR_HANDLE cpu = _refineHeap->GetCPUDescriptorHandleForHeapStart();
  createBufferSrv(_device.Get(), posStaged.resource.Get(), static_cast<UINT>(inputs.numberOfAtoms),
                  sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), parStaged.resource.Get(), static_cast<UINT>(inputs.numberOfAtoms),
                  sizeof(float) * 2, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), repStaged.resource.Get(), static_cast<UINT>(inputs.numberOfReplicas),
                  sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferSrv(_device.Get(), pocketStaged.resource.Get(),
                  static_cast<UINT>(inputs.blockingPockets.size() / 4), sizeof(float) * 4, cpu);
  cpu.ptr += _descriptorSize;
  createBufferUav(_device.Get(), vboStaged.resource.Get(), static_cast<UINT>(triangleData.size()),
                  sizeof(float4), cpu);

  resetCommandList();
  _commandList->SetPipelineState(_refinePso.Get());
  _commandList->SetComputeRootSignature(_refineRootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _refineHeap.Get() };
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetComputeRootDescriptorTable(0, _refineHeap->GetGPUDescriptorHandleForHeapStart());
  _commandList->SetComputeRoot32BitConstants(1, sizeof(RefineConstants) / 4, &constants, 0);
  _commandList->Dispatch(static_cast<UINT>((numberOfVertices + kThreadGroupSize - 1) / kThreadGroupSize), 1, 1);
  executeAndWait();

  readbackBuffer(vboStaged.resource.Get(), triangleData.data(), triangleData.size() * sizeof(float4),
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void SKComputeWellField::refineWellSurfaceVerticesCPUImplementation(std::vector<float4> &triangleData,
                                                                    const FieldInputs &inputs,
                                                                    float isovalue) noexcept
{
  const size_t numberOfVertices = triangleData.size() / 3;
  const double3x3 cell = inputs.replicaCell;
  double3x3 cellCopy = cell;
  const double3x3 inverseCell = double3x3::inverse(cellCopy);

  auto potentialEnergy = [&](const double3 &point) {
    double value = 0.0;
    for (size_t j = 0; j < inputs.numberOfReplicas; ++j)
    {
      const double3 replica(inputs.replicas[4 * j], inputs.replicas[4 * j + 1], inputs.replicas[4 * j + 2]);
      for (size_t iatom = 0; iatom < inputs.numberOfAtoms; ++iatom)
      {
        const double3 atom(inputs.atomPositions[4 * iatom], inputs.atomPositions[4 * iatom + 1],
                           inputs.atomPositions[4 * iatom + 2]);
        double3 ds = point - atom - replica;
        ds.x -= std::rint(ds.x);
        ds.y -= std::rint(ds.y);
        ds.z -= std::rint(ds.z);
        const double3 dr = cell * ds;
        const double rr = std::max(double3::dot(dr, dr), 1.0e-8);
        if (rr < 12.0 * 12.0)
        {
          const double sigma = double(inputs.potentialParameters[2 * iatom + 1]);
          const double sigma2rr = sigma * sigma / rr;
          const double rri3 = sigma2rr * sigma2rr * sigma2rr;
          value += double(inputs.potentialParameters[2 * iatom]) * (rri3 * (rri3 - 1.0));
        }
      }
    }
    return value;
  };

  const unsigned threadCount = std::max(1u, std::thread::hardware_concurrency());
  std::atomic<size_t> nextVertex{0};
  std::vector<std::thread> workers;
  workers.reserve(threadCount);

  auto refineVertex = [&](size_t vertexId) {
    // Twin copies of a shared vertex can differ by an ulp across cubes; quantizing the inputs welds them
    // bitwise, so they refine identically and the mesh stays watertight.
    const float4 &stored = triangleData[3 * vertexId];
    const double3 quantized(std::rint(double(stored.x) * 1048576.0) / 1048576.0,
                            std::rint(double(stored.y) * 1048576.0) / 1048576.0,
                            std::rint(double(stored.z) * 1048576.0) / 1048576.0);
    const double3 point = quantized * inputs.correction;

    // Vertices on or near the trim cap lie on the U = iso isosurface, not the well sheet: skip them.
    const double energyHere = potentialEnergy(point);
    if (energyHere > double(isovalue) - 0.02 * std::fabs(double(isovalue)))
      return;

    double nearest = 1.0e10;
    for (size_t j = 0; j < inputs.numberOfReplicas; ++j)
    {
      const double3 replica(inputs.replicas[4 * j], inputs.replicas[4 * j + 1], inputs.replicas[4 * j + 2]);
      for (size_t iatom = 0; iatom < inputs.numberOfAtoms; ++iatom)
      {
        const double3 atom(inputs.atomPositions[4 * iatom], inputs.atomPositions[4 * iatom + 1],
                           inputs.atomPositions[4 * iatom + 2]);
        double3 ds = point - atom - replica;
        ds.x -= std::rint(ds.x);
        ds.y -= std::rint(ds.y);
        ds.z -= std::rint(ds.z);
        nearest = std::min(nearest, (cell * ds).length()
                                        - contactOptimum * double(inputs.potentialParameters[2 * iatom + 1]));
      }
    }

    // A blocking pocket has no potential, so there is no well floor to slide onto against one.
    if (blockingPocketDistance(point, inputs.blockingPockets, inputs.numberOfBlockingPockets, cell,
                               inputs.correction) < nearest)
      return;

    double3 directionSum(0.0, 0.0, 0.0);
    double weightSum = 0.0;
    for (size_t j = 0; j < inputs.numberOfReplicas; ++j)
    {
      const double3 replica(inputs.replicas[4 * j], inputs.replicas[4 * j + 1], inputs.replicas[4 * j + 2]);
      for (size_t iatom = 0; iatom < inputs.numberOfAtoms; ++iatom)
      {
        const double3 atom(inputs.atomPositions[4 * iatom], inputs.atomPositions[4 * iatom + 1],
                           inputs.atomPositions[4 * iatom + 2]);
        double3 ds = point - atom - replica;
        ds.x -= std::rint(ds.x);
        ds.y -= std::rint(ds.y);
        ds.z -= std::rint(ds.z);
        const double3 dr = cell * ds;
        const double r = dr.length();
        if (!(r > 1.0e-6))
          continue;
        const double weighted = r - contactOptimum * double(inputs.potentialParameters[2 * iatom + 1]);
        if (weighted - nearest > 6.0 * softminTemperature)
          continue;
        const double w = std::exp(-(weighted - nearest) / softminTemperature);
        directionSum = directionSum + (-w / r) * dr;
        weightSum += w;
      }
    }
    if (!(weightSum > 0.0))
      return;

    // 1 against one wall, 0 where opposing walls cancel. There the 1D minimum is sideways rather than into
    // the wall, so the search span is scaled down with it, holding those vertices near the contact surface.
    const double reliability = directionSum.length() / weightSum;
    const double t = std::clamp((reliability - 0.25) / (0.6 - 0.25), 0.0, 1.0);
    const double span = 0.7 * (t * t * (3.0 - 2.0 * t));
    if (span < 0.05)
      return;
    const double3 rayFractional = inverseCell * double3::normalize(directionSum);

    const int coarse = 14;
    double sBest = 0.0;
    double uBest = energyHere;
    for (int i = -coarse; i <= coarse; ++i)
    {
      const double s = span * double(i) / double(coarse);
      const double u = potentialEnergy(point + s * rayFractional);
      if (u < uBest)
      {
        uBest = u;
        sBest = s;
      }
    }
    // An interior minimum only; if the best sample is an endpoint the floor is out of reach.
    if (std::fabs(sBest) >= span - 0.5 * span / double(coarse))
      return;

    const double invphi = 0.6180339887;
    double a = sBest - span / double(coarse);
    double b = sBest + span / double(coarse);
    double x1 = b - invphi * (b - a);
    double x2 = a + invphi * (b - a);
    double f1 = potentialEnergy(point + x1 * rayFractional);
    double f2 = potentialEnergy(point + x2 * rayFractional);
    for (int iteration = 0; iteration < 20; ++iteration)
    {
      if (f1 < f2)
      {
        b = x2; x2 = x1; f2 = f1;
        x1 = b - invphi * (b - a);
        f1 = potentialEnergy(point + x1 * rayFractional);
      }
      else
      {
        a = x1; x1 = x2; f1 = f2;
        x2 = a + invphi * (b - a);
        f2 = potentialEnergy(point + x2 * rayFractional);
      }
    }
    const double s = 0.5 * (a + b);
    const double3 refined = point + s * rayFractional;
    triangleData[3 * vertexId] = float4(float(refined.x / inputs.correction.x),
                                        float(refined.y / inputs.correction.y),
                                        float(refined.z / inputs.correction.z), 1.0f);
  };

  for (unsigned t = 0; t < threadCount; ++t)
  {
    workers.emplace_back([&]() {
      for (;;)
      {
        const size_t vertexId = nextVertex.fetch_add(1, std::memory_order_relaxed);
        if (vertexId >= numberOfVertices)
          break;
        refineVertex(vertexId);
      }
    });
  }
  for (std::thread &worker : workers)
    worker.join();
}

const std::string SKComputeWellField::_wellFieldKernel = R"foo(
cbuffer Constants : register(b0)
{
  int numberOfReplicas;
  int startIndexAtoms;
  int endIndexAtoms;
  int numberOfBlockingPockets;
  float4 cella;
  float4 cellb;
  float4 cellc;
  float4 replicaCorrection;
};

StructuredBuffer<float4> atomPosition : register(t0);
StructuredBuffer<float4> gridPosition : register(t1);
StructuredBuffer<float2> potparameters : register(t2);
StructuredBuffer<float4> replicas : register(t3);
StructuredBuffer<float4> blockingPockets : register(t4);
RWStructuredBuffer<float2> accumulated : register(u0);
RWStructuredBuffer<float4> softmin : register(u1);

static const float overlapEnergy = 10000000.0f;
static const float blockedEnergyPerAngstrom = 1000.0f;
static const float contactOptimum = 1.12246204831f;
static const float tau = 0.4f;

float3 toCartesian(float3 t)
{
  return float3(dot(cella.xyz, t), dot(cellb.xyz, t), dot(cellc.xyz, t));
}

float blockingPocketDistance(float3 gridpos)
{
  float nearest = 1.0e10f;
  const float3 unitCellPosition = gridpos / replicaCorrection.xyz;
  for (int i = 0; i < numberOfBlockingPockets; ++i)
  {
    float3 ds = unitCellPosition - blockingPockets[i].xyz;
    ds -= round(ds);
    nearest = min(nearest, length(toCartesian(ds * replicaCorrection.xyz)) - blockingPockets[i].w);
  }
  return nearest;
}

// Batched over atoms like the energy grid, accumulating one float2 per grid point: (U, d), plus one float4:
// the softmin-weighted sum of unit vectors toward the atoms and the sum of the weights. The ratio
// |sum| / weightSum is the medial reliability: 1 against a single wall, 0 on the medial axis of a channel
// where opposing walls cancel. Its low set marks where the channel is too narrow for the contact sheet and
// the well degenerates into a 1D filament.
[numthreads(64, 1, 1)]
void ComputeWellFieldGrid(uint3 dtid : SV_DispatchThreadID)
{
  uint igrid = dtid.x;
  float value = 0.0f;
  float distance = 1.0e10f;
  float3 directionSum = float3(0.0f, 0.0f, 0.0f);
  float weightSum = 0.0f;

  float3 gridpos = gridPosition[igrid].xyz;

  // A blocking pocket closes the surface the way a sphere of framework would: its own signed distance joins
  // the minimum that the contact surface is the zero set of, which wraps the surface smoothly around the
  // sphere, and its interior is an overlap so the depth trim discards it as well. Because the energy inside
  // a pocket ramps at the reciprocal of the scale the trim converts the distance with, the two agree there
  // and the combined field stays a distance to the sphere.
  const float pocket = blockingPocketDistance(gridpos);

  for (int j = 0; j < numberOfReplicas; ++j)
  {
    float3 replica = replicas[j].xyz;
    for (int iatom = startIndexAtoms; iatom < endIndexAtoms; ++iatom)
    {
      float3 ds = (gridpos - atomPosition[iatom].xyz) - replica;
      ds -= round(ds);
      float3 dr = toCartesian(ds);

      // An atom sitting exactly on a grid point would make r*r zero and the energy NaN; a floor of 1e-8 puts
      // such a point deep inside the repulsive core instead.
      float rr = max(dot(dr, dr), 1.0e-8f);
      if (rr < 12.0f * 12.0f)
      {
        float size = potparameters[iatom].y;
        float temp = size * size / rr;
        float rri3 = temp * temp * temp;
        value += potparameters[iatom].x * (rri3 * (rri3 - 1.0f));

        float r = sqrt(rr);
        float contact = r - contactOptimum * size;
        distance = min(distance, contact);
        float w = exp(-contact / tau);
        directionSum += w * (-dr / r);   // toward the atom
        weightSum += w;
      }
    }
  }

  float2 previous = accumulated[igrid];
  const float energy = pocket < 0.0f ? min(-pocket * blockedEnergyPerAngstrom, overlapEnergy)
                                     : previous.x + min(value, overlapEnergy);
  accumulated[igrid] = float2(energy, min(min(previous.y, distance), pocket));
  float4 previousSoftmin = softmin[igrid];
  softmin[igrid] = float4(previousSoftmin.xyz + directionSum, previousSoftmin.w + weightSum);
}
)foo";

const std::string SKComputeWellField::_refineKernel = R"foo(
cbuffer Constants : register(b0)
{
  int numberOfAtoms;
  int numberOfReplicas;
  int numberOfVertices;
  int numberOfBlockingPockets;
  float4 cella;
  float4 cellb;
  float4 cellc;
  float4 inverseCella;
  float4 inverseCellb;
  float4 inverseCellc;
  float4 replicaCorrection;
  float isovalue;
  float3 pad;
};

StructuredBuffer<float4> atomPosition : register(t0);
StructuredBuffer<float2> potparameters : register(t1);
StructuredBuffer<float4> replicas : register(t2);
StructuredBuffer<float4> blockingPockets : register(t3);
// Three float4 per vertex: position (unit-cell fractional), normal, pad.
RWStructuredBuffer<float4> VBOBuffer : register(u0);

static const float contactOptimum = 1.12246204831f;
static const float tau = 0.4f;

float3 toCartesian(float3 t)
{
  return float3(dot(cella.xyz, t), dot(cellb.xyz, t), dot(cellc.xyz, t));
}

float3 toFractional(float3 v)
{
  return float3(dot(inverseCella.xyz, v), dot(inverseCellb.xyz, v), dot(inverseCellc.xyz, v));
}

float blockingPocketDistance(float3 position)
{
  float nearest = 1.0e10f;
  const float3 unitCellPosition = position / replicaCorrection.xyz;
  for (int i = 0; i < numberOfBlockingPockets; ++i)
  {
    float3 ds = unitCellPosition - blockingPockets[i].xyz;
    ds -= round(ds);
    nearest = min(nearest, length(toCartesian(ds * replicaCorrection.xyz)) - blockingPockets[i].w);
  }
  return nearest;
}

// U at one point of the grid, in replica-cell fractional coordinates. HLSL reserves "point" as a
// primitive-type modifier, so the point being evaluated is named "position" throughout this kernel.
float wellPotentialEnergy(float3 position)
{
  float value = 0.0f;
  for (int j = 0; j < numberOfReplicas; ++j)
  {
    float3 replica = replicas[j].xyz;
    for (int iatom = 0; iatom < numberOfAtoms; ++iatom)
    {
      float3 ds = (position - atomPosition[iatom].xyz) - replica;
      ds -= round(ds);
      float3 dr = toCartesian(ds);
      float rr = max(dot(dr, dr), 1.0e-8f);
      if (rr < 12.0f * 12.0f)
      {
        float temp = potparameters[iatom].y * potparameters[iatom].y / rr;
        float rri3 = temp * temp * temp;
        value += potparameters[iatom].x * (rri3 * (rri3 - 1.0f));
      }
    }
  }
  return value;
}

// Marching cubes puts the vertices on the Apollonius surface d = 0, the single-atom contact optimum. The
// true well floor deviates from it where several atoms contribute --- deeper and slightly shifted --- so
// each vertex is slid into the wall to the minimum of the exact analytic U on that line.
//
// The direction is the softmin-weighted average of the unit vectors toward the nearby atoms, a smoothed
// gradient of the distance field. Three properties make it the right choice: it depends only on the
// (quantized) position, so shared vertices refine bitwise identically and the mesh stays watertight; it is
// continuous across the Apollonius creases (two atoms equally near), where the ray to the single nearest
// atom would tear neighbours apart; and its magnitude reports reliability --- between opposing walls the
// contributions cancel, which is exactly where a big probe's surface runs close to the medial ridge of a
// narrow channel and any fixed search line turns near-tangent to the sheet. There the 1D minimum is sideways
// rather than into the wall, and following it sprays spikes; instead the search span is scaled down with the
// reliability, holding those vertices near the contact surface.
[numthreads(64, 1, 1)]
void RefineWellSurfaceVertices(uint3 dtid : SV_DispatchThreadID)
{
  uint vertexId = dtid.x;
  if (vertexId >= (uint)numberOfVertices) return;

  // Twin copies of a shared vertex can differ by an ulp across cubes; quantizing the inputs welds them
  // bitwise, so they refine identically and the mesh stays watertight.
  const float3 quantized = round(VBOBuffer[3 * vertexId].xyz * 1048576.0f) / 1048576.0f;
  const float3 position = quantized * replicaCorrection.xyz;

  // Vertices on or near the trim cap lie on the U = iso isosurface, not the well sheet: skip them.
  const float energyHere = wellPotentialEnergy(position);
  if (energyHere > isovalue - 0.02f * abs(isovalue)) return;

  float nearest = 1.0e10f;
  for (int j = 0; j < numberOfReplicas; ++j)
  {
    float3 replica = replicas[j].xyz;
    for (int iatom = 0; iatom < numberOfAtoms; ++iatom)
    {
      float3 ds = (position - atomPosition[iatom].xyz) - replica;
      ds -= round(ds);
      nearest = min(nearest, length(toCartesian(ds)) - contactOptimum * potparameters[iatom].y);
    }
  }

  // A blocking pocket has no potential, so there is no well floor to slide onto against one. Vertices whose
  // closest wall is a pocket rather than an atom are the ones on its sphere, and they are left where the
  // distance field put them.
  if (blockingPocketDistance(position) < nearest) return;

  float3 directionSum = float3(0.0f, 0.0f, 0.0f);
  float weightSum = 0.0f;
  for (int jj = 0; jj < numberOfReplicas; ++jj)
  {
    float3 replica = replicas[jj].xyz;
    for (int iatom = 0; iatom < numberOfAtoms; ++iatom)
    {
      float3 ds = (position - atomPosition[iatom].xyz) - replica;
      ds -= round(ds);
      float3 dr = toCartesian(ds);
      const float r = length(dr);
      if (!(r > 1.0e-6f)) continue;
      const float weighted = r - contactOptimum * potparameters[iatom].y;
      if (weighted - nearest > 6.0f * tau) continue;
      const float w = exp(-(weighted - nearest) / tau);
      directionSum += w * (-dr / r);   // toward the atom
      weightSum += w;
    }
  }
  if (!(weightSum > 0.0f)) return;
  const float reliability = length(directionSum) / weightSum;   // 1 one wall, 0 opposing walls cancelling
  const float span = 0.7f * smoothstep(0.25f, 0.6f, reliability);
  if (span < 0.05f) return;
  const float3 rayFractional = toFractional(normalize(directionSum));   // fractional step per angstrom

  // Bracket the 1D minimum of U along the ray by coarse sampling of s in [-span, span].
  const int coarse = 14;
  float sBest = 0.0f;
  float uBest = energyHere;
  for (int i = -coarse; i <= coarse; ++i)
  {
    const float s = span * float(i) / float(coarse);
    const float u = wellPotentialEnergy(position + s * rayFractional);
    if (u < uBest) { uBest = u; sBest = s; }
  }
  // An interior minimum only; if the best position is an endpoint the floor is out of reach, leave the vertex.
  if (abs(sBest) >= span - 0.5f * span / float(coarse)) return;

  // Golden-section on the bracketing interval. Enough iterations that the final interval
  // (0.05 A * 0.618^n) is well under 1e-5 A: twin copies of a shared vertex differ by an ulp across cubes,
  // and the search tolerance bounds how far they can split.
  const float invphi = 0.6180339887f;
  float a = sBest - span / float(coarse);
  float b = sBest + span / float(coarse);
  float x1 = b - invphi * (b - a);
  float x2 = a + invphi * (b - a);
  float f1 = wellPotentialEnergy(position + x1 * rayFractional);
  float f2 = wellPotentialEnergy(position + x2 * rayFractional);
  for (int iteration = 0; iteration < 20; ++iteration)
  {
    if (f1 < f2)
    {
      b = x2; x2 = x1; f2 = f1;
      x1 = b - invphi * (b - a);
      f1 = wellPotentialEnergy(position + x1 * rayFractional);
    }
    else
    {
      a = x1; x1 = x2; f1 = f2;
      x2 = a + invphi * (b - a);
      f2 = wellPotentialEnergy(position + x2 * rayFractional);
    }
  }
  const float s = 0.5f * (a + b);

  VBOBuffer[3 * vertexId] = float4((position + s * rayFractional) / replicaCorrection.xyz, 1.0f);
}
)foo";
