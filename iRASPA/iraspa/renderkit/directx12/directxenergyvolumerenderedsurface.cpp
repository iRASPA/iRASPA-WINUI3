/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxenergyvolumerenderedsurface.h"
#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/cubegeometry.h"
#include "geometry/backplanegeometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr UINT kTransferFunctionWidth = 256;
constexpr UINT kTransferFunctionLayers = 23;
constexpr DXGI_FORMAT kVolumeFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

void volumeLog(const std::string &msg)
{
  std::cerr << msg << '\n';
  char tempPath[MAX_PATH] = {};
  if (GetTempPathA(MAX_PATH, tempPath) == 0)
    return;
  const std::string path = std::string(tempPath) + "iraspa_volume.log";
  std::ofstream out(path, std::ios::app);
  if (out)
    out << msg << '\n';
}
}

DirectXEnergyVolumeRenderedSurface::~DirectXEnergyVolumeRenderedSurface()
{
  for (RKCache<RKRenderObject *, std::vector<float4>> &cache : _caches)
    cache.clear();
  if (_fenceEvent)
  {
    CloseHandle(_fenceEvent);
    _fenceEvent = nullptr;
  }
}

void DirectXEnergyVolumeRenderedSurface::loadShader(ID3D12Device * /*device*/)
{
}

UINT DirectXEnergyVolumeRenderedSurface::flatStructureCount() const
{
  UINT count = 0;
  for (const auto &movie : _renderStructures)
    count += static_cast<UINT>(movie.size());
  return count;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXEnergyVolumeRenderedSurface::cpuHandle(UINT descriptorIndex) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(descriptorIndex) * _srvDescriptorSize;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXEnergyVolumeRenderedSurface::gpuHandle(UINT descriptorIndex) const
{
  D3D12_GPU_DESCRIPTOR_HANDLE handle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += SIZE_T(descriptorIndex) * _srvDescriptorSize;
  return handle;
}

void DirectXEnergyVolumeRenderedSurface::ensureUploadInfrastructure(ID3D12Device *device)
{
  if (_commandAllocator && _commandList && _fence && _fenceEvent)
    return;

  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_commandAllocator))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create command allocator";
    return;
  }
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _commandAllocator.Get(), nullptr,
                                       IID_PPV_ARGS(&_commandList))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create command list";
    return;
  }
  _commandList->Close();

  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create fence";
    return;
  }
  _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DirectXEnergyVolumeRenderedSurface::executeAndWait(ID3D12CommandQueue *queue)
{
  if (!_commandList || !queue || !_fence || !_fenceEvent)
    return;

  _commandList->Close();
  ID3D12CommandList *lists[] = { _commandList.Get() };
  queue->ExecuteCommandLists(1, lists);

  const UINT64 fenceToWait = ++_fenceValue;
  queue->Signal(_fence.Get(), fenceToWait);
  if (_fence->GetCompletedValue() < fenceToWait)
  {
    _fence->SetEventOnCompletion(fenceToWait, _fenceEvent);
    WaitForSingleObject(_fenceEvent, INFINITE);
  }
}

void DirectXEnergyVolumeRenderedSurface::createVolumeRootSignature(ID3D12Device *device)
{
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 3;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;
  srvRange.OffsetInDescriptorsFromTableStart = 0;

  // Match scene CBV root indices: 0=b0 frame, 1=b1 structure, 2=b3 lights, 4=b2 isosurface.
  // Root 3 = SRV table (t0 volume, t1 transfer function, t2 scene depth).
  D3D12_ROOT_PARAMETER params[5] = {};
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

  D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
  // s0: volume — linear + wrap (matches OpenGL GL_REPEAT)
  samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].ShaderRegister = 0;
  samplers[0].RegisterSpace = 0;
  samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // s1: transfer function — linear + clamp
  samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplers[1].ShaderRegister = 1;
  samplers[1].RegisterSpace = 0;
  samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 5;
  rootDesc.pParameters = params;
  rootDesc.NumStaticSamplers = 2;
  rootDesc.pStaticSamplers = samplers;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
  {
    if (error)
      OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to serialize root signature";
    return;
  }

  if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                         IID_PPV_ARGS(&_rootSignature))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create root signature";
  }
}

