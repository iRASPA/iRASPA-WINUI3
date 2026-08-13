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
  bool initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter = nullptr);

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
  /// Resolve MSAA scene depth into a single-sample texture for volume ray occlusion.
  ID3D12Resource *resolveSceneDepth(ID3D12GraphicsCommandList *commandList);
  /// 1× resolved depth DSV (for glow and other non-MSAA depth tests).
  D3D12_CPU_DESCRIPTOR_HANDLE resolvedDepthCPUHandle() const;
  /// Transition resolved depth to DEPTH_READ for binding as a DSV.
  void prepareResolvedDepthForDepthTest(ID3D12GraphicsCommandList *commandList);

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
  bool createDeviceAndQueues(const LUID *avoidAdapter = nullptr);
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
