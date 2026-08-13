/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#pragma once

#include "dx12devicecontext.h"
#include "dx12input.h"
#include "directxbackgroundshader.h"
#include "directxatomshader.h"
#include "directxbondshader.h"
#include "directxobjectshader.h"
#include "directxunitcellshader.h"
#include "directxboundingboxshader.h"
#include "directxlocalaxesshader.h"
#include "directxribbonshader.h"
#include "directxribbonselectionshader.h"
#include "directxribbonambientocclusionshader.h"
#include "directxtextrenderingshader.h"
#include "directxglobalaxesshader.h"
#include "directxenergysurface.h"
#include "directxenergyvolumerenderedsurface.h"
#include "directxpickingshader.h"
#include "directxselectionshader.h"
#include "directxblurshader.h"
#include "directxcompositeshader.h"
#include "rkcamera.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"
#include "trackball.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// WinUI-facing renderer: owns a Dx12DeviceContext and draws a live scene frame
/// including picking, selection overlays, and selection glow/blur/composite.
class DirectXRenderer
{
public:
  DirectXRenderer();
  ~DirectXRenderer();

  DirectXRenderer(const DirectXRenderer &) = delete;
  DirectXRenderer &operator=(const DirectXRenderer &) = delete;

  Dx12DeviceContext &deviceContext() { return m_device; }
  const Dx12DeviceContext &deviceContext() const { return m_device; }

  bool initializeComposition(UINT width, UINT height);
  bool initializeHwnd(HWND hwnd, UINT width, UINT height);
  /// Device and scene with no swap chain, for a renderer whose frames only ever reach a
  /// file (the out-of-process picture and movie export). \a avoidAdapter names the
  /// adapter the live view is on so a second GPU takes the work when there is one; see
  /// Dx12DeviceContext::initializeOffscreen. Only renderSceneToImage() is meaningful
  /// afterwards -- renderFrame() has nothing to present to.
  bool initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter = nullptr);
  void release();

  void resize(UINT width, UINT height);
  void renderFrame();

  /// Draw one frame at an arbitrary resolution into a CPU-side RGBA8 image, for picture
  /// and movie export. Returns a null RKImage if the scene is not ready or the targets
  /// could not be allocated at that size.
  ///
  /// Shares the command list, allocator and shader state with renderFrame(), none of
  /// which is thread-safe, so this must run on the render thread. It is also slow enough
  /// (a full scene draw plus a GPU stall for the readback) that callers should drive it
  /// one frame at a time from a worker rather than looping on the UI thread.
  RKImage renderSceneToImage(int width, int height, RKRenderQuality quality);

  /// Cocoa-style setNeedsDisplay hook. Host provides a coalesced RequestRedraw.
  void setNeedsDisplayCallback(std::function<void()> callback);
  void redraw();

  /// Fired after a click-pick or rubber-band drag changed the model selection,
  /// so the UI (atoms inspector list) can mirror it. Called on the UI thread.
  void setSelectionChangedCallback(std::function<void()> callback);

  /// Cocoa CameraDidChangeNotification equivalent: fired after user input
  /// changed the camera (rotation drag, pan, wheel zoom, reset), so the
  /// camera inspector can refresh its readouts. Called on the UI thread.
  void setCameraChangedCallback(std::function<void()> callback);

  void setClearColor(float r, float g, float b, float a);
  void setStatusMessage(const std::wstring &message);
  const std::wstring &statusMessage() const { return m_status; }

  /// Quality the ambient occlusion is baked at by the next reload. Baking is far more
  /// expensive than drawing and the live view cannot wait for it, so it stays at low;
  /// export raises it once, before the scene is loaded, and pays the cost per project
  /// rather than per frame.
  void setAmbientOcclusionQuality(RKRenderQuality quality) { m_ambientOcclusionQuality = quality; }
  RKRenderQuality ambientOcclusionQuality() const { return m_ambientOcclusionQuality; }

  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);
  void setRenderDataSource(std::shared_ptr<RKRenderDataSource> source);
  void reloadData();
  void reloadSelectionData();
  // What a structure occludes is what stands around it, so hiding or showing one of them leaves the
  // baked occlusion of the rest describing a scene that is no longer there. Cocoa throws the cached
  // textures of the whole scene away on such a change and lets the next reload bake them again.
  void invalidateCachedAmbientOcclusionTextures(std::vector<std::shared_ptr<RKRenderObject>> structures);

  std::array<int, 4> pickTexture(int x, int y, int width, int height);

  void onPointerPressed(const Dx12Input::PointerEvent &e);
  void onPointerMoved(const Dx12Input::PointerEvent &e);
  void onPointerReleased(const Dx12Input::PointerEvent &e);
  void onWheel(const Dx12Input::WheelEvent &e);

  /// Rubber-band selection (Cocoa selectInRectangle). Coordinates are device
  /// pixels with top-left origin; extend=true adds to the current selection.
  void applyRectangleSelection(double x0, double y0, double x1, double y1, bool extend);

  std::shared_ptr<RKCamera> camera() const { return m_camera; }

  void resetCameraView();
  void setCameraOrthographic(bool orthographic);
  void zoomCamera(double delta);