void DirectXEnergyVolumeRenderedSurface::initializePSOs(ID3D12Device *device, DXGI_FORMAT rtvFormat,
                                                        DXGI_FORMAT dsvFormat)
{
  if (!_rootSignature)
  {
    volumeLog("initializePSOs aborted: root signature is null");
    return;
  }

  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
  {
    volumeLog(std::string("initializePSOs shader compile failed vs=") + (vs ? "ok" : "null")
              + " ps=" + (ps ? "ok" : "null")
              + " vsBytes=" + std::to_string(_vertexShaderSource.size())
              + " psBytes=" + std::to_string(_pixelShaderSource.size()));
    // Dump PS source head/tail for offline fxc diagnosis.
    {
      char tempPath[MAX_PATH] = {};
      if (GetTempPathA(MAX_PATH, tempPath))
      {
        const std::string path = std::string(tempPath) + "iraspa_volume_ps.hlsl";
        std::ofstream out(path, std::ios::binary);
        if (out)
          out.write(_pixelShaderSource.data(), static_cast<std::streamsize>(_pixelShaderSource.size()));
        volumeLog("wrote " + path);
      }
    }
    return;
  }

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  auto makePso = [&](bool opaque, ComPtr<ID3D12PipelineState> &outPso, const char *label) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = _rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // Match QT/Cocoa back-face cull (same cube strip).
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    psoDesc.RasterizerState.DepthClipEnable = FALSE; // GL_DEPTH_CLAMP
    // Opaque: depth test + write (QT LEQUAL). Occlusion vs atoms is also done in the ray
    // march; SV_DEPTH is clamped to the opaque-surface depth so walls are not discarded.
    // Transparent: no depth test (QT).
    psoDesc.DepthStencilState.DepthEnable = opaque ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask =
        opaque ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = dsvFormat;
    psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPso))))
    {
      volumeLog(std::string("DirectXEnergyVolumeRenderedSurface: failed to create") + label);
      return false;
    }
    return true;
  };

  _opaquePsoReady = makePso(true, _opaquePso, " opaque PSO");
  _transparentPsoReady = makePso(false, _transparentPso, " transparent PSO");
  if (_opaquePsoReady && _transparentPsoReady)
    volumeLog("DirectXEnergyVolumeRenderedSurface: volume PSOs ready");
  else
    volumeLog("DirectXEnergyVolumeRenderedSurface: volume PSO creation FAILED");
}

void DirectXEnergyVolumeRenderedSurface::initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat,
                                                    DXGI_FORMAT dsvFormat)
{
  if (!device)
    return;
  createVolumeRootSignature(device);
  initializePSOs(device, rtvFormat, dsvFormat);
  createDepthCopyPipeline(device);
  ensureUploadInfrastructure(device);
}

void DirectXEnergyVolumeRenderedSurface::invalidateIsosurface(
    std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  for (const std::shared_ptr<RKRenderObject> &structure : structures)
  {
    for (RKCache<RKRenderObject *, std::vector<float4>> &cache : _caches)
      cache.remove(structure.get());
  }
}

void DirectXEnergyVolumeRenderedSurface::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXEnergyVolumeRenderedSurface::deleteBuffers()
{
  _buffers.clear();
  _srvHeap.Reset();
}

void DirectXEnergyVolumeRenderedSurface::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXEnergyVolumeRenderedSurface::initializeTransferFunctionTexture(
    ID3D12Device *device, ID3D12CommandQueue *commandQueue)
{
  if (!device || !commandQueue)
    return;
  ensureUploadInfrastructure(device);
  if (!_commandAllocator || !_commandList)
    return;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
  texDesc.Width = kTransferFunctionWidth;
  texDesc.Height = 1;
  texDesc.DepthOrArraySize = kTransferFunctionLayers;
  texDesc.MipLevels = 1;
  texDesc.Format = kVolumeFormat;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  _transferFunctionTexture.Reset();
  if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&_transferFunctionTexture))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create TF texture";
    return;
  }

  const UINT numSubresources = kTransferFunctionLayers;
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
  std::vector<UINT> numRows(numSubresources);
  std::vector<UINT64> rowSizes(numSubresources);
  UINT64 uploadSize = 0;
  device->GetCopyableFootprints(&texDesc, 0, numSubresources, 0, layouts.data(), numRows.data(),
                                rowSizes.data(), &uploadSize);

  _transferFunctionUpload = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
  if (!_transferFunctionUpload)
    return;

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(_transferFunctionUpload->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return;

  const std::array<float4, 256> *tables[kTransferFunctionLayers] = {
    &RASPA_PES_TransferFunction, &CoolWarmTransferFunction, &XrayTransferFunction,
    &GrayTransferFunction, &RainbowTransferFunction, &TurboTransferFunction,
    &GnuplotTransferFunction, &SpectralTransferFunction, &CoolTransferFunction,
    &ViridisTransferFunction, &PlasmaTransferFunction, &InfernoTransferFunction,
    &MagmaTransferFunction, &CividisTransferFunction, &SpringTransferFunction,
    &SummerTransferFunction, &AutumnTransferFunction, &WinterTransferFunction,
    &RedsTransferFunction, &GreensTransferFunction, &BluesTransferFunction,
    &PurplesTransferFunction, &OrangesTransferFunction
  };

  for (UINT layer = 0; layer < numSubresources; ++layer)
  {
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &fp = layouts[layer];
    uint8_t *dst = mapped + fp.Offset;
    const float4 *src = tables[layer]->data();
    std::memcpy(dst, src, kTransferFunctionWidth * sizeof(float4));
  }
  _transferFunctionUpload->Unmap(0, nullptr);

  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);

  for (UINT layer = 0; layer < numSubresources; ++layer)
  {
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = _transferFunctionTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = layer;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = _transferFunctionUpload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = layouts[layer];

    _commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = _transferFunctionTexture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  _commandList->ResourceBarrier(1, &barrier);

  executeAndWait(commandQueue);
}

