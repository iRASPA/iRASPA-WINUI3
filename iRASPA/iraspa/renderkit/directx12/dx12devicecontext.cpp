/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#include "dx12devicecontext.h"
#include "directxdevicehelpers.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{
  bool sameAdapter(const LUID &a, const LUID &b)
  {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
  }

  // Read once: every context of a run has to land on the same adapter, since resources
  // cannot be shared across devices.
  bool forceWarpAdapter()
  {
    static const bool forced = GetEnvironmentVariableW(L"IRASPA_D3D12_FORCE_WARP", nullptr, 0) != 0;
    return forced;
  }

  /// Whether an exported picture is to be path-traced, which on a card without ray tracing only the
  /// software adapter can do. Asked here rather than left to the renderer so that the export takes
  /// the software adapter on its own: the window's device is created in a different process and has
  /// no need of it, and drawing the interface in software is painful enough to be worth avoiding.
  bool tracePicturesRequested()
  {
    static const bool requested =
        GetEnvironmentVariableW(L"IRASPA_D3D12_TRACE_PICTURE", nullptr, 0) != 0;
    return requested;
  }

  /// Whether an adapter can trace rays inline. Answering needs a device, so this makes a throwaway
  /// one; the tier of the device that is kept is read again in detectRaytracingSupport().
  bool adapterTracesRays(IDXGIAdapter1 *adapter)
  {
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
      return false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
      return false;
    return options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
  }

  // First usable hardware adapter, except that an adapter matching avoidAdapter is only
  // taken when it is the sole candidate. Offscreen export passes the adapter the window
  // is already on, so a second GPU takes the export work instead of competing for the one
  // drawing the UI; on a single-GPU machine it falls back to sharing.
  //
  // When rays are to be traced, a card that cannot trace them is passed over in favour of one that
  // can, however much better it would otherwise be. Only when none can does one come back, the
  // caller deciding then between rasterizing on it and tracing in software.
  void getHardwareAdapter(IDXGIFactory4 *factory, IDXGIAdapter1 **outAdapter, const LUID *avoidAdapter,
                          bool preferRaytracing)
  {
    *outAdapter = nullptr;
    ComPtr<IDXGIAdapter1> fallback;
    ComPtr<IDXGIAdapter1> withoutRaytracing;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
      DXGI_ADAPTER_DESC1 desc = {};
      adapter->GetDesc1(&desc);
      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        continue;
      if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        continue;

      if (preferRaytracing && !adapterTracesRays(adapter.Get()))
      {
        if (!withoutRaytracing)
          withoutRaytracing = adapter;
        continue;
      }

      if (avoidAdapter && sameAdapter(desc.AdapterLuid, *avoidAdapter))
      {
        if (!fallback)
          fallback = adapter;
        continue;
      }

      *outAdapter = adapter.Detach();
      return;
    }
    if (fallback)
      *outAdapter = fallback.Detach();
    else if (withoutRaytracing)
      *outAdapter = withoutRaytracing.Detach();
  }
}

Dx12DeviceContext::Fence::~Fence()
{
  if (event)
    CloseHandle(event);
}

Dx12DeviceContext::~Dx12DeviceContext()
{
  release();
}

void Dx12DeviceContext::setExtraRenderTargetCount(int count)
{
  if (m_initialized)
    return;
  m_extraRenderTargetCount = (std::max)(0, count);
}