private:
  enum class Tracking
  {
    none = 0,
    rotating = 1,
    backgroundClick = 2,
    panning = 3,
    trucking = 4
  };

  void markNeedsDisplay();
  void notifyCameraChanged();
  void recordScenePass(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                       int width, int height);
  void recordSelectionGlow(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv, D3D12_CPU_DESCRIPTOR_HANDLE destRtv,
                           int width, int height);
  RKImage captureOffscreenFrame(int width, int height);
  bool createCommandList();
  /// Everything the three initialize* entry points share once their device context is up.
  bool initializeCommandListAndScene();
  bool initializeScene();
  void createConstantBuffers();
  void updateConstantBuffers();
  void updateTransformUniforms();
  void updateStructureUniforms();
  void updateIsosurfaceUniforms();
  void updateLightUniforms();
  void updateGlobalAxesUniforms();
  void pushStructuresToShaders();
  void uploadPendingTextures();
  void resetSceneResources();
  void createGlowTarget(ID3D12Device *device, int width, int height);
  void resizeGlowAndBlur(int width, int height);
  void clearAllSelections();
  void applyPickAt(int x, int y, bool toggle);
  void beginGpuFrame();
  void endGpuFrame();
  UINT inflightSlot() const { return m_device.frameIndex(); }
  ID3D12Resource *frameCB() const { return m_frameCBV[inflightSlot()].Get(); }
  ID3D12Resource *structureCB() const { return m_structureCBV[inflightSlot()].Get(); }
  ID3D12Resource *lightsCB() const { return m_lightsCBV[inflightSlot()].Get(); }
  ID3D12Resource *isosurfaceCB() const { return m_isosurfaceCBV[inflightSlot()].Get(); }
  ID3D12Resource *globalAxesCB() const { return m_globalAxesCBV[inflightSlot()].Get(); }

  Dx12DeviceContext m_device;
  ComPtr<ID3D12GraphicsCommandList> m_commandList;
  Dx12DeviceContext::Fence *m_fence = nullptr;
  float m_clearColor[4] = {0.12f, 0.18f, 0.28f, 1.0f};
  std::wstring m_status = L"DirectX 12 ready";
  bool m_ready = false;
  bool m_sceneReady = false;
  std::function<void()> m_needsDisplay;
  std::function<void()> m_selectionChanged;
  std::function<void()> m_cameraChanged;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12DescriptorHeap> m_srvHeap;
  ComPtr<ID3D12Resource> m_frameCBV[Dx12DeviceContext::kInflightFrameCount];
  ComPtr<ID3D12Resource> m_structureCBV[Dx12DeviceContext::kInflightFrameCount];
  ComPtr<ID3D12Resource> m_lightsCBV[Dx12DeviceContext::kInflightFrameCount];
  ComPtr<ID3D12Resource> m_isosurfaceCBV[Dx12DeviceContext::kInflightFrameCount];
  ComPtr<ID3D12Resource> m_globalAxesCBV[Dx12DeviceContext::kInflightFrameCount];
  UINT m_structureCBVStride = 0;
  UINT m_structureCBVCapacity = 0;
  UINT m_isosurfaceCBVStride = 0;
  UINT m_isosurfaceCBVCapacity = 0;
  uint64_t m_frameFenceValues[Dx12DeviceContext::kInflightFrameCount] = {};
  bool m_gpuFrameStarted = false;

  // Glow offscreen target (shared depth from main scene).
  ComPtr<ID3D12Resource> m_glowTexture;
  ComPtr<ID3D12DescriptorHeap> m_glowRtvHeap;
  D3D12_RESOURCE_STATES m_glowState = D3D12_RESOURCE_STATE_COMMON;
  int m_glowWidth = 0;
  int m_glowHeight = 0;
  static constexpr DXGI_FORMAT kGlowFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

  DirectXBackgroundShader m_backgroundShader;
  DirectXAtomShader m_atomShader;
  DirectXBondShader m_bondShader;
  DirectXObjectShader m_objectShader;
  DirectXUnitCellShader m_unitCellShader;
  DirectXLocalAxesShader m_localAxesShader;
  DirectXRibbonShader m_ribbonShader;
  DirectXRibbonSelectionShader m_ribbonSelectionShader;
  DirectXRibbonAmbientOcclusionShader m_ribbonAmbientOcclusionShader;
  DirectXBoundingBoxShader m_boundingBoxShader;
  DirectXEnergySurface m_energySurfaceShader;
  DirectXEnergyVolumeRenderedSurface m_energyVolumeShader;
  DirectXPickingShader m_pickingShader;
  DirectXSelectionShader m_selectionShader;
  DirectXTextRenderingShader m_textShader;
  DirectXGlobalAxesShader m_globalAxesShader;
  DirectXBlurShader m_blurShader;
  DirectXCompositeShader m_compositeShader;

  RKRenderQuality m_quality = RKRenderQuality::high;
  RKRenderQuality m_ambientOcclusionQuality = RKRenderQuality::low;
  std::shared_ptr<RKRenderDataSource> m_dataSource;
  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> m_structures;
  std::shared_ptr<RKCamera> m_camera;
  TrackBall m_trackBall;
  Tracking m_tracking = Tracking::none;
  float m_pressX = 0.f;
  float m_pressY = 0.f;
  float m_panLastX = 0.f;
  float m_panLastY = 0.f;
  static constexpr float kClickDragThresholdPx = 4.f;
};
