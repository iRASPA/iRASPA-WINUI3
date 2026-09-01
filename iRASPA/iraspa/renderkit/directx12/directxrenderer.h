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
#include "directxedgecueingshader.h"
#include "directxtracedpresentshader.h"
#include "directxpathtracergeometry.h"
#include "directxpathtracershader.h"
#include "directxshadowmaskshader.h"
#include "rkcamera.h"
#include "rkrenderkitprotocols.h"
#include "rkrenderuniforms.h"
#include "trackball.h"
#include <array>
#include <chrono>
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
  /// Dx12DeviceContext::initializeOffscreen. With \a requireRaytracing the adapter is
  /// chosen for its ability to trace rays, down to the software one. Only
  /// renderSceneToImage() is meaningful afterwards -- renderFrame() has nothing to present to.
  bool initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter = nullptr,
                           bool requireRaytracing = false);
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

  /// True when this renderer's adapter and the machine's shader compiler can both do inline
  /// ray tracing, which is what the path tracer and the traced shadow mask are built on.
  /// Everything ray-traced is gated on this and falls back to rasterizing when it is false.
  bool supportsRaytracing() const { return m_supportsRaytracing; }

  /// One line saying what the ray-tracing capability came out as, and why when it came out
  /// negative. Written once per device; the camera pane shows it and it goes to the log.
  const std::string &raytracingStatus() const { return m_raytracingStatus; }

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

  // One transparent structure, plus the flat index it occupies in the structure and
  // isosurface constant buffers.
  struct RenderOrderItem
  {
    size_t sceneIndex;
    size_t movieIndex;
    size_t structureIndex;
  };

  void markNeedsDisplay();
  void notifyCameraChanged();
  std::vector<RenderOrderItem> backToFrontRenderOrder() const;
  void recordScenePass(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                       int width, int height, bool suppressMolecularGeometry = false);
  void recordSelectionGlow(D3D12_CPU_DESCRIPTOR_HANDLE destRtv, int width, int height);
  /// Whether anything visible asked for a contour or a halo. Nothing about the cues is recorded when
  /// nothing did: not the parameters in the frame uniforms, and not the pass that reads them.
  bool drawsEdgeCues() const;
  /// Puts the frame's colour where the frame is going with the depth cues drawn over it, and reports
  /// whether it did: the caller resolves the colour itself when it did not.
  bool recordEdgeCues(D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv, int width, int height);
  RKImage captureOffscreenFrame(int width, int height);
  /// The traced counterpart: the same frame, but with the molecular geometry left out of the raster
  /// pass and traced instead, then composited over what the raster pass did draw.
  RKImage captureTracedOffscreenFrame(int width, int height, uint32_t sampleCount,
                                      uint32_t maximumBounces);
  /// The traced composite with its cues drawn over it, or a null image when there are none to draw.
  RKImage captureTracedFrameWithCues(int width, int height);
  RKImage readbackTexture(ID3D12Resource *texture, DXGI_FORMAT format, int width, int height);
  /// A null image, with \a reason recorded as the status so that a caller with no log to read can
  /// report why there is no picture.
  RKImage failedCapture(const std::string &reason);

  /// Where the ray through the middle of the image starts and where it heads, in the world space the
  /// acceleration structures are built in.
  std::string describeCentreRay() const;
  bool createCommandList();
  /// Everything the three initialize* entry points share once their device context is up.
  bool initializeCommandListAndScene();
  void detectRaytracingSupport();

  /// Whether this frame of the render view is to be drawn by the tracer rather than the rasterizer.
  /// Asked before anything is recorded, since the answer decides whether the raster pass draws the
  /// molecular geometry at all.
  bool wantsInteractiveTracing() const;
  /// Compiles the kernels and builds the acceleration structure if that has not happened yet, and
  /// reports whether the tracer can go ahead. Waits on the build, so the frame after a change to the
  /// structure costs that wait.
  bool prepareInteractiveTracing();
  /// The single-sample copy of the rasterized scene that the traced image is composited over, kept
  /// for the size of the view and rebuilt when that changes.
  ID3D12Resource *ensureTracedSceneColor(UINT width, UINT height);
  /// Resolves the rasterized scene and its depth, then traces this frame's samples over them.
  bool recordTracedFrame(int width, int height);
  /// Draws the finished traced image onto the back buffer.
  void presentTracedFrame(D3D12_CPU_DESCRIPTOR_HANDLE destRtv, int width, int height);

  /// Whether the scene wants traced shadows, has a light able to cast one, and is on a device that
  /// can trace them. An export answers off the project alone; a frame of the render view also asks
  /// the machine, a shadow costing a ray per light per pixel.
  bool tracesShadows() const;
  /// Traces the shadow mask for the frame about to be recorded, or leaves the mask size at zero when
  /// it cannot. Must run before the raster passes, on the ray-tracing capable list.
  void recordShadowMask(int width, int height);
  /// Binds the traced mask, or the all-lit fallback when nothing was traced, to the scene root
  /// signature. Called wherever that root signature is set, since a root view does not survive it
  /// being set again.
  void bindShadowMask();
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
  /// The same list as m_commandList, for the ray-tracing commands that only exist on the
  /// later interface (the acceleration-structure builds). Null when the runtime has no DXR,
  /// in which case the tracer does not run and nothing asks for it.
  ComPtr<ID3D12GraphicsCommandList4> m_commandList4;
  Dx12DeviceContext::Fence *m_fence = nullptr;
  float m_clearColor[4] = {0.12f, 0.18f, 0.28f, 1.0f};
  std::wstring m_status = L"DirectX 12 ready";
  std::string m_raytracingStatus = "the ray-tracing capability has not been looked at";
  bool m_supportsRaytracing = false;
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

  // Glow offscreen target (shared depth from main scene). The shells are drawn into the
  // multisampled one, to be depth-tested against the scene's own depth, and resolved into the
  // single-sampled one the blur reads.
  ComPtr<ID3D12Resource> m_glowTexture;
  ComPtr<ID3D12Resource> m_glowMsaaTexture;
  ComPtr<ID3D12DescriptorHeap> m_glowRtvHeap;
  D3D12_RESOURCE_STATES m_glowState = D3D12_RESOURCE_STATE_COMMON;
  D3D12_RESOURCE_STATES m_glowMsaaState = D3D12_RESOURCE_STATE_COMMON;
  UINT m_glowSampleCount = 1;
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
  /// The contour lines and halos of Tarini et al., drawn over the finished image from its depth and
  /// from the cueing the molecular passes recorded in the stencil.
  DirectXEdgeCueingShader m_edgeCueingShader;

  /// The ray-traced shadow mask and the geometry it traces against. Both stay quiet on a device
  /// that cannot trace: the mask pass then leaves the mask size at zero and every raster pass
  /// lights every surface, exactly as it did before any of this existed.
  DirectXPathTracerGeometry m_pathTracerGeometry;
  DirectXShadowMaskShader m_shadowMaskShader;
  /// The path tracer, which draws the molecular geometry itself rather than only asking what the
  /// lights can see. Used by the exported still image, and by the render view when it is asked to
  /// trace rather than rasterize.
  DirectXPathTracerShader m_pathTracerShader;
  /// Puts a traced frame on the back buffer, the tracer's image being written by a compute kernel
  /// rather than drawn.
  DirectXTracedPresentShader m_tracedPresentShader;
  /// Size of the mask traced for the frame being recorded, which the frame uniforms carry to the
  /// raster passes so they index it by the size it was traced at. Zero when nothing was traced.
  UINT m_shadowMaskWidth = 0;
  UINT m_shadowMaskHeight = 0;
  /// Whether the shadow-mask kernel compiled, which is a property of the device and is settled once.
  /// Whether it is asked to run is the project's business, and is decided per frame.
  bool m_shadowMaskAvailable = false;
  /// Whether the first frame has reported what the shadow pass did, so that a failure is logged once
  /// rather than every frame.
  bool m_shadowStatusLogged = false;
  /// The same, for the traced frame: a trace that cannot start will not start on frame two, and a
  /// line per frame would bury everything else in the log.
  bool m_tracedFrameStatusLogged = false;

  /// The rasterized scene a traced frame is composited over, at the size of the view.
  ComPtr<ID3D12Resource> m_tracedSceneColor;
  UINT m_tracedSceneColorWidth = 0;
  UINT m_tracedSceneColorHeight = 0;

  /// The quality of a live frame, which follows whether the camera is being moved. Only the export
  /// path sets m_quality directly, to picture.
  RKRenderQuality interactiveQuality() const;

  /// When the wheel was last turned. A zoom has no release to go with its turn, so where a drag is
  /// observed to be under way a zoom has to be timed.
  std::chrono::steady_clock::time_point m_lastWheelTime{};

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