bool Dx12DeviceContext::createDeviceAndQueues(const LUID *avoidAdapter, bool requireRaytracing)
{
  // Debug builds only. This used to be unconditional, which was harmless while the layer was
  // absent from the machines that ship: without the Graphics Tools feature installed the call
  // simply fails. Deploying the Agility SDK changes that, since d3d12SDKLayers.dll travels with
  // it, so leaving this on would hand every user the validating runtime and the frame rate that
  // comes with it.
#ifndef NDEBUG
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    debugController->EnableDebugLayer();
#endif

  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory))))
    return false;

  ComPtr<IDXGIAdapter1> adapter;
  getHardwareAdapter(m_factory.Get(), &adapter, avoidAdapter, requireRaytracing);

  // WARP implements DXR 1.1, so on a machine whose cards do not it is the only way to trace at all.
  // It traverses in a shader and is far too slow for interactive work, which is why the render view
  // never comes here on its own; an export is not a frame anyone is waiting on, so it does.
  //
  // Only the redistributable WARP does, though: the one that ships with Windows reports no ray
  // tracing at all and no shader model 6.5 either. It takes both halves of the redistributable, the
  // driver and the D3D12 runtime it belongs to, deployed into D3D12\ and opted into by
  // directxagilitysdk.h. The driver alone is worse than neither: paired with an older operating
  // system runtime it reports tier 1.1 and then intersects nothing at all, so a traced picture comes
  // back empty with every capability query having said yes.
  const bool warpAsked = forceWarpAdapter() || (m_offscreen && tracePicturesRequested());
  const bool warpToTrace = requireRaytracing && !(adapter && adapterTracesRays(adapter.Get()));

  if (warpAsked || warpToTrace)
  {
    ComPtr<IDXGIAdapter> warpAdapter;
    m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
    if (SUCCEEDED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
    {
      m_adapterLuid = LUID{};
      m_adapterDescription = L"WARP software adapter";
      m_warpAdapter = true;
      detectRaytracingSupport();

      // Come to only for a tracing it turns out not to offer, so the card is the better bet after
      // all: it cannot trace either, but it rasterizes many times faster than this does.
      if (warpToTrace && !warpAsked && m_raytracingTier < D3D12_RAYTRACING_TIER_1_1 && adapter)
        m_device.Reset();
    }
  }

  if (!m_device)
  {
    if (!adapter)
      return false;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
      return false;
    DXGI_ADAPTER_DESC1 desc = {};
    adapter->GetDesc1(&desc);
    m_adapterLuid = desc.AdapterLuid;
    m_adapterDescription = desc.Description;
    m_warpAdapter = false;
  }

  detectRaytracingSupport();

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
    return false;

  for (UINT i = 0; i < kInflightFrameCount; ++i)
  {
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&m_commandAllocators[i]))))
      return false;
  }
  if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_bundleAllocator))))
    return false;

  return createHeaps();
}

void Dx12DeviceContext::detectRaytracingSupport()
{
  m_device5.Reset();
  m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  if (!m_device)
    return;

  // ID3D12Device5 arrived with the DXR runtime, so failing to get it means no ray tracing
  // at all rather than merely an unsupported adapter.
  if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&m_device5))))
    return;

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
  {
    m_device5.Reset();
    return;
  }
  m_raytracingTier = options5.RaytracingTier;
}

bool Dx12DeviceContext::createHeaps()
{
  // Swapchain backbuffers + 1 MSAA scene color RTV.
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = m_swapChainBufferCount + 1 + m_extraRenderTargetCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    return false;
  m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
  // Slot 0: MSAA scene depth; slot 1: single-sample resolved depth (glow / volume).
  dsvHeapDesc.NumDescriptors = 2 + m_extraRenderTargetCount;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    return false;
  m_dsvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  return true;
}

bool Dx12DeviceContext::initializeForHwnd(HWND hwnd, UINT width, UINT height, float clearColor[4])
{
  (void)clearColor;
  release();
  m_composition = false;
  m_width = (std::max)(1u, width);
  m_height = (std::max)(1u, height);

  if (!createDeviceAndQueues())
    return false;

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = m_width;
  desc.Height = m_height;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = m_swapChainBufferCount;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.Scaling = DXGI_SCALING_STRETCH;

  ComPtr<IDXGISwapChain1> swap1;
  if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr, nullptr, &swap1)))
    return false;
  if (FAILED(swap1.As(&m_swapChain)))
    return false;

  m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

  if (!setupRenderTargets())
    return false;

  m_initialized = true;
  return true;
}