void DirectXEnergyVolumeRenderedSurface::reloadData(ID3D12Device *device, ID3D12CommandQueue *commandQueue)
{
  if (!device || !commandQueue)
  {
    volumeLog("reloadData skipped: null device/queue");
    return;
  }

  volumeLog("reloadData begin structures=" + std::to_string(_renderStructures.size()));

  // Drop cached grids so quality / method / data changes rebuild the 3D texture.
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      if (_renderStructures[i][j])
      {
        for (RKCache<RKRenderObject *, std::vector<float4>> &cache : _caches)
          cache.remove(_renderStructures[i][j].get());
      }
    }
  }

  ensureUploadInfrastructure(device);
  ensureFarDepthTexture(device, commandQueue);
  initializeTransferFunctionTexture(device, commandQueue);
  initializeVertexBuffers(device, commandQueue);
  volumeLog(std::string("reloadData end psoOpaque=") + (_opaquePsoReady ? "1" : "0")
            + " psoTransparent=" + (_transparentPsoReady ? "1" : "0")
            + " srvHeap=" + (_srvHeap ? "1" : "0"));
}

void DirectXEnergyVolumeRenderedSurface::initializeVertexBuffers(ID3D12Device *device,
                                                                 ID3D12CommandQueue *commandQueue)
{
  if (!device || !commandQueue || !_commandAllocator || !_commandList)
    return;

  const UINT structureCount = flatStructureCount();
  const UINT descriptorCount = std::max(1u, structureCount * 3u);

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = descriptorCount;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  _srvHeap.Reset();
  if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create SRV heap";
    return;
  }
  _srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  CubeGeometry cube;
  const auto &vertices = cube.vertices();
  const auto &indices = cube.indices();
  const size_t vbBytes = vertices.size() * sizeof(RKVertex);
  const size_t ibBytes = indices.size() * sizeof(short);

  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);
  bool anyCopy = false;
  UINT flatIndex = 0;

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      MeshBuffers &bufs = _buffers[i][j];
      bufs = MeshBuffers{};

      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      if (source)
      {
        volumeLog("structure[" + std::to_string(i) + "][" + std::to_string(j)
                  + "] draw=" + std::to_string(source->drawAdsorptionSurface() ? 1 : 0)
                  + " method=" + std::to_string(static_cast<int>(source->adsorptionSurfaceRenderingMethod()))
                  + " tf=" + std::to_string(static_cast<int>(source->adsorptionVolumeTransferFunction())));
      }
      if (!source
          || !source->drawAdsorptionSurface()
          || source->adsorptionSurfaceRenderingMethod() != RKEnergySurfaceType::volumeRendering)
      {
        if (source && source->drawAdsorptionSurface()
            && source->adsorptionSurfaceRenderingMethod() != RKEnergySurfaceType::volumeRendering)
        {
          volumeLog("DirectXEnergyVolumeRenderedSurface: skip structure (method != volumeRendering)");
        }
        ++flatIndex;
        continue;
      }

      const int3 dimensionsHint = source->dimensions();
      const int largestSizeHint = std::max({dimensionsHint.x, dimensionsHint.y, dimensionsHint.z});
      int powerOfTwo = 1;
      while (largestSizeHint > static_cast<int>(std::pow(2, powerOfTwo)))
        powerOfTwo += 1;
      if (powerOfTwo < 0 || powerOfTwo >= static_cast<int>(_caches.size()))
        powerOfTwo = static_cast<int>(_caches.size()) - 1;
      int size = static_cast<int>(std::pow(2, powerOfTwo));

      std::vector<float4> *textureData = nullptr;
      const bool cached = _caches[powerOfTwo].contains(_renderStructures[i][j].get());
      if (cached)
      {
        textureData = _caches[powerOfTwo].object(_renderStructures[i][j].get());
      }
      else
      {
        std::vector<float4> gridData = source->gridValueAndGradientData();
        if (gridData.empty())
        {
          volumeLog("DirectXEnergyVolumeRenderedSurface: empty gridValueAndGradientData");
          ++flatIndex;
          continue;
        }
        const int3 dimensions = source->dimensions();
        const int largestSize = std::max({dimensions.x, dimensions.y, dimensions.z});
        powerOfTwo = 1;
        while (largestSize > static_cast<int>(std::pow(2, powerOfTwo)))
          powerOfTwo += 1;
        if (powerOfTwo < 0 || powerOfTwo >= static_cast<int>(_caches.size()))
          powerOfTwo = static_cast<int>(_caches.size()) - 1;
        size = static_cast<int>(std::pow(2, powerOfTwo));
        textureData = new std::vector<float4>(std::move(gridData));
      }

      bufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbBytes, 1));
      bufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibBytes, 1));
      if (!vertices.empty())
        DirectXDeviceHelpers::writeUploadBuffer(bufs.vertexBuffer.Get(), vertices.data(), vbBytes);
      if (!indices.empty())
        DirectXDeviceHelpers::writeUploadBuffer(bufs.indexBuffer.Get(), indices.data(), ibBytes);
      bufs.vbv = { bufs.vertexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(std::max<size_t>(vbBytes, 1)),
                   static_cast<UINT>(sizeof(RKVertex)) };
      bufs.ibv = { bufs.indexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(std::max<size_t>(ibBytes, 1)),
                   DXGI_FORMAT_R16_UINT };
      bufs.indexCount = static_cast<UINT>(indices.size());

      D3D12_RESOURCE_DESC texDesc = {};
      texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      texDesc.Width = static_cast<UINT64>(size);
      texDesc.Height = static_cast<UINT>(size);
      texDesc.DepthOrArraySize = static_cast<UINT16>(size);
      texDesc.MipLevels = 1;
      texDesc.Format = kVolumeFormat;
      texDesc.SampleDesc.Count = 1;
      texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

      D3D12_HEAP_PROPERTIES defaultHeap = {};
      defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
      if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&bufs.volumeTexture))))
      {
        std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create volume texture";
        if (!cached)
          delete textureData;
        ++flatIndex;
        continue;
      }

      D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
      UINT64 uploadSize = 0;
      device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, nullptr, nullptr, &uploadSize);
      bufs.volumeUpload = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
      if (!bufs.volumeUpload)
      {
        if (!cached)
          delete textureData;
        ++flatIndex;
        continue;
      }

      uint8_t *mapped = nullptr;
      D3D12_RANGE readRange = {0, 0};
      if (SUCCEEDED(bufs.volumeUpload->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
      {
        const UINT depthPitch = layout.Footprint.RowPitch * layout.Footprint.Height;
        const size_t expectedFloats = static_cast<size_t>(size) * size * size;
        float energyMin = 1e30f;
        float energyMax = -1e30f;
        size_t opaqueBins = 0;
        for (UINT z = 0; z < static_cast<UINT>(size); ++z)
        {
          for (UINT y = 0; y < static_cast<UINT>(size); ++y)
          {
            const size_t srcIndex = (static_cast<size_t>(z) * size + y) * size;
            uint8_t *dstRow = mapped + layout.Offset + z * depthPitch + y * layout.Footprint.RowPitch;
            if (srcIndex + size <= textureData->size() && srcIndex + size <= expectedFloats)
            {
              std::memcpy(dstRow, textureData->data() + srcIndex, size * sizeof(float4));
              for (int x = 0; x < size; ++x)
              {
                const float e = (*textureData)[srcIndex + static_cast<size_t>(x)].x;
                energyMin = (std::min)(energyMin, e);
                energyMax = (std::max)(energyMax, e);
                // RASPA_PES mid-range (~0.15–0.95) is the opaque wall band.
                if (e > 0.15f && e < 0.95f)
                  ++opaqueBins;
              }
            }
            else
              std::memset(dstRow, 0, size * sizeof(float4));
          }
        }
        bufs.volumeUpload->Unmap(0, nullptr);
        volumeLog("volume energy range=[" + std::to_string(energyMin) + "," + std::to_string(energyMax)
                  + "] opaqueBandVoxels=" + std::to_string(opaqueBins)
                  + "/" + std::to_string(expectedFloats));
      }

      D3D12_TEXTURE_COPY_LOCATION dst = {};
      dst.pResource = bufs.volumeTexture.Get();
      dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dst.SubresourceIndex = 0;

      D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
      srcLoc.pResource = bufs.volumeUpload.Get();
      srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      srcLoc.PlacedFootprint = layout;

      _commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = bufs.volumeTexture.Get();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      _commandList->ResourceBarrier(1, &barrier);
      anyCopy = true;

      D3D12_SHADER_RESOURCE_VIEW_DESC volumeSrv = {};
      volumeSrv.Format = kVolumeFormat;
      volumeSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      volumeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      volumeSrv.Texture3D.MipLevels = 1;
      volumeSrv.Texture3D.MostDetailedMip = 0;
      volumeSrv.Texture3D.ResourceMinLODClamp = 0.0f;
      device->CreateShaderResourceView(bufs.volumeTexture.Get(), &volumeSrv, cpuHandle(flatIndex * 3u));

      if (_transferFunctionTexture)
      {
        D3D12_SHADER_RESOURCE_VIEW_DESC tfSrv = {};
        tfSrv.Format = kVolumeFormat;
        tfSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        tfSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tfSrv.Texture1DArray.MostDetailedMip = 0;
        tfSrv.Texture1DArray.MipLevels = 1;
        tfSrv.Texture1DArray.FirstArraySlice = 0;
        tfSrv.Texture1DArray.ArraySize = kTransferFunctionLayers;
        tfSrv.Texture1DArray.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(_transferFunctionTexture.Get(), &tfSrv,
                                         cpuHandle(flatIndex * 3u + 1u));
      }

      bufs.volumeValid = true;
      volumeLog("DirectXEnergyVolumeRenderedSurface: volume texture ready size=" + std::to_string(size)
                + " indices=" + std::to_string(bufs.indexCount));

      if (!cached)
        _caches[powerOfTwo].insert(_renderStructures[i][j].get(), textureData);

      ++flatIndex;
    }
  }

  if (anyCopy)
    executeAndWait(commandQueue);
  else
    _commandList->Close();

  // Always bind a valid far-depth SRV so t2 is never an uninitialized descriptor.
  bindFarDepthToAllSlots(device);
}

