/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <string>

using Microsoft::WRL::ComPtr;

/// Qt-free D3D12 device + swap-chain host used by WinUI (SwapChainPanel) and as a shared backend.
class Dx12DeviceContext
{
public:
  struct Fence
  {
    Fence() = default;
    ~Fence();
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    std::atomic<uint64_t> value{0};
    Fence(const Fence &) = delete;
    Fence &operator=(const Fence &) = delete;
  };

  Dx12DeviceContext() = default;
  ~Dx12DeviceContext();

  Dx12DeviceContext(const Dx12DeviceContext &) = delete;
  Dx12DeviceContext &operator=(const Dx12DeviceContext &) = delete;

  /// Create device/queues and an HWND swap chain (Qt / classic window path).
  bool initializeForHwnd(HWND hwnd, UINT width, UINT height, float clearColor[4] = nullptr);

  /// Create device/queues and a composition swap chain for WinUI SwapChainPanel.
  /// After success, call setSwapChainOnPanel(IUnknown* panelNative) or SetSwapChain from the host.
  bool initializeForComposition(UINT width, UINT height, float clearColor[4] = nullptr);

  /// Create a device with scene targets but no swap chain, for rendering that never
  /// reaches a window (picture and movie export). When \a avoidAdapter names the adapter
  /// the live view already runs on and a second hardware adapter exists, the second one
  /// is chosen, so exporting does not contend with the GPU that is drawing the window.
  /// Nothing can be shared with a context on a different adapter: D3D12 resources belong
  /// to one device, so such a context builds its own buffers from the model.
  ///
  /// With \a requireRaytracing, a card that can trace rays is taken over one that cannot, and the
  /// software adapter over a machine where none can. That is far too slow to draw a window with, but
  /// an export is not a frame anyone is waiting on, and it is the difference between a traced picture
  /// and a rasterized one.
  bool initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter = nullptr,
                           bool requireRaytracing = false);

  bool isOffscreen() const { return m_offscreen; }
  /// Which adapter this context ended up on; pass to initializeOffscreen to avoid it.
  LUID adapterLuid() const { return m_adapterLuid; }
  const std::wstring &adapterDescription() const { return m_adapterDescription; }

  void release();
  bool resize(UINT width, UINT height);
  bool present(UINT syncInterval = 1);

  /// WinUI SwapChainPanel: map physical-pixel swap chain into DIP layout (1/CompositionScale).
  void setCompositionScale(float scaleX, float scaleY);

  bool isInitialized() const { return m_initialized; }
  bool isComposition() const { return m_composition; }
  DXGI_FORMAT backBufferFormat() const
  {
    return m_composition ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  UINT width() const { return m_width; }
  UINT height() const { return m_height; }
  /// Scene MSAA sample count (1–8), matching QT's capped multisampling.
  UINT sceneSampleCount() const { return m_sceneSampleCount; }
  DXGI_SAMPLE_DESC sceneSampleDesc() const { return m_sceneSampleDesc; }

  ID3D12Device *device() const { return m_device.Get(); }
  /// The device the ray-tracing entry points live on, or null on a runtime older than
  /// Windows 10 1809. Everything the tracer needs (acceleration-structure builds and the
  /// prebuild-info query) hangs off this interface rather than off ID3D12Device.
  ID3D12Device5 *device5() const { return m_device5.Get(); }

  /// Everything the debug layer has complained about since this was last called, emptied as it is
  /// read. The layer is enabled whenever it is installed, but its messages otherwise go only to a
  /// debugger, which an export running on its own has none of.
  std::string takeDebugMessages();
  D3D12_RAYTRACING_TIER raytracingTier() const { return m_raytracingTier; }
  /// True when the adapter can run inline RayQuery from a compute shader, which is the only
  /// form of ray tracing the path tracer and the shadow mask use. Tier 1.0 is not enough:
  /// it only offers the separate ray-tracing pipeline, which this port does not build.
  bool supportsInlineRaytracing() const
  {
    return m_device5 != nullptr && m_raytracingTier >= D3D12_RAYTRACING_TIER_1_1;
  }
  /// Set when the context fell back to (or was forced onto) the software rasterizer. WARP
  /// implements DXR 1.1, so it is the only way to exercise the tracer on hardware without it.
  bool isWarpAdapter() const { return m_warpAdapter; }
  ID3D12CommandQueue *commandQueue() const { return m_commandQueue.Get(); }
  ID3D12CommandAllocator *commandAllocator() const { return m_commandAllocators[m_frameIndex].Get(); }
  ID3D12CommandAllocator *bundleAllocator() const { return m_bundleAllocator.Get(); }
  IDXGISwapChain3 *swapChain() const { return m_swapChain.Get(); }

  static constexpr UINT kInflightFrameCount = 2;

  Fence *createFence() const;
  void waitForGPU(Fence *f) const;
  uint64_t signalFence(Fence *f) const;
  void waitForFenceValue(Fence *f, uint64_t value) const;
  void advanceFrame();
  UINT frameIndex() const { return m_frameIndex; }

  void transitionResource(ID3D12Resource *resource, ID3D12GraphicsCommandList *commandList,
                          D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;
  void uavBarrier(ID3D12Resource *resource, ID3D12GraphicsCommandList *commandList) const;

  ID3D12Resource *backBufferRenderTarget() const;
  D3D12_CPU_DESCRIPTOR_HANDLE backBufferRenderTargetCPUHandle() const;

  /// MSAA (or 1×) scene color/depth used for all main-scene draws.
  ID3D12Resource *sceneColorResource() const { return m_sceneColor.Get(); }
  ID3D12Resource *depthStencilResource() const;
  D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCPUHandle() const;
  D3D12_CPU_DESCRIPTOR_HANDLE depthStencilCPUHandle() const;

  /// Resolve MSAA scene color into the current swapchain backbuffer.
  void resolveSceneColorToBackBuffer(ID3D12GraphicsCommandList *commandList);
  /// Resolve (or copy, at 1×) MSAA scene color into any 1× texture of backBufferFormat().
  /// The destination is returned to \a destinationState afterwards.
  void resolveSceneColorTo(ID3D12GraphicsCommandList *commandList, ID3D12Resource *destination,
                           D3D12_RESOURCE_STATES destinationState);

  /// Rebuild the scene color/depth targets at an arbitrary size so a frame can be drawn at
  /// export resolution rather than the swap chain's. The swap chain itself is untouched;
  /// pair with resolveSceneColorTo() instead of resolveSceneColorToBackBuffer(), because
  /// the backbuffer is still the old size. Both calls destroy and recreate the targets, so
  /// the GPU must be idle across them.
  bool beginOffscreenCapture(UINT width, UINT height);
  void endOffscreenCapture();
  bool offscreenCaptureActive() const { return m_offscreenActive; }
  /// Resolve MSAA scene depth into a single-sample texture for volume ray occlusion. The result is
  /// left in \a finalState, which the path tracer asks to be a non-pixel one: the same texture is
  /// read from a compute shader there rather than from a pixel shader.
  ID3D12Resource *resolveSceneDepth(
      ID3D12GraphicsCommandList *commandList,
      D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  /// Lets a pixel shader read the scene's stencil, and puts the buffer back afterwards. Only the
  /// pass that draws the edge cues does this, to find out which structure drew each pixel and which
  /// cues it asked for; the multisampled buffer is read as it stands, sample by sample, there being
  /// no resolve for a stencil. The depth half goes through resolveSceneDepth instead.
  void beginSceneStencilRead(ID3D12GraphicsCommandList *commandList);
  void endSceneStencilRead(ID3D12GraphicsCommandList *commandList);

  uint32_t alignedCBSize(uint32_t size) const;
  uint32_t alignedTexturePitch(uint32_t rowPitch) const;
  uint32_t alignedTextureOffset(uint32_t offset) const;

  ID3D12Resource *createExtraRenderTargetAndView(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                 UINT width, UINT height,
                                                 const float *clearColor = nullptr,
                                                 int samples = 0);
  ID3D12Resource *createExtraDepthStencilAndView(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                                 UINT width, UINT height,
                                                 int samples = 0);

  void setExtraRenderTargetCount(int count);

private:
  bool createDeviceAndQueues(const LUID *avoidAdapter = nullptr, bool requireRaytracing = false);
  void detectRaytracingSupport();
  bool createHeaps();
  bool setupRenderTargets();
  void releaseRenderTargets();
  void detectSceneSampleCount();
  bool createSceneMsaaTargets();
  DXGI_SAMPLE_DESC makeSampleDesc(DXGI_FORMAT format, int samples) const;
  ID3D12Resource *createOffscreenRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                              UINT width, UINT height,
                                              const float *clearColor, int samples);
  ID3D12Resource *createDepthStencil(D3D12_CPU_DESCRIPTOR_HANDLE viewHandle,
                                     UINT width, UINT height, int samples);

  bool m_initialized = false;
  bool m_composition = false;
  bool m_offscreen = false;
  bool m_offscreenActive = false;
  bool m_warpAdapter = false;
  D3D12_RAYTRACING_TIER m_raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
  LUID m_adapterLuid = {};
  std::wstring m_adapterDescription;
  UINT m_width = 1;
  UINT m_height = 1;
  UINT m_savedWidth = 1;
  UINT m_savedHeight = 1;
  int m_extraRenderTargetCount = 0;
  int m_swapChainBufferCount = 2;
  UINT m_rtvStride = 0;
  UINT m_dsvStride = 0;
  UINT m_sceneSampleCount = 1;
  DXGI_SAMPLE_DESC m_sceneSampleDesc = {1, 0};
  D3D12_RESOURCE_STATES m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
  D3D12_RESOURCE_STATES m_sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  D3D12_RESOURCE_STATES m_resolvedDepthState = D3D12_RESOURCE_STATE_COMMON;

  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12Device5> m_device5;
  ComPtr<ID3D12CommandQueue> m_commandQueue;
  ComPtr<IDXGISwapChain3> m_swapChain;
  UINT m_frameIndex = 0;
  ComPtr<ID3D12CommandAllocator> m_commandAllocators[kInflightFrameCount];
  ComPtr<ID3D12CommandAllocator> m_bundleAllocator;
  ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
  ComPtr<ID3D12Resource> m_renderTargets[3];
  ComPtr<ID3D12Resource> m_depthStencil;       // MSAA scene depth (or 1×)
  ComPtr<ID3D12Resource> m_sceneColor;         // MSAA scene color (or 1×)
  ComPtr<ID3D12Resource> m_resolvedDepth;      // single-sample depth for volume
  ComPtr<IDXGIFactory4> m_factory;
};