bool Dx12DeviceContext::initializeForComposition(UINT width, UINT height, float clearColor[4])
{
  (void)clearColor;
  release();
  m_composition = true;
  m_width = (std::max)(1u, width);
  m_height = (std::max)(1u, height);

  if (!createDeviceAndQueues())
    return false;

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = m_width;
  desc.Height = m_height;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = m_swapChainBufferCount;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

  ComPtr<IDXGISwapChain1> swap1;
  if (FAILED(m_factory->CreateSwapChainForComposition(m_commandQueue.Get(), &desc, nullptr, &swap1)))
    return false;
  if (FAILED(swap1.As(&m_swapChain)))
    return false;

  if (!setupRenderTargets())
    return false;

  m_initialized = true;
  return true;
}

bool Dx12DeviceContext::initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter,
                                            bool requireRaytracing)
{
  release();
  // Not a composition context: without a swap chain there is no premultiplied-alpha
  // surface to match, and RGBA is the order the readback wants anyway.
  m_composition = false;
  m_offscreen = true;
  m_width = (std::max)(1u, width);
  m_height = (std::max)(1u, height);

  if (!createDeviceAndQueues(avoidAdapter, requireRaytracing))
    return false;

  // Only the scene targets; setupRenderTargets() would go looking for backbuffers. The
  // descriptor heaps still reserve the swap-chain slots so that sceneColorCPUHandle()
  // indexes the same way in both kinds of context.
  if (!createSceneMsaaTargets())
    return false;

  m_initialized = true;
  return true;
}

void Dx12DeviceContext::releaseRenderTargets()
{
  m_resolvedDepth.Reset();
  m_sceneColor.Reset();
  m_depthStencil.Reset();
  for (int i = 0; i < m_swapChainBufferCount; ++i)
    m_renderTargets[i].Reset();
  m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  m_sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  m_resolvedDepthState = D3D12_RESOURCE_STATE_COMMON;
}

void Dx12DeviceContext::release()
{
  releaseRenderTargets();
  m_dsvHeap.Reset();
  m_rtvHeap.Reset();
  m_bundleAllocator.Reset();
  for (UINT i = 0; i < kInflightFrameCount; ++i)
    m_commandAllocators[i].Reset();
  m_frameIndex = 0;
  m_commandQueue.Reset();
  m_swapChain.Reset();
  m_device5.Reset();
  m_device.Reset();
  m_factory.Reset();
  m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  m_warpAdapter = false;
  m_initialized = false;
  m_offscreen = false;
  m_offscreenActive = false;
}

void Dx12DeviceContext::detectSceneSampleCount()
{
  m_sceneSampleCount = 1;
  m_sceneSampleDesc = {1, 0};
  // Match QT: target 8×, fall back to 4/2 if unsupported for color + depth.
  const DXGI_FORMAT colorFmt = backBufferFormat();
  for (int candidate : {8, 4, 2})
  {
    const DXGI_SAMPLE_DESC colorDesc = makeSampleDesc(colorFmt, candidate);
    const DXGI_SAMPLE_DESC depthDesc = makeSampleDesc(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, candidate);
    if (colorDesc.Count == static_cast<UINT>(candidate)
        && depthDesc.Count == static_cast<UINT>(candidate))
    {
      m_sceneSampleCount = static_cast<UINT>(candidate);
      m_sceneSampleDesc = colorDesc;
      return;
    }
  }
}