void DirectXEnergyVolumeRenderedSurface::ensureFarDepthTexture(ID3D12Device *device,
                                                               ID3D12CommandQueue *commandQueue)
{
  if (!device || !commandQueue || _farDepthTexture)
    return;

  ensureUploadInfrastructure(device);
  if (!_commandAllocator || !_commandList)
    return;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = 1;
  texDesc.Height = 1;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R32_FLOAT;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                             IID_PPV_ARGS(&_farDepthTexture))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create far-depth texture";
    return;
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
  UINT64 uploadSize = 0;
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, nullptr, nullptr, &uploadSize);
  _farDepthUpload = DirectXDeviceHelpers::createUploadBuffer(device, uploadSize);
  if (!_farDepthUpload)
  {
    _farDepthTexture.Reset();
    return;
  }

  const float farDepth = 1.0f;
  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (SUCCEEDED(_farDepthUpload->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
  {
    std::memcpy(mapped + layout.Offset, &farDepth, sizeof(farDepth));
    _farDepthUpload->Unmap(0, nullptr);
  }

  _commandAllocator->Reset();
  _commandList->Reset(_commandAllocator.Get(), nullptr);

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = _farDepthTexture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = _farDepthUpload.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = layout;

  _commandList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = _farDepthTexture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  _commandList->ResourceBarrier(1, &barrier);

  executeAndWait(commandQueue);
}

void DirectXEnergyVolumeRenderedSurface::bindFarDepthToAllSlots(ID3D12Device *device)
{
  if (!device || !_srvHeap || !_farDepthTexture)
    return;
  bindSceneDepthSRV(device, _farDepthTexture.Get());
}

void DirectXEnergyVolumeRenderedSurface::createDepthCopyPipeline(ID3D12Device *device)
{
  if (!device || _depthCopyPso)
    return;

  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;

  D3D12_ROOT_PARAMETER param = {};
  param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  param.DescriptorTable.NumDescriptorRanges = 1;
  param.DescriptorTable.pDescriptorRanges = &srvRange;
  param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &param;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to serialize depth-copy root signature\n";
    return;
  }
  if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                         IID_PPV_ARGS(&_depthCopyRootSignature))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create depth-copy root signature\n";
    return;
  }

  const std::string vsSrc = R"foo(
struct VSInput { float4 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(VSInput input)
{
  VSOutput o;
  o.position = float4(input.position.xy, 0.0, 1.0);
  return o;
}
)foo";
  const std::string psSrc = R"foo(
Texture2D<float> depthTexture : register(t0);
struct PSInput { float4 position : SV_POSITION; };
float4 PSMain(PSInput input) : SV_TARGET
{
  float d = depthTexture.Load(int3(int2(input.position.xy), 0)).r;
  return float4(d, 0.0, 0.0, 1.0);
}
)foo";

  ComPtr<ID3DBlob> vs = compileShader(vsSrc, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(psSrc, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = _depthCopyRootSignature.Get();
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
  psoDesc.SampleDesc.Count = 1;

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_depthCopyPso))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create depth-copy PSO\n";
    return;
  }

  BackPlaneGeometry quad;
  const auto &vertices = quad.vertices();
  const size_t vbBytes = vertices.size() * sizeof(RKVertex);
  _depthCopyVb = DirectXDeviceHelpers::createUploadBuffer(device, vbBytes);
  if (_depthCopyVb)
  {
    DirectXDeviceHelpers::writeUploadBuffer(_depthCopyVb.Get(), vertices.data(), vbBytes);
    _depthCopyVbv = { _depthCopyVb->GetGPUVirtualAddress(),
                      static_cast<UINT>(vbBytes),
                      static_cast<UINT>(sizeof(RKVertex)) };
  }

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_sceneDepthCopyRtvHeap));

  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
  srvHeapDesc.NumDescriptors = 1;
  srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_depthCopySrcSrvHeap));
}

void DirectXEnergyVolumeRenderedSurface::ensureSceneDepthCopy(ID3D12Device *device, UINT width, UINT height)
{
  if (!device || width == 0 || height == 0)
    return;
  if (_sceneDepthCopy && _sceneDepthCopyWidth == width && _sceneDepthCopyHeight == height)
    return;

  _sceneDepthCopy.Reset();
  _sceneDepthCopyWidth = width;
  _sceneDepthCopyHeight = height;
  _sceneDepthCopyState = D3D12_RESOURCE_STATE_COPY_DEST;

  // Same typeless depth format as the live DSV so CopyResource stays bit-exact (like QT's
  // depth blit). Sampled as R32_FLOAT_X8X24_TYPELESS while the live DSV is rebound.
  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  clearValue.DepthStencil.Depth = 1.0f;
  clearValue.DepthStencil.Stencil = 0;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
  texDesc.SampleDesc.Count = 1;
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, &clearValue,
                                             IID_PPV_ARGS(&_sceneDepthCopy))))
  {
    std::cerr << "DirectXEnergyVolumeRenderedSurface: failed to create scene-depth copy\n";
    return;
  }
}