bool Dx12DeviceContext::createSceneMsaaTargets()
{
  detectSceneSampleCount();
  DirectXDeviceHelpers::setSceneSampleCount(m_sceneSampleCount);
  const DXGI_FORMAT colorFmt = backBufferFormat();

  D3D12_CLEAR_VALUE colorClear = {};
  colorClear.Format = colorFmt;
  colorClear.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC colorDesc = {};
  colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  colorDesc.Width = m_width;
  colorDesc.Height = m_height;
  colorDesc.DepthOrArraySize = 1;
  colorDesc.MipLevels = 1;
  colorDesc.Format = colorFmt;
  colorDesc.SampleDesc = m_sceneSampleDesc;
  colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
                                               IID_PPV_ARGS(&m_sceneColor))))
    return false;
  m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  sceneRtv.ptr += static_cast<SIZE_T>(m_swapChainBufferCount) * m_rtvStride;
  m_device->CreateRenderTargetView(m_sceneColor.Get(), nullptr, sceneRtv);

  ID3D12Resource *ds = createDepthStencil(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(),
                                          m_width, m_height, static_cast<int>(m_sceneSampleCount));
  if (!ds)
    return false;
  m_depthStencil.Attach(ds);
  m_sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

  D3D12_CLEAR_VALUE depthClear = {};
  depthClear.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depthClear.DepthStencil.Depth = 1.0f;

  D3D12_RESOURCE_DESC resolvedDesc = {};
  resolvedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resolvedDesc.Width = m_width;
  resolvedDesc.Height = m_height;
  resolvedDesc.DepthOrArraySize = 1;
  resolvedDesc.MipLevels = 1;
  resolvedDesc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
  resolvedDesc.SampleDesc.Count = 1;
  resolvedDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resolvedDesc,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                               IID_PPV_ARGS(&m_resolvedDepth))))
    return false;
  m_resolvedDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

  // 1× DSV for glow / other non-MSAA passes that still need scene depth.
  D3D12_DEPTH_STENCIL_VIEW_DESC resolvedDsv = {};
  resolvedDsv.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  resolvedDsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  D3D12_CPU_DESCRIPTOR_HANDLE resolvedHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
  resolvedHandle.ptr += m_dsvStride;
  m_device->CreateDepthStencilView(m_resolvedDepth.Get(), &resolvedDsv, resolvedHandle);

  std::printf("Dx12DeviceContext: scene MSAA sample count = %u\n", m_sceneSampleCount);
  return true;
}

bool Dx12DeviceContext::setupRenderTargets()
{
  releaseRenderTargets();

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (int i = 0; i < m_swapChainBufferCount; ++i)
  {
    if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
      return false;
    m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
    rtvHandle.ptr += m_rtvStride;
  }

  return createSceneMsaaTargets();
}

bool Dx12DeviceContext::resize(UINT width, UINT height)
{
  if (!m_initialized)
    return false;

  m_width = (std::max)(1u, width);
  m_height = (std::max)(1u, height);

  releaseRenderTargets();

  if (m_offscreen)
    return createSceneMsaaTargets();

  const DXGI_FORMAT format = m_composition ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  HRESULT hr = m_swapChain->ResizeBuffers(m_swapChainBufferCount, m_width, m_height, format, 0);
  if (FAILED(hr))
    return false;

  return setupRenderTargets();
}

void Dx12DeviceContext::setCompositionScale(float scaleX, float scaleY)
{
  if (!m_composition || !m_swapChain)
    return;

  scaleX = (std::max)(scaleX, 0.0001f);
  scaleY = (std::max)(scaleY, 0.0001f);

  ComPtr<IDXGISwapChain2> swap2;
  if (FAILED(m_swapChain.As(&swap2)) || !swap2)
    return;

  // Swap chain buffers are in physical pixels; SwapChainPanel layout is in DIPs.
  DXGI_MATRIX_3X2_F inverseScale = {};
  inverseScale._11 = 1.0f / scaleX;
  inverseScale._22 = 1.0f / scaleY;
  swap2->SetMatrixTransform(&inverseScale);
}

bool Dx12DeviceContext::present(UINT syncInterval)
{
  if (!m_initialized || !m_swapChain)
    return false;
  const HRESULT hr = m_swapChain->Present(syncInterval, 0);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
  {
    const HRESULT reason = m_device ? m_device->GetDeviceRemovedReason() : hr;
    std::fprintf(stderr, "Dx12DeviceContext::present: device lost (hr=0x%08lX reason=0x%08lX)\n",
                 static_cast<unsigned long>(hr), static_cast<unsigned long>(reason));
    return false;
  }
  return SUCCEEDED(hr);
}

Dx12DeviceContext::Fence *Dx12DeviceContext::createFence() const
{
  Fence *f = new Fence;
  if (FAILED(m_device->CreateFence(f->value.load(), D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f->fence))))
    return f;
  f->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  return f;
}

void Dx12DeviceContext::waitForGPU(Fence *f) const
{
  waitForFenceValue(f, signalFence(f));
}