ID3D12Resource *DirectXEnergyVolumeRenderedSurface::copySceneDepthAndBind(
    ID3D12Device *device, ID3D12GraphicsCommandList *commandList, ID3D12Resource *sceneDepthResource)
{
  if (!device || !commandList || !sceneDepthResource || !_sceneDepthCopy)
  {
    bindFarDepthToAllSlots(device);
    return _farDepthTexture.Get();
  }

  // Caller left the live depth in DEPTH_READ | PIXEL_SHADER_RESOURCE with DSV unbound.
  D3D12_RESOURCE_BARRIER barriers[2] = {};
  barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[0].Transition.pResource = sceneDepthResource;
  barriers[0].Transition.StateBefore =
      D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[1].Transition.pResource = _sceneDepthCopy.Get();
  barriers[1].Transition.StateBefore = _sceneDepthCopyState;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  if (_sceneDepthCopyState == D3D12_RESOURCE_STATE_COPY_DEST)
    commandList->ResourceBarrier(1, barriers);
  else
    commandList->ResourceBarrier(2, barriers);
  _sceneDepthCopyState = D3D12_RESOURCE_STATE_COPY_DEST;

  commandList->CopyResource(_sceneDepthCopy.Get(), sceneDepthResource);

  barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barriers[0].Transition.StateAfter =
      D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  commandList->ResourceBarrier(2, barriers);
  _sceneDepthCopyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  bindSceneDepthSRV(device, _sceneDepthCopy.Get());
  return _sceneDepthCopy.Get();
}

void DirectXEnergyVolumeRenderedSurface::bindSceneDepthSRV(ID3D12Device *device,
                                                           ID3D12Resource *sceneDepthReadable)
{
  if (!device || !sceneDepthReadable || !_srvHeap)
    return;

  const D3D12_RESOURCE_DESC desc = sceneDepthReadable->GetDesc();
  D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
  if (desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS
      || desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT
      || desc.Format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  else
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
  depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depthSrv.Texture2D.MostDetailedMip = 0;
  depthSrv.Texture2D.MipLevels = 1;
  depthSrv.Texture2D.PlaneSlice = 0;
  depthSrv.Texture2D.ResourceMinLODClamp = 0.0f;

  const UINT structureCount = flatStructureCount();
  for (UINT i = 0; i < structureCount; ++i)
    device->CreateShaderResourceView(sceneDepthReadable, &depthSrv, cpuHandle(i * 3u + 2u));
}

bool DirectXEnergyVolumeRenderedSurface::needsSceneDepth() const
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      if (_renderStructures[i][j]
          && _renderStructures[i][j]->isVisible()
          && source
          && source->drawAdsorptionSurface()
          && source->adsorptionSurfaceRenderingMethod() == RKEnergySurfaceType::volumeRendering)
        return true;
    }
  }
  return false;
}

void DirectXEnergyVolumeRenderedSurface::paintCommon(
    ID3D12GraphicsCommandList *commandList, ID3D12PipelineState *pso, bool opaquePass,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBV, D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
    UINT structureCBVStride, D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase, UINT isosurfaceCBVStride,
    D3D12_GPU_VIRTUAL_ADDRESS lightsCBV)
{
  if (!commandList || !pso || !_rootSignature || !_srvHeap)
  {
    static bool logged = false;
    if (!logged)
    {
      volumeLog(std::string("paintCommon early-out pso=") + (pso ? "ok" : "null")
                + " rs=" + (_rootSignature ? "ok" : "null")
                + " heap=" + (_srvHeap ? "ok" : "null"));
      logged = true;
    }
    return;
  }

  commandList->SetGraphicsRootSignature(_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = { _srvHeap.Get() };
  commandList->SetDescriptorHeaps(1, heaps);
  commandList->SetGraphicsRootConstantBufferView(0, frameCBV);
  commandList->SetGraphicsRootConstantBufferView(2, lightsCBV);
  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  int drawCount = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      const MeshBuffers &bufs = _buffers[i][j];
      const bool visible = _renderStructures[i][j]
          && _renderStructures[i][j]->isVisible()
          && source
          && source->drawAdsorptionSurface()
          && source->adsorptionSurfaceRenderingMethod() == RKEnergySurfaceType::volumeRendering
          && bufs.volumeValid
          && bufs.indexCount > 0
          && bufs.vertexBuffer
          && bufs.indexBuffer;

      bool passMatch = false;
      if (visible)
      {
        const auto tf = source->adsorptionVolumeTransferFunction();
        if (opaquePass)
          passMatch = (tf == RKPredefinedVolumeRenderingTransferFunction::RASPA_PES);
        else
          passMatch = (tf != RKPredefinedVolumeRenderingTransferFunction::RASPA_PES);
      }

      if (passMatch)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->SetGraphicsRootConstantBufferView(
            4, isosurfaceCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * isosurfaceCBVStride);
        commandList->SetGraphicsRootDescriptorTable(3, gpuHandle(static_cast<UINT>(index) * 3u));
        commandList->IASetVertexBuffers(0, 1, &bufs.vbv);
        commandList->IASetIndexBuffer(&bufs.ibv);
        commandList->DrawIndexedInstanced(bufs.indexCount, 1, 0, 0, 0);
        ++drawCount;
      }
      ++index;
    }
  }

  static int paintLogCount = 0;
  if (paintLogCount < 8)
  {
    volumeLog(std::string("paintCommon ") + (opaquePass ? "opaque" : "transparent")
              + " draws=" + std::to_string(drawCount)
              + " structures=" + std::to_string(index));
    ++paintLogCount;
  }
}

void DirectXEnergyVolumeRenderedSurface::paintOpaque(
    ID3D12GraphicsCommandList *commandList, D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase, UINT structureCBVStride,
    D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase, UINT isosurfaceCBVStride,
    D3D12_GPU_VIRTUAL_ADDRESS lightsCBV)
{
  if (!_opaquePsoReady || !_opaquePso)
    return;
  paintCommon(commandList, _opaquePso.Get(), true, frameCBV, structureCBVBase, structureCBVStride,
              isosurfaceCBVBase, isosurfaceCBVStride, lightsCBV);
}

void DirectXEnergyVolumeRenderedSurface::paintTransparent(
    ID3D12GraphicsCommandList *commandList, D3D12_GPU_VIRTUAL_ADDRESS frameCBV,
    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase, UINT structureCBVStride,
    D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase, UINT isosurfaceCBVStride,
    D3D12_GPU_VIRTUAL_ADDRESS lightsCBV)
{
  if (!_transparentPsoReady || !_transparentPso)
    return;
  paintCommon(commandList, _transparentPso.Get(), false, frameCBV, structureCBVBase, structureCBVStride,
              isosurfaceCBVBase, isosurfaceCBVStride, lightsCBV);
}

const std::string DirectXEnergyVolumeRenderedSurface::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::IsosurfaceUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 UV : TEXCOORD0;
  float3 worldPosition : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = mul(structureUniforms.modelMatrix, mul(structureUniforms.boxMatrix, input.vertexPosition));
  output.worldPosition = pos.xyz;
  output.UV = input.vertexPosition.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, pos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXEnergyVolumeRenderedSurface::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::IsosurfaceUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
Texture3D<float4> volume : register(t0);
Texture1DArray<float4> transferFunction : register(t1);
Texture2D<float> depthTexture : register(t2);
SamplerState volumeSampler : register(s0);
SamplerState transferFunctionSampler : register(s1);

struct PSInput
{
  float4 position : SV_POSITION;
  float3 UV : TEXCOORD0;
  float3 worldPosition : TEXCOORD1;
};

struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_DEPTH;
};

struct Ray
{
  float3 origin;
  float3 direction;
};

struct AABB
{
  float3 top;
  float3 bottom;
};

void ray_box_intersection(Ray ray, AABB box, out float t_0, out float t_1)
{
  float3 direction_inv = 1.0 / ray.direction;
  float3 t_top = direction_inv * (box.top - ray.origin);
  float3 t_bottom = direction_inv * (box.bottom - ray.origin);
  float3 t_min = min(t_top, t_bottom);
  float2 t = max(t_min.xx, t_min.yz);
  t_0 = max(0.0, max(t.x, t.y));
  float3 t_max = max(t_top, t_bottom);
  t = min(t_max.xx, t_max.yz);
  t_1 = min(t.x, t.y);
}

static const int numSamples = 100000;