uint64_t Dx12DeviceContext::signalFence(Fence *f) const
{
  if (!f || !f->fence || !m_commandQueue)
    return 0;
  const uint64_t newValue = f->value.fetch_add(1) + 1;
  m_commandQueue->Signal(f->fence.Get(), newValue);
  return newValue;
}

void Dx12DeviceContext::waitForFenceValue(Fence *f, uint64_t value) const
{
  if (!f || !f->fence || value == 0)
    return;
  if (f->fence->GetCompletedValue() < value)
  {
    f->fence->SetEventOnCompletion(value, f->event);
    WaitForSingleObject(f->event, INFINITE);
  }
}

void Dx12DeviceContext::advanceFrame()
{
  m_frameIndex = (m_frameIndex + 1) % kInflightFrameCount;
}

void Dx12DeviceContext::transitionResource(ID3D12Resource *resource, ID3D12GraphicsCommandList *commandList,
                                           D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  commandList->ResourceBarrier(1, &barrier);
}

void Dx12DeviceContext::uavBarrier(ID3D12Resource *resource, ID3D12GraphicsCommandList *commandList) const
{
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  commandList->ResourceBarrier(1, &barrier);
}

// Both return nothing offscreen, where there is no backbuffer to hand out. Callers there
// resolve into a texture of their own instead.
ID3D12Resource *Dx12DeviceContext::backBufferRenderTarget() const
{
  if (!m_swapChain)
    return nullptr;
  return m_renderTargets[m_swapChain->GetCurrentBackBufferIndex()].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DeviceContext::backBufferRenderTargetCPUHandle() const
{
  if (!m_swapChain)
    return D3D12_CPU_DESCRIPTOR_HANDLE{};
  const int frameIndex = static_cast<int>(m_swapChain->GetCurrentBackBufferIndex());
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  rtvHandle.ptr += static_cast<SIZE_T>(frameIndex) * m_rtvStride;
  return rtvHandle;
}

ID3D12Resource *Dx12DeviceContext::depthStencilResource() const
{
  return m_depthStencil.Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DeviceContext::sceneColorCPUHandle() const
{
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  rtv.ptr += static_cast<SIZE_T>(m_swapChainBufferCount) * m_rtvStride;
  return rtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12DeviceContext::depthStencilCPUHandle() const
{
  return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Dx12DeviceContext::resolveSceneColorToBackBuffer(ID3D12GraphicsCommandList *commandList)
{
  resolveSceneColorTo(commandList, backBufferRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Dx12DeviceContext::resolveSceneColorTo(ID3D12GraphicsCommandList *commandList,
                                            ID3D12Resource *destination,
                                            D3D12_RESOURCE_STATES destinationState)
{
  if (!commandList || !m_sceneColor || !destination)
    return;

  if (m_sceneSampleCount <= 1)
  {
    // 1×: copy scene color → destination.
    if (m_sceneColorState != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
      transitionResource(m_sceneColor.Get(), commandList, m_sceneColorState, D3D12_RESOURCE_STATE_COPY_SOURCE);
      m_sceneColorState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    transitionResource(destination, commandList, destinationState, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->CopyResource(destination, m_sceneColor.Get());
    transitionResource(destination, commandList, D3D12_RESOURCE_STATE_COPY_DEST, destinationState);
    transitionResource(m_sceneColor.Get(), commandList, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return;
  }

  if (m_sceneColorState != D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
  {
    transitionResource(m_sceneColor.Get(), commandList, m_sceneColorState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    m_sceneColorState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
  }
  transitionResource(destination, commandList, destinationState, D3D12_RESOURCE_STATE_RESOLVE_DEST);
  commandList->ResolveSubresource(destination, 0, m_sceneColor.Get(), 0, backBufferFormat());
  transitionResource(destination, commandList, D3D12_RESOURCE_STATE_RESOLVE_DEST, destinationState);
  transitionResource(m_sceneColor.Get(), commandList, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

bool Dx12DeviceContext::beginOffscreenCapture(UINT width, UINT height)
{
  if (!m_initialized || m_offscreenActive)
    return false;

  m_savedWidth = m_width;
  m_savedHeight = m_height;
  m_width = (std::max)(1u, width);
  m_height = (std::max)(1u, height);
  m_offscreenActive = true;

  if (createSceneMsaaTargets())
    return true;

  // Out of memory at the requested size: put the window-sized targets back so the live
  // view keeps working, rather than leaving the context pointing at half-built ones.
  endOffscreenCapture();
  return false;
}

void Dx12DeviceContext::endOffscreenCapture()
{
  if (!m_offscreenActive)
    return;
  m_offscreenActive = false;
  m_width = m_savedWidth;
  m_height = m_savedHeight;
  createSceneMsaaTargets();
}

ID3D12Resource *Dx12DeviceContext::resolveSceneDepth(ID3D12GraphicsCommandList *commandList,
                                                     D3D12_RESOURCE_STATES finalState)
{
  if (!commandList || !m_depthStencil || !m_resolvedDepth)
    return nullptr;

  if (m_sceneSampleCount <= 1)
  {
    if (m_sceneDepthState != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
      transitionResource(m_depthStencil.Get(), commandList, m_sceneDepthState, D3D12_RESOURCE_STATE_COPY_SOURCE);
      m_sceneDepthState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (m_resolvedDepthState != D3D12_RESOURCE_STATE_COPY_DEST)
    {
      transitionResource(m_resolvedDepth.Get(), commandList, m_resolvedDepthState, D3D12_RESOURCE_STATE_COPY_DEST);
      m_resolvedDepthState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    commandList->CopyResource(m_resolvedDepth.Get(), m_depthStencil.Get());
  }
  else
  {
    if (m_sceneDepthState != D3D12_RESOURCE_STATE_RESOLVE_SOURCE)
    {
      transitionResource(m_depthStencil.Get(), commandList, m_sceneDepthState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      m_sceneDepthState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    }
    if (m_resolvedDepthState != D3D12_RESOURCE_STATE_RESOLVE_DEST)
    {
      transitionResource(m_resolvedDepth.Get(), commandList, m_resolvedDepthState, D3D12_RESOURCE_STATE_RESOLVE_DEST);
      m_resolvedDepthState = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    }
    // Nearest-Z resolve (like QT's GL_NEAREST depth blit / Cocoa MSAA depth resolve).
    // Classic ResolveSubresource averages samples and pushes occlusion too far so volume
    // rays march through atoms.
    ComPtr<ID3D12GraphicsCommandList1> list1;
    if (SUCCEEDED(commandList->QueryInterface(IID_PPV_ARGS(&list1))) && list1)
    {
      list1->ResolveSubresourceRegion(
          m_resolvedDepth.Get(), 0, 0, 0,
          m_depthStencil.Get(), 0, nullptr,
          DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
          D3D12_RESOLVE_MODE_MIN);
    }
    else
    {
      commandList->ResolveSubresource(m_resolvedDepth.Get(), 0, m_depthStencil.Get(), 0,
                                      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    }
  }

  transitionResource(m_resolvedDepth.Get(), commandList, m_resolvedDepthState, finalState);
  m_resolvedDepthState = finalState;

  transitionResource(m_depthStencil.Get(), commandList, m_sceneDepthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  return m_resolvedDepth.Get();
}

void Dx12DeviceContext::beginSceneStencilRead(ID3D12GraphicsCommandList *commandList)
{
  if (!commandList || !m_depthStencil)
    return;
  if (m_sceneDepthState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    return;
  transitionResource(m_depthStencil.Get(), commandList, m_sceneDepthState,
                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_sceneDepthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void Dx12DeviceContext::endSceneStencilRead(ID3D12GraphicsCommandList *commandList)
{
  if (!commandList || !m_depthStencil)
    return;
  if (m_sceneDepthState == D3D12_RESOURCE_STATE_DEPTH_WRITE)
    return;
  transitionResource(m_depthStencil.Get(), commandList, m_sceneDepthState,
                     D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

std::string Dx12DeviceContext::takeDebugMessages()
{
  if (!m_device)
    return std::string();

  ComPtr<ID3D12InfoQueue> queue;
  if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&queue))) || !queue)
    return std::string();

  std::string collected;
  const UINT64 count = queue->GetNumStoredMessages();
  std::vector<char> storage;
  for (UINT64 i = 0; i < count; ++i)
  {
    SIZE_T length = 0;
    if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0)
      continue;
    storage.resize(length);
    D3D12_MESSAGE *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
    if (FAILED(queue->GetMessage(i, message, &length)))
      continue;
    if (message->Severity > D3D12_MESSAGE_SEVERITY_WARNING)
      continue;

    if (!collected.empty())
      collected += "; ";
    collected.append(message->pDescription, message->DescriptionByteLength > 0
                                                 ? message->DescriptionByteLength - 1
                                                 : 0);
  }
  queue->ClearStoredMessages();
  return collected;
}

uint32_t Dx12DeviceContext::alignedCBSize(uint32_t size) const
{
  return (size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)
         & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
}

uint32_t Dx12DeviceContext::alignedTexturePitch(uint32_t rowPitch) const
{
  return (rowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
         & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
}

uint32_t Dx12DeviceContext::alignedTextureOffset(uint32_t offset) const
{
  return (offset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)
         & ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
}

DXGI_SAMPLE_DESC Dx12DeviceContext::makeSampleDesc(DXGI_FORMAT format, int samples) const
{
  DXGI_SAMPLE_DESC sampleDesc = {};
  sampleDesc.Count = 1;
  sampleDesc.Quality = 0;
  if (samples > 1)
  {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaInfo = {};
    msaaInfo.Format = format;
    msaaInfo.SampleCount = static_cast<UINT>(samples);
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaaInfo, sizeof(msaaInfo)))
        && msaaInfo.NumQualityLevels > 0)
    {
      sampleDesc.Count = static_cast<UINT>(samples);
      // Standard MSAA pattern (quality 0) — portable across vendors.
      sampleDesc.Quality = 0;
    }
  }
  return sampleDesc;
}

ID3D12Resource *Dx12DeviceContext::createOffscreenRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                               UINT width, UINT height,
                                                               const float *clearColor, int samples)
{
  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (clearColor)
    memcpy(clearValue.Color, clearColor, 4 * sizeof(float));

  D3D12_HEAP_PROPERTIES heapProp = {};
  heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rtDesc = {};
  rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rtDesc.Width = width;
  rtDesc.Height = height;
  rtDesc.DepthOrArraySize = 1;
  rtDesc.MipLevels = 1;
  rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rtDesc.SampleDesc = makeSampleDesc(rtDesc.Format, samples);
  rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  ID3D12Resource *resource = nullptr;
  if (FAILED(m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &rtDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                                               IID_PPV_ARGS(&resource))))
    return nullptr;

  m_device->CreateRenderTargetView(resource, nullptr, viewHandle);
  return resource;
}

ID3D12Resource *Dx12DeviceContext::createDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                      UINT width, UINT height, int samples)
{
  D3D12_CLEAR_VALUE depthClearValue = {};
  depthClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depthClearValue.DepthStencil.Depth = 1.0f;

  D3D12_HEAP_PROPERTIES heapProp = {};
  heapProp.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  bufDesc.Width = width;
  bufDesc.Height = height;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
  bufDesc.SampleDesc = makeSampleDesc(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, samples);
  bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  ID3D12Resource *resource = nullptr;
  if (FAILED(m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue,
                                               IID_PPV_ARGS(&resource))))
    return nullptr;

  D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
  depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  depthStencilDesc.ViewDimension = bufDesc.SampleDesc.Count <= 1
                                       ? D3D12_DSV_DIMENSION_TEXTURE2D
                                       : D3D12_DSV_DIMENSION_TEXTURE2DMS;
  m_device->CreateDepthStencilView(resource, &depthStencilDesc, viewHandle);
  return resource;
}

ID3D12Resource *Dx12DeviceContext::createExtraRenderTargetAndView(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                                  UINT width, UINT height,
                                                                  const float *clearColor, int samples)
{
  return createOffscreenRenderTarget(viewHandle, width, height, clearColor, samples);
}

ID3D12Resource *Dx12DeviceContext::createExtraDepthStencilAndView(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                                  UINT width, UINT height, int samples)
{
  return createDepthStencil(viewHandle, width, height, samples);
}