PSOutput PSMain(PSInput input)
{
  PSOutput output;
  float3 ambient, diffuse, specular;
  // Match QT/Cocoa: use replica counts as-is for texture tiling.
  float3 numberOfReplicas = structureUniforms.numberOfReplicas.xyz;
  float3 direction = normalize(input.worldPosition.xyz - frameUniforms.cameraPosition.xyz);
  float4 dir = float4(direction.x, direction.y, direction.z, 0.0);
  float3 ray_direction = mul(structureUniforms.inverseBoxMatrix,
                             mul(structureUniforms.inverseModelMatrix, dir)).xyz;

  float3 ray_origin = input.UV;
  // Guard only the step divisor (Cocoa/QT divide by .z directly).
  float replicaZ = max(abs(numberOfReplicas.z), 1.0);
  float stepLength = isosurfaceUniforms.stepLength / replicaZ;

  float t_0, t_1;
  Ray casting_ray = { ray_origin, ray_direction };
  AABB bounding_box = { float3(1.0, 1.0, 1.0), float3(0.0, 0.0, 0.0) };
  ray_box_intersection(casting_ray, bounding_box, t_0, t_1);

  float3 ray_start = ray_origin + ray_direction * t_0;
  float3 ray_stop = ray_origin + ray_direction * t_1;

  float3 ray = ray_stop - ray_start;
  float ray_length = length(ray);
  float3 step_vector = stepLength * ray / max(ray_length, 1e-8);

  float depth = depthTexture.Load(int3(int2(input.position.xy), 0)).r;
  // 1x1 far-depth fallback is OOB for most FragCoords (Load → 0). Real scene depth is >0.
  if (depth <= 0.0)
    depth = 1.0;

  float newDepth = 1.0;
  // Must use the same box as the VS / ray UV (structure box, incl. replica origin).
  float4x4 m = mul(frameUniforms.mvpMatrix,
                   mul(structureUniforms.modelMatrix, structureUniforms.boxMatrix));

  float4 scaleToEncompassing = isosurfaceUniforms.scaleToEncompassing;

  float4 colour = float4(0.0, 0.0, 0.0, 0.0);
  float3 position = ray_start;
  [loop]
  for (int i = 0; i < numSamples && ray_length > 0 && colour.a < 1.0; i++)
  {
    // SampleLevel avoids gradient-based unroll failure under D3DCOMPILE_ENABLE_STRICTNESS.
    float4 values = volume.SampleLevel(volumeSampler, numberOfReplicas * (scaleToEncompassing.xyz * position), 0);
    float3 grad = mul(structureUniforms.modelMatrix,
                      mul(transpose(structureUniforms.inverseBoxMatrix),
                          float4(values.gba, 0.0))).rgb;
    // Flat regions / padding edges have ~0 gradients; normalize(0) → NaN → black blobs.
    float gradLen = length(grad);
    if (gradLen < 1e-5)
    {
      position = position + step_vector;
      ray_length -= stepLength;
      float4 clipSkip = mul(m, float4(position, 1.0));
      newDepth = 0.5 * (clipSkip.z / clipSkip.w) + 0.5;
      if (newDepth > depth)
        break;
      continue;
    }
    float3 normal = grad / gradLen;
    float4 c = transferFunction.SampleLevel(transferFunctionSampler,
                                            float2(values.r, isosurfaceUniforms.transferFunctionIndex), 0);

    c.a = smoothstep(isosurfaceUniforms.transparencyThreshold, 1.0, c.a);
    c.a = saturate(c.a);
    if (c.a < 1e-4)
    {
      position = position + step_vector;
      ray_length -= stepLength;
      float4 clipSkip = mul(m, float4(position, 1.0));
      newDepth = 0.5 * (clipSkip.z / clipSkip.w) + 0.5;
      if (newDepth > depth)
        break;
      continue;
    }

    float3 R = reflect(-direction, normal);
    float dotProduct = dot(normal, direction);

    if (dotProduct < 0)
    {
      ambient = isosurfaceUniforms.ambientBackSide.rgb;
      diffuse = abs(dotProduct) * isosurfaceUniforms.diffuseBackSide.rgb;
      specular = pow(max(dot(R, direction), 0.0), isosurfaceUniforms.shininessBackSide)
                 * isosurfaceUniforms.specularBackSide.rgb;
      float3 totalColor = (ambient + diffuse + specular).rgb;
      totalColor = max(totalColor, float3(0.05, 0.05, 0.05));

      if (isosurfaceUniforms.backHDR)
        totalColor = 1.0 - exp2(-totalColor * isosurfaceUniforms.backHDRExposure);

      c.a = 1.0 - pow(1.0 - c.a, stepLength * 2000.0);
      colour.rgb += (1.0 - colour.a) * c.a * c.rgb * totalColor.rgb;
      colour.a += (1.0 - colour.a) * c.a;
    }
    else
    {
      ambient = isosurfaceUniforms.ambientFrontSide.rgb;
      diffuse = abs(dotProduct) * isosurfaceUniforms.diffuseFrontSide.rgb;
      specular = pow(max(dot(R, direction), 0.0), isosurfaceUniforms.shininessFrontSide)
                 * isosurfaceUniforms.specularFrontSide.rgb;
      float3 totalColor = (ambient + diffuse + specular).rgb;
      totalColor = max(totalColor, float3(0.05, 0.05, 0.05));

      if (isosurfaceUniforms.frontHDR)
        totalColor = 1.0 - exp2(-totalColor * isosurfaceUniforms.frontHDRExposure);

      c.a = 1.0 - pow(1.0 - c.a, stepLength * 2000.0);
      colour.rgb += (1.0 - colour.a) * c.a * c.rgb * totalColor.rgb;
      colour.a += (1.0 - colour.a) * c.a;
    }

    // Match Cocoa/QT: advance, then stop when the next sample would be behind scene depth.
    position = position + step_vector;
    ray_length -= stepLength;

    float4 clipPosition = mul(m, float4(position, 1.0));
    newDepth = 0.5 * (clipPosition.z / clipPosition.w) + 0.5;
    if (newDepth > depth)
      break;
  }

  // Match Cocoa/QT: low-opacity rays keep atom depth; opaque hits keep the marched depth.
  if (colour.a < 0.5)
    newDepth = depth;

  float3 hsv = rgb2hsv(colour.xyz);
  hsv.x = hsv.x * isosurfaceUniforms.hue;
  hsv.y = hsv.y * isosurfaceUniforms.saturation;
  hsv.z = hsv.z * isosurfaceUniforms.value;
  float3 rgb = hsv2rgb(hsv);
  // Guard against any residual NaNs from HSV on edge cases.
  if (any(isnan(rgb)) || any(isinf(rgb)))
    rgb = colour.xyz;
  output.color = float4(rgb * isosurfaceUniforms.diffuseFrontSide.w * colour.a,
                         isosurfaceUniforms.diffuseFrontSide.w * colour.a);
  output.depth = newDepth;
  return output;
}
)foo");
