/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#include "directxrenderer.h"
#include "directxdevicehelpers.h"
#include "directxdxccompiler.h"
#include "rkstring.h"
#include "rkrenderuniforms.h"
#include "rkrendersettings.h"
#include "atomviewer.h"
#include "bondviewer.h"
#include "proteinribbonmixin.h"
#include "proteinribbonsegmentsupport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace
{
  /// Whether any light in \a source is able to put something into shadow. A rig made only of lights
  /// on the view axis is not: such a light travels with the line of sight, so anything that would
  /// stand between it and a surface stands between the eye and that surface too, and is therefore
  /// what the eye sees instead. Asking first lets the pass be skipped for the camera rig rather than
  /// traced and found to have changed nothing.
  /// Overrides the document's export settings, for tracing a picture out of a project that does not
  /// ask for one. The value is the sample count, so that the cost of converging can be traded against
  /// the wait without rebuilding.
  bool tracePicturesFromEnvironment(uint32_t &sampleCount)
  {
    static const uint32_t requested = []() -> uint32_t {
      wchar_t text[32] = {};
      const DWORD length = GetEnvironmentVariableW(L"IRASPA_D3D12_TRACE_PICTURE", text, 32);
      if (length == 0 || length >= 32) return 0;
      const long value = std::wcstol(text, nullptr, 10);
      // Set but not to a number: the variable being present is the request, so a default stands in.
      if (value <= 0) return 256;
      return static_cast<uint32_t>((std::min)(value, 65536L));
    }();

    sampleCount = requested;
    return requested != 0;
  }
}

DirectXRenderer::DirectXRenderer() = default;

DirectXRenderer::~DirectXRenderer()
{
  release();
}

void DirectXRenderer::setNeedsDisplayCallback(std::function<void()> callback)
{
  m_needsDisplay = std::move(callback);
}

void DirectXRenderer::setSelectionChangedCallback(std::function<void()> callback)
{
  m_selectionChanged = std::move(callback);
}

void DirectXRenderer::setCameraChangedCallback(std::function<void()> callback)
{
  m_cameraChanged = std::move(callback);
}

void DirectXRenderer::notifyCameraChanged()
{
  if (m_cameraChanged)
    m_cameraChanged();
}

void DirectXRenderer::redraw()
{
  markNeedsDisplay();
}

void DirectXRenderer::markNeedsDisplay()
{
  if (m_needsDisplay)
    m_needsDisplay();
}

bool DirectXRenderer::createCommandList()
{
  ID3D12Device *dev = m_device.device();
  if (!dev)
    return false;

  if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_device.commandAllocator(),
                                    nullptr, IID_PPV_ARGS(&m_commandList))))
    return false;
  m_commandList->Close();

  m_commandList4.Reset();
  if (m_device.device5())
    m_commandList->QueryInterface(IID_PPV_ARGS(&m_commandList4));

  delete m_fence;
  m_fence = m_device.createFence();
  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
    m_frameFenceValues[i] = 0;
  m_gpuFrameStarted = false;
  return m_fence != nullptr;
}

void DirectXRenderer::createConstantBuffers()
{
  ID3D12Device *dev = m_device.device();
  m_structureCBVStride = DirectXDeviceHelpers::alignedCBSize(sizeof(RKStructureUniforms));
  m_structureCBVCapacity = 1;
  m_isosurfaceCBVStride = DirectXDeviceHelpers::alignedCBSize(sizeof(RKIsosurfaceUniforms));
  m_isosurfaceCBVCapacity = 1;
  m_blockingPocketCBVStride = DirectXDeviceHelpers::alignedCBSize(sizeof(RKBlockingPocketUniforms));
  m_blockingPocketCBVCapacity = 1;

  RKTransformationUniforms frame{};
  RKStructureUniforms structure{};
  RKLightsUniforms lights{};
  RKIsosurfaceUniforms isosurface{};
  RKBlockingPocketUniforms blockingPocket{};
  RKGlobalAxesUniforms globalAxes(nullptr);

  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
  {
    m_frameCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(sizeof(RKTransformationUniforms)));
    m_lightsCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(sizeof(RKLightsUniforms)));
    m_structureCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, m_structureCBVStride * m_structureCBVCapacity);
    m_isosurfaceCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, m_isosurfaceCBVStride * m_isosurfaceCBVCapacity);
    m_blockingPocketCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, m_blockingPocketCBVStride * m_blockingPocketCBVCapacity);
    m_globalAxesCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(sizeof(RKGlobalAxesUniforms)));

    DirectXDeviceHelpers::writeUploadBuffer(m_frameCBV[i].Get(), &frame, sizeof(frame));
    DirectXDeviceHelpers::writeUploadBuffer(m_structureCBV[i].Get(), &structure, sizeof(structure));
    DirectXDeviceHelpers::writeUploadBuffer(m_lightsCBV[i].Get(), &lights, sizeof(lights));
    DirectXDeviceHelpers::writeUploadBuffer(m_isosurfaceCBV[i].Get(), &isosurface, sizeof(isosurface));
    DirectXDeviceHelpers::writeUploadBuffer(m_blockingPocketCBV[i].Get(), &blockingPocket, sizeof(blockingPocket));
    DirectXDeviceHelpers::writeUploadBuffer(m_globalAxesCBV[i].Get(), &globalAxes, sizeof(globalAxes));
  }
}

void DirectXRenderer::pushStructuresToShaders()
{
  m_atomShader.setRenderStructures(m_structures);
  m_bondShader.setRenderStructures(m_structures);
  m_objectShader.setRenderStructures(m_structures);
  m_unitCellShader.setRenderStructures(m_structures);
  m_localAxesShader.setRenderStructures(m_structures);
  m_ribbonShader.setRenderStructures(m_structures);
  m_ribbonSelectionShader.setRenderStructures(m_structures);
  m_ribbonAmbientOcclusionShader.setRenderStructures(m_structures);
  m_pickingShader.setRenderStructures(m_structures);
  m_selectionShader.setRenderStructures(m_structures);
  m_textShader.setRenderStructures(m_structures);
  m_energySurfaceShader.setRenderStructures(m_structures);
  m_energyVolumeShader.setRenderStructures(m_structures);
  m_blockingPocketsShader.setRenderStructures(m_structures);
}

void DirectXRenderer::createGlowTarget(ID3D12Device *device, int width, int height)
{
  if (!device)
    return;

  m_glowWidth = (std::max)(1, width);
  m_glowHeight = (std::max)(1, height);
  m_glowTexture.Reset();
  m_glowMsaaTexture.Reset();

  // Slot 0 is the resolved glow the blur reads, slot 1 the multisampled one drawn into.
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 2;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_glowRtvHeap));

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(m_glowWidth);
  desc.Height = static_cast<UINT>(m_glowHeight);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = kGlowFormat;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear = {};
  clear.Format = kGlowFormat;
  clear.Color[0] = clear.Color[1] = clear.Color[2] = clear.Color[3] = 0.0f;

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                             IID_PPV_ARGS(&m_glowTexture))))
  {
    std::fprintf(stderr, "DirectXRenderer: failed to create glow render target");
    return;
  }
  m_glowState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  const D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = m_glowRtvHeap->GetCPUDescriptorHandleForHeapStart();
  const UINT rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  rtvDesc.Format = kGlowFormat;
  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  device->CreateRenderTargetView(m_glowTexture.Get(), &rtvDesc, rtvStart);

  // The shells are drawn multisampled so they can be depth-tested against the scene's own
  // multisampled depth, then resolved into the single-sampled texture the blur reads. Resolving the
  // depth instead and testing at pixel rate would collapse each pixel's samples to one value, which
  // beats a shell that clears its atom by a thousandth of a radius everywhere the sphere is steep.
  m_glowSampleCount = (std::max)(1u, m_device.sceneSampleCount());
  if (m_glowSampleCount <= 1)
    return;

  desc.SampleDesc.Count = m_glowSampleCount;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                             IID_PPV_ARGS(&m_glowMsaaTexture))))
  {
    std::fprintf(stderr, "DirectXRenderer: failed to create multisampled glow render target");
    m_glowSampleCount = 1;
    return;
  }
  m_glowMsaaState = D3D12_RESOURCE_STATE_RENDER_TARGET;

  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
  D3D12_CPU_DESCRIPTOR_HANDLE msaaRtv = rtvStart;
  msaaRtv.ptr += rtvStride;
  device->CreateRenderTargetView(m_glowMsaaTexture.Get(), &rtvDesc, msaaRtv);
}

void DirectXRenderer::resizeGlowAndBlur(int width, int height)
{
  ID3D12Device *dev = m_device.device();
  if (!dev)
    return;
  const int w = (std::max)(1, width);
  const int h = (std::max)(1, height);
  createGlowTarget(dev, w, h);
  m_blurShader.resize(dev, w, h);
}

void DirectXRenderer::uploadPendingTextures()
{
  if (!m_commandList || !m_device.device())
    return;

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);
  m_backgroundShader.ensureTextureUploaded(m_device.device(), m_commandList.Get());
  m_textShader.ensureTexturesUploaded(m_device.device(), m_commandList.Get());
  m_globalAxesShader.ensureTexturesUploaded(m_device.device(), m_commandList.Get());
  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);
}

void DirectXRenderer::resetSceneResources()
{
  m_sceneReady = false;
  m_shadowMaskShader.release();
  m_pathTracerShader.release();
  m_pathTracerGeometry.release();
  m_tracedPresentShader.release();
  m_tracedSceneColor.Reset();
  m_tracedSceneColorWidth = 0;
  m_tracedSceneColorHeight = 0;
  m_tracedFrameStatusLogged = false;
  m_shadowMaskWidth = 0;
  m_shadowMaskHeight = 0;
  m_rootSignature.Reset();
  m_srvHeap.Reset();
  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
  {
    m_frameCBV[i].Reset();
    m_structureCBV[i].Reset();
    m_lightsCBV[i].Reset();
    m_isosurfaceCBV[i].Reset();
    m_blockingPocketCBV[i].Reset();
    m_globalAxesCBV[i].Reset();
    m_frameFenceValues[i] = 0;
  }
  m_gpuFrameStarted = false;
  m_structureCBVStride = 0;
  m_structureCBVCapacity = 0;
  m_isosurfaceCBVStride = 0;
  m_isosurfaceCBVCapacity = 0;
  m_blockingPocketCBVStride = 0;
  m_blockingPocketCBVCapacity = 0;

  m_glowTexture.Reset();
  m_glowMsaaTexture.Reset();
  m_glowRtvHeap.Reset();
  m_glowState = D3D12_RESOURCE_STATE_COMMON;
  m_glowMsaaState = D3D12_RESOURCE_STATE_COMMON;
  m_glowSampleCount = 1;
  m_glowWidth = 0;
  m_glowHeight = 0;

  // Destroy GPU children before the device is released / recreated.
  // Several shaders are non-assignable (reference members / QCache), so recreate in place.
  auto recreate = [](auto &obj) {
    using T = std::decay_t<decltype(obj)>;
    obj.~T();
    new (&obj) T();
  };
  recreate(m_backgroundShader);
  recreate(m_atomShader);
  recreate(m_bondShader);
  recreate(m_objectShader);
  recreate(m_unitCellShader);
  recreate(m_localAxesShader);
  recreate(m_ribbonShader);
  recreate(m_ribbonSelectionShader);
  recreate(m_ribbonAmbientOcclusionShader);
  recreate(m_boundingBoxShader);
  recreate(m_energySurfaceShader);
  recreate(m_energyVolumeShader);
  recreate(m_blockingPocketsShader);
  recreate(m_pickingShader);
  recreate(m_selectionShader);
  recreate(m_textShader);
  recreate(m_globalAxesShader);
  recreate(m_blurShader);
  recreate(m_compositeShader);
}

bool DirectXRenderer::initializeScene()
{
  ID3D12Device *dev = m_device.device();
  if (!dev || !m_commandList)
    return false;

  m_rootSignature = DirectXDeviceHelpers::createSceneRootSignature(dev);
  if (!m_rootSignature)
  {
    std::fprintf(stderr, "DirectXRenderer: failed to create root signature");
    m_status = L"Failed to create scene root signature";
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
  srvHeapDesc.NumDescriptors = 1;
  srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(dev->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap))))
  {
    std::fprintf(stderr, "DirectXRenderer: failed to create SRV heap");
    m_status = L"Failed to create SRV heap";
    return false;
  }

  createConstantBuffers();

  const DXGI_FORMAT rtvFormat = m_device.backBufferFormat();
  const DXGI_FORMAT dsvFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

  m_backgroundShader.initialize(dev, m_rootSignature.Get(), rtvFormat,
                                m_srvHeap->GetCPUDescriptorHandleForHeapStart());
  m_atomShader.initialize(dev, m_device.commandQueue(), m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_bondShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_objectShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_unitCellShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_localAxesShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_ribbonShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_ribbonSelectionShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat, kGlowFormat);
  m_ribbonSelectionShader.setRibbonShader(&m_ribbonShader);
  m_ribbonAmbientOcclusionShader.initialize(dev, m_device.commandQueue());
  m_ribbonAmbientOcclusionShader.setRibbonShader(&m_ribbonShader);
  m_atomShader.setRibbonAmbientOcclusionShader(&m_ribbonAmbientOcclusionShader);
  m_boundingBoxShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_energySurfaceShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_energyVolumeShader.initialize(dev, rtvFormat, dsvFormat);
  m_blockingPocketsShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_pickingShader.initialize(dev, m_rootSignature.Get(), m_device.commandQueue());
  m_pickingShader.setRibbonShader(&m_ribbonShader);
  m_pickingShader.setAtomSphereShader(&m_atomShader.atomSphereShader());
  m_pickingShader.setAtomOrthographicImposterShader(&m_atomShader.atomOrthographicImposterShader());
  m_pickingShader.setBondShader(&m_bondShader);
  m_pickingShader.setObjectShader(&m_objectShader);
  m_selectionShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat, kGlowFormat);
  m_textShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_globalAxesShader.initialize(dev, m_rootSignature.Get(), rtvFormat, dsvFormat);
  m_blurShader.initialize(dev, kGlowFormat);
  m_compositeShader.initialize(dev, m_rootSignature.Get(), rtvFormat);
  m_tracedPresentShader.initialize(dev, m_rootSignature.Get(), rtvFormat);
  m_edgeCueingShader.initialize(dev, rtvFormat, m_device.sceneSampleCount());

  // Creates the all-lit fallback whether or not this device can trace, the raster root signature
  // needing something valid bound at t1 either way.
  m_shadowMaskShader.initialize(m_device);
  m_shadowMaskAvailable = m_shadowMaskShader.isReady();
  m_shadowStatusLogged = false;
  DirectXDxcCompiler::logDiagnostics("Shadow mask", m_shadowMaskShader.status());

  const int pixelW = (std::max)(1, static_cast<int>(m_device.width()));
  const int pixelH = (std::max)(1, static_cast<int>(m_device.height()));
  createGlowTarget(dev, pixelW, pixelH);
  m_blurShader.resize(dev, m_glowWidth, m_glowHeight);
  m_pickingShader.resize(dev, pixelW, pixelH);

  if (!m_camera)
    m_camera = std::make_shared<RKCamera>();
  m_camera->updateCameraForWindowResize(m_device.width(), m_device.height());

  if (!m_structures.empty())
    pushStructuresToShaders();

  m_backgroundShader.reload(m_dataSource);
  m_boundingBoxShader.setRenderDataSource(m_dataSource);
  m_globalAxesShader.setRenderDataSource(m_dataSource);

  // Upload initial background texture
  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);
  m_backgroundShader.ensureTextureUploaded(dev, m_commandList.Get());
  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  if (!m_structures.empty())
  {
    m_atomShader.reloadData(dev);
    m_bondShader.reloadData(dev);
    m_objectShader.reloadData(dev);
    m_unitCellShader.reloadData(dev);
    m_localAxesShader.reloadData(dev);
    m_ribbonShader.reloadData(dev);
    m_ribbonSelectionShader.reloadData(dev);
    m_pickingShader.reloadData(dev);
    m_selectionShader.reloadSelectionData(dev);
    m_textShader.reloadData(dev);
    m_energySurfaceShader.reloadData(dev);
    m_energyVolumeShader.reloadData(dev, m_device.commandQueue());
    m_blockingPocketsShader.reloadData(dev);
    // After the ribbon meshes are on the GPU, since the one bake draws atoms and ribbons together.
    m_atomShader.reloadAmbientOcclusionData(dev, m_device.commandQueue(), m_dataSource,
                                            m_ambientOcclusionQuality);
  }
  m_boundingBoxShader.reloadData(dev);
  m_globalAxesShader.reloadData(dev);

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);
  m_textShader.ensureTexturesUploaded(dev, m_commandList.Get());
  m_globalAxesShader.ensureTexturesUploaded(dev, m_commandList.Get());
  m_commandList->Close();
  ID3D12CommandList *fontLists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, fontLists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  updateConstantBuffers();
  m_sceneReady = true;
  return true;
}

bool DirectXRenderer::initializeCommandListAndScene()
{
  if (!createCommandList())
  {
    m_status = L"Failed to create D3D12 command list";
    return false;
  }
  m_ready = true;
  detectRaytracingSupport();
  if (!initializeScene())
  {
    m_ready = false;
    return false;
  }
  return true;
}

void DirectXRenderer::detectRaytracingSupport()
{
  m_supportsRaytracing = false;

  const std::string adapter = RKString(m_device.adapterDescription()).toStdString();

  if (!m_device.device5())
  {
    m_raytracingStatus = adapter + " has no DirectX Raytracing runtime";
  }
  else if (m_device.raytracingTier() < D3D12_RAYTRACING_TIER_1_1)
  {
    // Tier 1.0 hardware could still be traced through a ray-tracing pipeline, which this port
    // does not build: the Metal original intersects rays from a compute kernel, and inline
    // RayQuery is what that maps onto.
    m_raytracingStatus = adapter + " does not support DirectX Raytracing 1.1 (inline RayQuery)";
  }
  else if (!m_commandList4)
  {
    m_raytracingStatus = "the command list does not implement ID3D12GraphicsCommandList4";
  }
  else if (!DirectXDxcCompiler::canCompileInlineRaytracing())
  {
    m_raytracingStatus = "no shader compiler for inline ray tracing: "
                         + (DirectXDxcCompiler::isAvailable()
                                ? std::string("dxcompiler.dll cannot compile cs_6_5")
                                : DirectXDxcCompiler::unavailableReason());
  }
  else
  {
    m_supportsRaytracing = true;
    m_raytracingStatus = adapter + " supports DirectX Raytracing 1.1 (inline RayQuery)";
    if (m_device.isWarpAdapter())
      m_raytracingStatus += ", traversed in software";
  }

  // The machine-wide settings need to know this to decide what to offer and what to default to, and
  // it cannot change while the process runs.
  RKRenderSettings::setRaytracingCapability(m_supportsRaytracing, !m_device.isWarpAdapter());

  DirectXDxcCompiler::logDiagnostics("Ray tracing", m_raytracingStatus);
}

bool DirectXRenderer::wantsInteractiveTracing() const
{
  return m_supportsRaytracing && m_commandList4 && !m_structures.empty() && m_dataSource &&
         RKRenderSettings::shared().interactiveRenderMode() == RKRenderMode::rayTracing;
}

bool DirectXRenderer::prepareInteractiveTracing()
{
  // Compiled on the first traced frame rather than at start-up: the two kernels cost real time to
  // compile, and a session that never turns tracing on should not pay for them.
  if (!m_pathTracerShader.isReady())
  {
    m_pathTracerShader.initialize(m_device);
    DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerShader.status());
    if (!m_pathTracerShader.isReady())
      return false;
  }

  // Repacks and rebuilds only when something has invalidated the geometry, and the build has to be
  // waited on, so the frame that follows a change to the structure costs that wait.
  const bool built = m_pathTracerGeometry.build(m_device);
  if (!m_tracedFrameStatusLogged)
  {
    m_tracedFrameStatusLogged = true;
    DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerGeometry.status());
  }
  return built;
}

ID3D12Resource *DirectXRenderer::ensureTracedSceneColor(UINT width, UINT height)
{
  if (m_tracedSceneColor && m_tracedSceneColorWidth == width && m_tracedSceneColorHeight == height)
    return m_tracedSceneColor.Get();

  ID3D12Device *dev = m_device.device();
  if (!dev || width == 0 || height == 0)
    return nullptr;

  const DXGI_FORMAT format = m_device.backBufferFormat();

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear = {};
  clear.Format = format;
  clear.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  // Created in the state the resolve kernel reads it in, which is also the state
  // resolveSceneColorTo() expects to find it in and returns it to.
  m_tracedSceneColor.Reset();
  if (FAILED(dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear,
                                          IID_PPV_ARGS(&m_tracedSceneColor))))
  {
    m_tracedSceneColorWidth = 0;
    m_tracedSceneColorHeight = 0;
    return nullptr;
  }

  m_tracedSceneColorWidth = width;
  m_tracedSceneColorHeight = height;
  return m_tracedSceneColor.Get();
}

bool DirectXRenderer::recordTracedFrame(int width, int height)
{
  // The depth of what the raster pass did draw, which is what decides where the traced image is
  // allowed to show. Left in a state a compute shader can read rather than the pixel-shader one the
  // volume pass wants.
  ID3D12Resource *sceneDepth =
      m_device.resolveSceneDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12Resource *sceneColor =
      ensureTracedSceneColor(static_cast<UINT>(width), static_cast<UINT>(height));
  if (!sceneDepth || !sceneColor)
    return false;

  m_device.resolveSceneColorTo(m_commandList.Get(), sceneColor,
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  DirectXPathTracerShader::Settings settings;
  settings.maximumBounces =
      static_cast<uint32_t>(RKRenderSettings::shared().interactiveMaximumBounces());
  // A look rather than a cost, so a traced frame is graded the same way a rasterized one is.
  settings.ambientOcclusionStrength =
      m_dataSource ? float(std::clamp(m_dataSource->renderAmbientOcclusionStrength(), 0.0, 1.0))
                   : 0.0f;

  const UINT samples =
      static_cast<UINT>((std::max)(1, RKRenderSettings::samplesPerInteractiveFrame(m_quality)));

  return m_pathTracerShader.encodeInteractive(
      m_commandList4.Get(), m_device, m_pathTracerGeometry, frameCB()->GetGPUVirtualAddress(),
      lightsCB()->GetGPUVirtualAddress(), sceneColor, sceneDepth, static_cast<UINT>(width),
      static_cast<UINT>(height), settings, samples);
}

void DirectXRenderer::presentTracedFrame(D3D12_CPU_DESCRIPTOR_HANDLE destRtv, int width, int height)
{
  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MaxDepth = 1.0f;

  D3D12_RECT scissor = {};
  scissor.right = width;
  scissor.bottom = height;

  // The tracer swapped in its own compute root signature and descriptor heap, so the graphics ones
  // have to be put back before anything draws again.
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());

  m_commandList->OMSetRenderTargets(1, &destRtv, FALSE, nullptr);
  m_commandList->RSSetViewports(1, &viewport);
  m_commandList->RSSetScissorRects(1, &scissor);

  // A traced image is cued exactly as a rasterized one is, from the depth and the tag the resolve
  // kernel wrote in place of the depth buffer and the stencil the tracer never went through.
  ID3D12Resource *tracedDepth = m_pathTracerShader.compositeDepthBuffer();
  ID3D12Resource *tracedCueMask = m_pathTracerShader.compositeCueMaskBuffer();
  if (drawsEdgeCues() && m_edgeCueingShader.canPaintTraced() && tracedDepth && tracedCueMask)
  {
    m_device.transitionResource(tracedDepth, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_device.transitionResource(tracedCueMask, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    m_edgeCueingShader.paintTraced(m_commandList.Get(), destRtv, frameCB()->GetGPUVirtualAddress(),
                                   m_pathTracerShader.compositeTexture(), tracedDepth,
                                   tracedCueMask, width, height);

    m_device.transitionResource(tracedDepth, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_device.transitionResource(tracedCueMask, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return;
  }

  m_tracedPresentShader.paint(m_device.device(), m_commandList.Get(),
                              m_pathTracerShader.compositeTexture());
}

bool DirectXRenderer::tracesShadows() const
{
  if (!m_dataSource || !m_dataSource->wantsShadows())
    return false;

  // An export traces them wherever it is run, so that the picture a document describes does not
  // depend on the machine it is opened on. A frame of the render view asks the machine-wide setting
  // first, which is off by default where the rays would be traced in a shader.
  if (m_quality != RKRenderQuality::picture && !RKRenderSettings::shared().interactiveShadows())
    return false;

  return m_supportsRaytracing && m_shadowMaskAvailable;
}

void DirectXRenderer::recordShadowMask(int width, int height)
{
  // Whatever happens below, the raster passes have to be told the truth about what was traced: a
  // mask size that outlives its mask would have them read stale visibility, or read past the end of
  // a buffer that was never filled, which reports every light shadowed and turns the model black.
  m_shadowMaskWidth = 0;
  m_shadowMaskHeight = 0;

  if (tracesShadows() && m_commandList4 && !m_structures.empty())
  {
    // Repacks and rebuilds only when something has invalidated the geometry. The build has to be
    // waited on, so it costs a frame; the mask itself is traced every frame, the camera moving being
    // enough to change it.
    const bool built = m_pathTracerGeometry.build(m_device);
    if (built)
    {
      m_shadowMaskShader.encode(m_commandList4.Get(), m_device, m_pathTracerGeometry,
                                frameCB()->GetGPUVirtualAddress(),
                                lightsCB()->GetGPUVirtualAddress(), static_cast<UINT>(width),
                                static_cast<UINT>(height));
      m_shadowMaskWidth = m_shadowMaskShader.maskWidth();
      m_shadowMaskHeight = m_shadowMaskShader.maskHeight();
    }

    // Once rather than every frame: a pass that cannot run will not start running on frame two, and
    // a line per frame would bury everything else in the log.
    if (!m_shadowStatusLogged)
    {
      m_shadowStatusLogged = true;
      DirectXDxcCompiler::logDiagnostics("Shadow mask", built ? m_shadowMaskShader.status()
                                                             : m_pathTracerGeometry.status());
    }
  }

  // The frame uniforms were written before any of this was known, so the two fields that describe
  // the mask are patched in now. Nothing has been submitted yet — the whole frame goes to the queue
  // as one list once recording finishes — so the constant buffer can still be written to.
  if (ID3D12Resource *frameConstants = frameCB())
  {
    const float maskSize[2] = {static_cast<float>(m_shadowMaskWidth),
                               static_cast<float>(m_shadowMaskHeight)};
    DirectXDeviceHelpers::writeUploadBufferAt(frameConstants, maskSize, sizeof(maskSize),
                                              offsetof(RKTransformationUniforms, shadowMaskWidth));
  }
}

void DirectXRenderer::bindShadowMask()
{
  if (!m_commandList)
    return;

  // A root shader-resource view has no null binding, so the all-lit buffer stands in whenever
  // nothing was traced. The shaders will not look at it — a mask size of zero has them report every
  // light lit without reading anything — but the root signature has to be satisfied regardless.
  ID3D12Resource *mask = (m_shadowMaskWidth > 0) ? m_shadowMaskShader.maskBuffer()
                                                 : m_shadowMaskShader.allLitBuffer();
  if (!mask)
    return;

  m_commandList->SetGraphicsRootShaderResourceView(DirectXDeviceHelpers::kShadowMaskRootParameter,
                                                   mask->GetGPUVirtualAddress());
}

bool DirectXRenderer::initializeComposition(UINT width, UINT height)
{
  release();
  if (!m_device.initializeForComposition(width, height))
  {
    m_status = L"Failed to initialize D3D12 composition swap chain";
    return false;
  }
  if (!initializeCommandListAndScene())
    return false;
  m_status = L"DirectX 12 composition scene ready";
  return true;
}

bool DirectXRenderer::initializeHwnd(HWND hwnd, UINT width, UINT height)
{
  release();
  if (!m_device.initializeForHwnd(hwnd, width, height))
  {
    m_status = L"Failed to initialize D3D12 HWND swap chain";
    return false;
  }
  if (!initializeCommandListAndScene())
    return false;
  m_status = L"DirectX 12 HWND scene ready";
  return true;
}

bool DirectXRenderer::initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter,
                                          bool requireRaytracing)
{
  release();
  if (!m_device.initializeOffscreen(width, height, avoidAdapter, requireRaytracing))
  {
    m_status = L"Failed to initialize an offscreen D3D12 device";
    return false;
  }
  if (!initializeCommandListAndScene())
    return false;
  m_status = L"DirectX 12 offscreen scene ready";
  return true;
}

void DirectXRenderer::release()
{
  m_needsDisplay = nullptr;
  m_selectionChanged = nullptr;

  if (m_ready && m_fence)
    m_device.waitForGPU(m_fence);

  m_ready = false;
  m_supportsRaytracing = false;
  m_raytracingStatus = "the ray-tracing capability has not been looked at";
  resetSceneResources();
  m_commandList4.Reset();
  m_commandList.Reset();
  delete m_fence;
  m_fence = nullptr;
  m_device.release();
}

void DirectXRenderer::resize(UINT width, UINT height)
{
  if (!m_ready)
    return;
  m_device.waitForGPU(m_fence);
  m_device.resize(width, height);
  if (m_camera)
    m_camera->updateCameraForWindowResize(width, height);
  if (m_sceneReady)
  {
    const int pixelW = (std::max)(1, static_cast<int>(m_device.width()));
    const int pixelH = (std::max)(1, static_cast<int>(m_device.height()));
    m_pickingShader.resize(m_device.device(), pixelW, pixelH);
    resizeGlowAndBlur(pixelW, pixelH);
  }
}

void DirectXRenderer::setClearColor(float r, float g, float b, float a)
{
  m_clearColor[0] = r;
  m_clearColor[1] = g;
  m_clearColor[2] = b;
  m_clearColor[3] = a;
  markNeedsDisplay();
}

void DirectXRenderer::setStatusMessage(const std::wstring &message)
{
  m_status = message;
}

void DirectXRenderer::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  // Swapping structures may drop GPU resources still referenced by the
  // in-flight frame (crashes with OBJECT_DELETED_WHILE_STILL_IN_USE).
  if (m_sceneReady && m_fence)
    m_device.waitForGPU(m_fence);

  m_structures = std::move(structures);
  // The acceleration structures bake the geometry, so they are dropped here and rebuilt on the next
  // frame that traces, and what the rebuild made of the new geometry is worth reporting again.
  m_pathTracerGeometry.setRenderStructures(m_structures);
  m_shadowStatusLogged = false;
  if (m_sceneReady)
    pushStructuresToShaders();
  markNeedsDisplay();
}

void DirectXRenderer::setRenderDataSource(std::shared_ptr<RKRenderDataSource> source)
{
  // The shader reloads below release textures/buffers of the previous scene;
  // drain the queue first so the in-flight frame can't still reference them.
  if (m_sceneReady && m_fence)
    m_device.waitForGPU(m_fence);

  m_dataSource = std::move(source);

  if (m_dataSource)
  {
    if (std::shared_ptr<RKCamera> camera = m_dataSource->camera())
    {
      // Cocoa switchToCurrentProject: keep the archived worldRotation (gallery /
      // document orientation) and only re-fit zoom to the current window. A full
      // resetCameraToDirection would zero the saved angle and is reserved for the
      // explicit Camera → Reset action.
      m_camera = camera;
      m_camera->resetForNewBoundingBox(m_dataSource->renderBoundingBox());
      m_camera->updateCameraForWindowResize(m_device.width(), m_device.height());
      m_camera->resetCameraDistance();
    }
  }

  if (m_sceneReady)
  {
    m_backgroundShader.reload(m_dataSource);
    m_boundingBoxShader.setRenderDataSource(m_dataSource);
    m_globalAxesShader.setRenderDataSource(m_dataSource);
    reloadData();
  }
}

void DirectXRenderer::reloadData()
{
  if (!m_sceneReady)
    return;

  // AO path (and style changes that rebuild AO heaps/textures) must not run while a
  // previously submitted frame still references those GPU objects.
  if (m_fence)
    m_device.waitForGPU(m_fence);

  ID3D12Device *dev = m_device.device();
  pushStructuresToShaders();
  m_backgroundShader.reload(m_dataSource);
  m_atomShader.reloadData(dev);
  m_bondShader.reloadData(dev);
  m_objectShader.reloadData(dev);
  m_unitCellShader.reloadData(dev);
  m_localAxesShader.reloadData(dev);
  m_ribbonShader.reloadData(dev);
  m_ribbonSelectionShader.reloadData(dev);
  m_pickingShader.reloadData(dev);
  m_selectionShader.reloadSelectionData(dev);
  m_boundingBoxShader.reloadData(dev);
  m_textShader.reloadData(dev);
  m_globalAxesShader.reloadData(dev);
  m_energySurfaceShader.reloadData(dev);
  m_energyVolumeShader.reloadData(dev, m_device.commandQueue());
  m_blockingPocketsShader.reloadData(dev);

  try
  {
    m_atomShader.reloadAmbientOcclusionData(dev, m_device.commandQueue(), m_dataSource,
                                            m_ambientOcclusionQuality);
  }
  catch (const std::exception &ex)
  {
    std::fprintf(stderr, "DirectXRenderer: AO reload failed: %s", ex.what());
    m_status = L"Ambient occlusion reload failed";
  }
  catch (...)
  {
    std::fprintf(stderr, "DirectXRenderer: AO reload failed (unknown)");
    m_status = L"Ambient occlusion reload failed";
  }

  uploadPendingTextures();
  updateStructureUniforms();
  updateIsosurfaceUniforms();
  updateGlobalAxesUniforms();
  markNeedsDisplay();
}

void DirectXRenderer::invalidateCachedAmbientOcclusionTextures(
    std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  m_atomShader.invalidateCachedAmbientOcclusionTextures(structures);
  m_ribbonAmbientOcclusionShader.invalidateCachedAmbientOcclusionTexture(structures);
}

void DirectXRenderer::invalidateCachedIsosurfaces(std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  m_energySurfaceShader.invalidateIsosurface(structures);
  m_energyVolumeShader.invalidateIsosurface(structures);
}

void DirectXRenderer::reloadSelectionData()
{
  if (!m_sceneReady)
    return;

  // The selection instance buffers are freed and recreated here; a previously
  // submitted frame may still be reading them on the GPU (rapid Ctrl/Shift
  // multi-select clicks hit this race immediately).
  if (m_fence)
    m_device.waitForGPU(m_fence);

  m_selectionShader.reloadSelectionData(m_device.device());
  // Which ribbon ranges are selected is worked out here too, rather than per frame, since it comes
  // from the atom tree and only changes when the selection does.
  m_ribbonSelectionShader.reloadData(m_device.device());
  markNeedsDisplay();
}

std::array<int, 4> DirectXRenderer::pickTexture(int x, int y, int width, int height)
{
  if (!m_sceneReady || !frameCB() || !structureCB())
    return std::array<int, 4>{};

  if (m_fence)
    m_device.waitForGPU(m_fence);
  updateConstantBuffers();

  m_pickingShader.setOrthographic(m_camera && m_camera->isOrthographic());
  return m_pickingShader.pickTexture(frameCB()->GetGPUVirtualAddress(),
                                     structureCB()->GetGPUVirtualAddress(),
                                     m_structureCBVStride,
                                     x, y, width, height);
}

void DirectXRenderer::onPointerPressed(const Dx12Input::PointerEvent &e)
{
  if (!m_camera)
    return;

  // Right-drag = pan, middle-drag = truck (screen-space translate).
  if (e.button == Dx12Input::Button::right || e.button == Dx12Input::Button::middle)
  {
    m_panLastX = e.x;
    m_panLastY = e.y;
    m_tracking = (e.button == Dx12Input::Button::middle) ? Tracking::trucking : Tracking::panning;
    return;
  }

  if (e.button != Dx12Input::Button::left)
    return;

  // Shift+left is handled by the host as a rubber-band selection drag.
  if (static_cast<uint32_t>(e.modifiers) & static_cast<uint32_t>(Dx12Input::Modifier::shift))
  {
    m_tracking = Tracking::none;
    return;
  }

  m_pressX = e.x;
  m_pressY = e.y;
  m_tracking = Tracking::backgroundClick;
  m_camera->setTrackBallRotation(simd_quatd(1.0, double3(0.0, 0.0, 0.0)));
  const double h = static_cast<double>((std::max)(1u, m_device.height()));
  const double w = static_cast<double>((std::max)(1u, m_device.width()));
  // Match Qt/OpenGL: use widget Y as-is (top-left origin).
  m_trackBall.start(e.x, e.y, 0, 0, w, h);
}

void DirectXRenderer::onPointerMoved(const Dx12Input::PointerEvent &e)
{
  if (!m_camera)
    return;

  if (m_tracking == Tracking::panning || m_tracking == Tracking::trucking)
  {
    const float dx = m_panLastX - e.x;
    const float dy = m_panLastY - e.y;
    const double distance = m_camera->distance().z;
    const double scale = distance / 1500.0;
    if (m_tracking == Tracking::panning)
      m_camera->setPanning(dx * scale, -dy * scale);
    else
      m_camera->setTrucking(dx * scale, -dy * scale);
    m_panLastX = e.x;
    m_panLastY = e.y;
    markNeedsDisplay();
    return;
  }

  if (m_tracking == Tracking::backgroundClick)
  {
    const float dx = e.x - m_pressX;
    const float dy = e.y - m_pressY;
    if (std::sqrt(dx * dx + dy * dy) > kClickDragThresholdPx)
      m_tracking = Tracking::rotating;
    else
      return;
  }

  if (m_tracking != Tracking::rotating)
    return;

  simd_quatd trackBallRotation = m_trackBall.rollToTrackball(e.x, e.y);
  m_camera->setTrackBallRotation(trackBallRotation);
  markNeedsDisplay();
}

void DirectXRenderer::onPointerReleased(const Dx12Input::PointerEvent &e)
{
  if (m_tracking == Tracking::panning || m_tracking == Tracking::trucking)
  {
    m_tracking = Tracking::none;
    markNeedsDisplay();
    notifyCameraChanged();
    return;
  }

  if (m_tracking == Tracking::backgroundClick)
  {
    const bool toggle = (static_cast<uint32_t>(e.modifiers) &
                         static_cast<uint32_t>(Dx12Input::Modifier::ctrl)) != 0;
    applyPickAt(static_cast<int>(e.x), static_cast<int>(e.y), toggle);
    m_tracking = Tracking::none;
    return;
  }

  if (m_tracking != Tracking::rotating || !m_camera)
  {
    m_tracking = Tracking::none;
    return;
  }

  simd_quatd trackBallRotation = m_trackBall.rollToTrackball(e.x, e.y);
  simd_quatd worldRotation = m_camera->worldRotation();
  m_camera->setWorldRotation(trackBallRotation * worldRotation);
  m_camera->setTrackBallRotation(simd_quatd(1.0, double3(0.0, 0.0, 0.0)));
  m_tracking = Tracking::none;
  markNeedsDisplay();
  notifyCameraChanged();
}

void DirectXRenderer::onWheel(const Dx12Input::WheelEvent &e)
{
  if (!m_camera)
    return;
  // WinUI wheel delta is typically ±120 per notch; match Qt angleDelta/40 feel.
  m_camera->increaseDistance(e.delta / 40.0);
  m_lastWheelTime = std::chrono::steady_clock::now();
  markNeedsDisplay();
  notifyCameraChanged();
}

RKRenderQuality DirectXRenderer::interactiveQuality() const
{
  // A drag says plainly that it is under way, having both a press and a release.
  if (m_tracking == Tracking::rotating || m_tracking == Tracking::panning ||
      m_tracking == Tracking::trucking)
    return RKRenderQuality::medium;

  // A wheel does not, so a zoom is taken to be under way for a short while after the last notch.
  // Long enough to cover the gap between notches of one gesture, short enough not to be noticed
  // once the gesture ends.
  const auto sinceWheel = std::chrono::steady_clock::now() - m_lastWheelTime;
  if (sinceWheel < std::chrono::milliseconds(150))
    return RKRenderQuality::medium;

  return RKRenderQuality::high;
}

void DirectXRenderer::resetCameraView()
{
  if (!m_camera)
    return;
  if (m_dataSource)
    m_camera->resetForNewBoundingBox(m_dataSource->renderBoundingBox());
  m_camera->resetCameraToDirection();
  m_camera->updateCameraForWindowResize(m_device.width(), m_device.height());
  m_status = L"Camera reset";
  markNeedsDisplay();
  notifyCameraChanged();
}

void DirectXRenderer::setCameraOrthographic(bool orthographic)
{
  if (!m_camera)
    return;
  if (orthographic)
    m_camera->setCameraToOrthographic();
  else
    m_camera->setCameraToPerspective();
  m_status = orthographic ? L"Orthographic" : L"Perspective";
  markNeedsDisplay();
}

void DirectXRenderer::zoomCamera(double delta)
{
  if (!m_camera)
    return;
  m_camera->increaseDistance(delta);
  markNeedsDisplay();
  notifyCameraChanged();
}

void DirectXRenderer::clearAllSelections()
{
  for (auto &scene : m_structures)
  {
    for (auto &object : scene)
    {
      if (auto atomViewer = std::dynamic_pointer_cast<AtomViewer>(object))
        atomViewer->clearSelection();
    }
  }
}

void DirectXRenderer::applyPickAt(int x, int y, bool toggle)
{
  const int w = (std::max)(1, static_cast<int>(m_device.width()));
  const int h = (std::max)(1, static_cast<int>(m_device.height()));
  const std::array<int, 4> pixel = pickTexture(x, y, w, h);

  const int kind = pixel[0];
  // A ribbon pick carries a segment and a residue index, so it packs scene and movie into one channel
  // instead of spending a channel on each.
  const bool isRibbonPick = kind == ProteinRibbonSegmentSupport::ribbonPickObjectType;
  const int sceneId = isRibbonPick ? ((pixel[1] >> 16) & 0xFFFF) : pixel[1];
  const int movieId = isRibbonPick ? (pixel[1] & 0xFFFF) : pixel[2];
  const int objectId = pixel[3];

  if (kind == 0)
  {
    clearAllSelections();
    reloadSelectionData();
    m_status = L"Selection cleared";
    if (m_selectionChanged)
      m_selectionChanged();
    return;
  }

  if (sceneId < 0 || sceneId >= static_cast<int>(m_structures.size()))
    return;
  if (movieId < 0 || movieId >= static_cast<int>(m_structures[sceneId].size()))
    return;

  auto &object = m_structures[sceneId][movieId];

  // A plain click replaces the whole selection of the scene, not just the selection of the structure
  // that was hit: without this, clicking a residue of one chain and then a residue of another leaves
  // both lit up. Only a toggling click adds to what is already selected.
  const auto replaceSelection = [this, toggle]()
  {
    if (!toggle)
      clearAllSelections();
  };

  if (kind == 1)
  {
    if (auto atomViewer = std::dynamic_pointer_cast<AtomViewer>(object))
    {
      replaceSelection();
      if (!toggle)
        atomViewer->setAtomSelection(objectId);
      else
        atomViewer->toggleAtomSelection(objectId);
      reloadSelectionData();
      m_status = L"Atom selected " + std::to_wstring(objectId);
      if (m_selectionChanged)
        m_selectionChanged();
    }
  }
  else if (kind == 2)
  {
    if (auto bondViewer = std::dynamic_pointer_cast<BondViewer>(object))
    {
      replaceSelection();
      if (!toggle)
        bondViewer->setBondSelection(objectId);
      else
        bondViewer->toggleBondSelection(objectId);
      reloadSelectionData();
      m_status = L"Bond selected " + std::to_wstring(objectId);
      if (m_selectionChanged)
        m_selectionChanged();
    }
  }
  else if (isRibbonPick)
  {
    // A ribbon click selects the residue group node the pixel belongs to, the way a click on an atom
    // selects that atom. Whole-segment selection is reachable from the atom tree.
    if (auto ribbon = std::dynamic_pointer_cast<ProteinRibbonMixin>(object))
    {
      const int segmentId = pixel[2];
      const int residueId = pixel[3];
      replaceSelection();
      const auto action = toggle ? ProteinRibbonMixin::RibbonPickAction::toggleResidue
                                 : ProteinRibbonMixin::RibbonPickAction::replaceResidue;
      const bool picked = ribbon->applyRibbonPick(segmentId, residueId, action, false);
      if (picked || !toggle)
      {
        reloadSelectionData();
        m_status = picked ? L"Residue selected " + std::to_wstring(residueId)
                          : L"Selection cleared";
        if (m_selectionChanged)
          m_selectionChanged();
      }
    }
  }
}

// Port of Cocoa RenderTabViewController.selectInRectangle: build a frustum
// from the screen rectangle and select every visible atom/bond inside it.
void DirectXRenderer::applyRectangleSelection(double x0, double y0, double x1, double y1, bool extend)
{
  if (!m_camera)
    return;

  const double w = static_cast<double>((std::max)(1u, m_device.width()));
  const double h = static_cast<double>((std::max)(1u, m_device.height()));

  const double left = (std::min)(x0, x1);
  const double right = (std::max)(x0, x1);
  const double top = (std::min)(y0, y1);
  const double bottom = (std::max)(y0, y1);
  if (right - left < 1.0 || bottom - top < 1.0)
    return;

  const RKRect viewPort(0, 0, static_cast<int>(w), static_cast<int>(h));

  // myGluUnProject expects GL-style bottom-left origin; pointer coords are top-left.
  auto unproject = [&](double x, double yTopLeft, double z) -> double3
  {
    return m_camera->myGluUnProject(double3(x, h - yTopLeft, z), viewPort);
  };

  const double3 points0 = unproject(left, top, 0.0);
  const double3 points1 = unproject(left, top, 1.0);
  const double3 points2 = unproject(left, bottom, 0.0);
  const double3 points3 = unproject(left, bottom, 1.0);
  const double3 points4 = unproject(right, bottom, 0.0);
  const double3 points5 = unproject(right, bottom, 1.0);
  const double3 points6 = unproject(right, top, 0.0);
  const double3 points7 = unproject(right, top, 1.0);

  const double3 plane0 = double3::cross(points0 - points1, points0 - points2).normalise();
  const double3 plane1 = double3::cross(points2 - points3, points2 - points4).normalise();
  const double3 plane2 = double3::cross(points4 - points5, points4 - points6).normalise();
  const double3 plane3 = double3::cross(points6 - points7, points6 - points0).normalise();

  std::function<bool(double3)> insideFrustum = [=](double3 position) -> bool
  {
    return double3::dot(position - points0, plane0) < 0.0 &&
           double3::dot(position - points2, plane1) < 0.0 &&
           double3::dot(position - points4, plane2) < 0.0 &&
           double3::dot(position - points6, plane3) < 0.0;
  };

  size_t selectedCount = 0;
  for (auto &scene : m_structures)
  {
    for (auto &object : scene)
    {
      // A protein drawn as a ribbon is selected by the residue, not by the atom: the ribbon is what
      // the region was drawn over, and its atoms are usually not on screen at all (Cocoa
      // selectInRectangle).
      if (auto ribbon = std::dynamic_pointer_cast<ProteinRibbonMixin>(object);
          ribbon && ribbon->drawRibbon())
      {
        if (object->isVisible())
          selectedCount += ribbon->selectRibbonResiduesInRegion(insideFrustum, extend);
        else if (!extend)
        {
          if (auto atomViewer = std::dynamic_pointer_cast<AtomViewer>(object))
            atomViewer->clearSelection();
        }
        continue;
      }

      // Bonds first: the atom-selection setters below run
      // correctBondSelectionDueToAtomSelection(), which unions in the bonds
      // connected to selected atoms (matches Cocoa setSelectionFor).
      if (auto bondViewer = std::dynamic_pointer_cast<BondViewer>(object))
      {
        BondSelectionIndexSet bondIds = bondViewer->filterCartesianBondPositions(insideFrustum);
        if (std::shared_ptr<SKBondSetController> controller = bondViewer->bondSetController())
        {
          if (extend)
            controller->addSelectedObjects(bondIds);
          else
            controller->setSelectionIndexSet(bondIds);
        }
      }
      if (auto atomViewer = std::dynamic_pointer_cast<AtomViewer>(object))
      {
        std::set<int> atomIds = atomViewer->filterCartesianAtomPositions(insideFrustum);
        selectedCount += atomIds.size();
        if (extend)
          atomViewer->addToAtomSelection(atomIds);
        else
          atomViewer->setAtomSelection(atomIds);
      }
    }
  }

  reloadSelectionData();
  m_status = L"Selected " + std::to_wstring(selectedCount) + L" objects";
  if (m_selectionChanged)
    m_selectionChanged();
}

bool DirectXRenderer::drawsEdgeCues() const
{
  for (const std::vector<std::shared_ptr<RKRenderObject>> &movie : m_structures)
  {
    for (const std::shared_ptr<RKRenderObject> &structure : movie)
    {
      if (!structure || !structure->isVisible())
        continue;

      auto *atoms = dynamic_cast<RKRenderAtomSource *>(structure.get());
      if (atoms && atoms->drawAtoms() && atoms->atomEdgeCueing() != RKEdgeCueing::off)
        return true;

      auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(structure.get());
      if (ribbon && ribbon->drawRibbon() && ribbon->ribbonEdgeCueing() != RKEdgeCueing::off)
        return true;
    }
  }
  return false;
}

bool DirectXRenderer::recordEdgeCues(D3D12_CPU_DESCRIPTOR_HANDLE destinationRtv, int width, int height)
{
  // Nothing asked for a cue: the colour goes to the frame the way it always did, which is one copy
  // of the image fewer than drawing the cues costs.
  if (!drawsEdgeCues() || !m_edgeCueingShader.isReady())
    return false;

  ID3D12Resource *colour = m_edgeCueingShader.sceneTexture(m_device.device(), width, height);
  if (!colour)
    return false;

  // The depth first: resolving it moves the depth buffer through states of its own, and the stencil
  // read below is a state of the same resource.
  ID3D12Resource *depth = m_device.resolveSceneDepth(m_commandList.Get());
  if (!depth)
    return false;

  m_device.resolveSceneColorTo(m_commandList.Get(), colour, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  m_device.beginSceneStencilRead(m_commandList.Get());
  m_edgeCueingShader.paint(m_commandList.Get(), destinationRtv, frameCB()->GetGPUVirtualAddress(),
                           colour, depth, m_device.depthStencilResource(), width, height);
  m_device.endSceneStencilRead(m_commandList.Get());
  return true;
}

void DirectXRenderer::updateTransformUniforms()
{
  if (!frameCB())
    return;

  double4x4 projectionMatrix = double4x4();
  double4x4 modelViewMatrix = double4x4();
  double4x4 modelMatrix = double4x4();
  double4x4 viewMatrix = double4x4();
  double4x4 axesProjectionMatrix = double4x4();
  double4x4 axesModelViewMatrix = double4x4();
  double bloomLevel = 1.0;
  double bloomPulse = 1.0;
  bool isOrthographic = true;

  if (m_camera)
  {
    projectionMatrix = m_camera->projectionMatrix();
    modelViewMatrix = m_camera->modelViewMatrix();
    modelMatrix = m_camera->modelMatrix();
    viewMatrix = m_camera->viewMatrix();
    isOrthographic = m_camera->isOrthographic();

    if (m_dataSource && m_dataSource->axes())
    {
      double totalAxesSize = m_dataSource->axes()->totalAxesSize();
      axesProjectionMatrix = m_camera->axesProjectionMatrix(totalAxesSize);
      axesModelViewMatrix = m_camera->axesModelViewMatrix();
    }

    bloomLevel = m_camera->bloomLevel();
    bloomPulse = m_camera->bloomPulse();
  }

  // Keep OpenGL-style projection (Y-up NDC). D3D Z remapping is done in shaders.
  // Do not flip m22 — that inverts the scene vs Qt/Cocoa.

  RKTransformationUniforms transformationUniforms(
      projectionMatrix, modelViewMatrix, modelMatrix, viewMatrix,
      axesProjectionMatrix, axesModelViewMatrix, isOrthographic,
      bloomLevel, bloomPulse, static_cast<int>(m_device.sceneSampleCount()));

  // Edge cueing, after Tarini et al. section 5. The strengths and widths are one setting for the
  // whole image, so they are sent whenever anything in the scene asked for a cue at all; which cues
  // a pixel takes is decided from the structure that drew it. Left at zero otherwise, which is what
  // the pass reads to mean it has nothing to do.
  if (drawsEdgeCues())
  {
    transformationUniforms.edgeCueing = float4(RKEdgeCueingParameters::contourStrength,
                                               RKEdgeCueingParameters::contourWidthInPixels,
                                               RKEdgeCueingParameters::haloStrength,
                                               RKEdgeCueingParameters::haloRadiusInPixels);

    const double sceneRadius = m_camera ? (std::max)(m_camera->boundingBox().boundingSphereRadius(), 1.0)
                                        : 1.0;
    transformationUniforms.edgeCueingContourDepth =
        static_cast<float>(RKEdgeCueingParameters::contourDepthFraction * sceneRadius);
    transformationUniforms.edgeCueingHaloDepth =
        static_cast<float>(RKEdgeCueingParameters::haloDepthFraction * sceneRadius);
  }

  DirectXDeviceHelpers::writeUploadBuffer(frameCB(), &transformationUniforms, sizeof(transformationUniforms));
}

// Transparent objects must be composited back-to-front (farthest from the camera first),
// otherwise the blending between overlapping transparent movies is wrong.
std::vector<DirectXRenderer::RenderOrderItem> DirectXRenderer::backToFrontRenderOrder() const
{
  struct Entry
  {
    RenderOrderItem item;
    double depth;
  };

  std::vector<Entry> entries;
  size_t index = 0;
  for (size_t i = 0; i < m_structures.size(); ++i)
  {
    for (size_t j = 0; j < m_structures[i].size(); ++j)
    {
      double depth = 0.0;
      const std::shared_ptr<RKRenderObject> &structure = m_structures[i][j];
      if (m_camera && structure && structure->cell())
      {
        const double3 center = structure->cell()->boundingBox().center();
        const double4x4 modelMatrix =
            double4x4::AffinityMatrixToTransformationAroundArbitraryPointWithTranslation(
                double4x4(structure->orientation()), center, structure->origin());
        const double4 worldCenter = modelMatrix * double4(center.x, center.y, center.z, 1.0);
        const double4 viewCenter = m_camera->modelViewMatrix() * worldCenter;
        depth = viewCenter.z;
      }
      entries.push_back({{i, j, index}, depth});
      ++index;
    }
  }

  // The camera looks along the negative z-axis in view space, so the most negative
  // view-space z is farthest away and must be drawn first.
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry &a, const Entry &b) { return a.depth < b.depth; });

  std::vector<RenderOrderItem> order;
  order.reserve(entries.size());
  for (const Entry &entry : entries)
    order.push_back(entry.item);
  return order;
}

void DirectXRenderer::updateStructureUniforms()
{
  if (!structureCB())
    return;

  // One grading for the whole scene, so it comes off the project rather than out of the
  // per-structure constructor.
  const float ambientOcclusionStrength =
      m_dataSource ? float(std::clamp(m_dataSource->renderAmbientOcclusionStrength(), 0.0, 1.0))
                   : 0.0f;

  std::vector<RKStructureUniforms> structureUniforms;
  for (size_t i = 0; i < m_structures.size(); ++i)
  {
    for (size_t j = 0; j < m_structures[i].size(); ++j)
    {
      structureUniforms.push_back(RKStructureUniforms(i, j, m_structures[i][j]));
      structureUniforms.back().ambientOcclusionStrength = ambientOcclusionStrength;
    }
  }
  if (structureUniforms.empty())
    structureUniforms.push_back(RKStructureUniforms());

  const UINT needed = static_cast<UINT>(structureUniforms.size());
  if (needed > m_structureCBVCapacity)
  {
    if (m_fence)
      m_device.waitForGPU(m_fence);
    m_structureCBVCapacity = needed;
    for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
    {
      m_structureCBV[i] = DirectXDeviceHelpers::createUploadBuffer(
          m_device.device(), static_cast<uint64_t>(m_structureCBVStride) * m_structureCBVCapacity);
    }
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(structureCB()->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return;

  for (size_t i = 0; i < needed; ++i)
  {
    std::memcpy(mapped + static_cast<size_t>(i) * m_structureCBVStride,
                &structureUniforms[i], sizeof(RKStructureUniforms));
  }
  structureCB()->Unmap(0, nullptr);
}

void DirectXRenderer::updateIsosurfaceUniforms()
{
  if (!isosurfaceCB())
    return;

  std::vector<RKIsosurfaceUniforms> isosurfaceUniforms;
  for (size_t i = 0; i < m_structures.size(); ++i)
  {
    for (size_t j = 0; j < m_structures[i].size(); ++j)
      isosurfaceUniforms.push_back(RKIsosurfaceUniforms(m_structures[i][j]));
  }
  if (isosurfaceUniforms.empty())
    isosurfaceUniforms.push_back(RKIsosurfaceUniforms());

  const UINT needed = static_cast<UINT>(isosurfaceUniforms.size());
  if (needed > m_isosurfaceCBVCapacity)
  {
    if (m_fence)
      m_device.waitForGPU(m_fence);
    m_isosurfaceCBVCapacity = needed;
    for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
    {
      m_isosurfaceCBV[i] = DirectXDeviceHelpers::createUploadBuffer(
          m_device.device(), static_cast<uint64_t>(m_isosurfaceCBVStride) * m_isosurfaceCBVCapacity);
    }
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(isosurfaceCB()->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return;

  for (size_t i = 0; i < needed; ++i)
  {
    std::memcpy(mapped + static_cast<size_t>(i) * m_isosurfaceCBVStride,
                &isosurfaceUniforms[i], sizeof(RKIsosurfaceUniforms));
  }
  isosurfaceCB()->Unmap(0, nullptr);
}

void DirectXRenderer::updateBlockingPocketUniforms()
{
  if (!blockingPocketCB())
    return;

  std::vector<RKBlockingPocketUniforms> blockingPocketUniforms;
  for (size_t i = 0; i < m_structures.size(); ++i)
  {
    for (size_t j = 0; j < m_structures[i].size(); ++j)
      blockingPocketUniforms.push_back(RKBlockingPocketUniforms(m_structures[i][j]));
  }
  if (blockingPocketUniforms.empty())
    blockingPocketUniforms.push_back(RKBlockingPocketUniforms());

  const UINT needed = static_cast<UINT>(blockingPocketUniforms.size());
  if (needed > m_blockingPocketCBVCapacity)
  {
    if (m_fence)
      m_device.waitForGPU(m_fence);
    m_blockingPocketCBVCapacity = needed;
    for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
    {
      m_blockingPocketCBV[i] = DirectXDeviceHelpers::createUploadBuffer(
          m_device.device(), static_cast<uint64_t>(m_blockingPocketCBVStride) * m_blockingPocketCBVCapacity);
    }
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  if (FAILED(blockingPocketCB()->Map(0, &readRange, reinterpret_cast<void **>(&mapped))))
    return;

  for (size_t i = 0; i < needed; ++i)
  {
    std::memcpy(mapped + static_cast<size_t>(i) * m_blockingPocketCBVStride,
                &blockingPocketUniforms[i], sizeof(RKBlockingPocketUniforms));
  }
  blockingPocketCB()->Unmap(0, nullptr);
}

void DirectXRenderer::updateLightUniforms()
{
  if (!lightsCB())
    return;

  RKLightsUniforms lightUniforms = RKLightsUniforms(m_dataSource);
  DirectXDeviceHelpers::writeUploadBuffer(lightsCB(), &lightUniforms, sizeof(lightUniforms));
}

void DirectXRenderer::updateGlobalAxesUniforms()
{
  if (!globalAxesCB())
    return;
  RKGlobalAxesUniforms uniforms(m_dataSource);
  DirectXDeviceHelpers::writeUploadBuffer(globalAxesCB(), &uniforms, sizeof(uniforms));
}

void DirectXRenderer::updateConstantBuffers()
{
  updateTransformUniforms();
  updateStructureUniforms();
  updateLightUniforms();
  updateIsosurfaceUniforms();
  updateBlockingPocketUniforms();
  updateGlobalAxesUniforms();
}

void DirectXRenderer::beginGpuFrame()
{
  if (m_gpuFrameStarted)
    m_device.advanceFrame();
  m_gpuFrameStarted = true;
  if (m_fence)
    m_device.waitForFenceValue(m_fence, m_frameFenceValues[inflightSlot()]);
}

void DirectXRenderer::endGpuFrame()
{
  if (m_fence)
    m_frameFenceValues[inflightSlot()] = m_device.signalFence(m_fence);
}

void DirectXRenderer::renderFrame()
{
  if (!m_ready || !m_commandList)
    return;

  // Wait only for the inflight slot we are about to reuse (Cocoa 2/3-frame overlap).
  beginGpuFrame();

  if (!m_sceneReady || !m_rootSignature)
  {
    m_device.commandAllocator()->Reset();
    m_commandList->Reset(m_device.commandAllocator(), nullptr);

    ID3D12Resource *backBuffer = m_device.backBufferRenderTarget();
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();
    m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
    m_commandList->ClearRenderTargetView(sceneRtv, m_clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    m_device.transitionResource(backBuffer, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_device.resolveSceneColorToBackBuffer(m_commandList.Get());
    m_device.transitionResource(backBuffer, m_commandList.Get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->Close();
    ID3D12CommandList *lists[] = {m_commandList.Get()};
    m_device.commandQueue()->ExecuteCommandLists(1, lists);
    if (!m_device.present(1))
    {
      m_status = L"Graphics device lost";
      m_ready = false;
    }
    endGpuFrame();
    return;
  }

  // A frame the camera is being moved through is drawn more cheaply than one at rest, as Cocoa's
  // view does it: fewer traced paths per pixel, and imposters shaded per pixel rather than per
  // sample. Settled here so every pass of this frame agrees on it.
  m_quality = interactiveQuality();

  updateConstantBuffers();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
  bindShadowMask();

  // Pick is on demand in pickTexture() (click / rubber-band). A full-viewport pick
  // pass every frame redraws every atom and bond into an R32G32B32A32 target and
  // is what TDRs the GPU on large Gallery structures.

  ID3D12Resource *backBuffer = m_device.backBufferRenderTarget();
  const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE backRtv = m_device.backBufferRenderTargetCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();
  const int width = static_cast<int>(m_device.width());
  const int height = static_cast<int>(m_device.height());

  // Settled before anything is recorded, a traced frame leaving the molecular geometry out of the
  // raster pass: by the time the pass has run it is too late to change one's mind about that.
  const bool tracing = wantsInteractiveTracing() && prepareInteractiveTracing();

  // Before the raster passes, which read what it writes. Nothing is left for the mask to shade when
  // the tracer is drawing the molecular geometry: it casts its own shadow rays, one per light per
  // hit, which is the thing the mask stands in for.
  if (!tracing)
    recordShadowMask(width, height);
  else
  {
    m_shadowMaskWidth = 0;
    m_shadowMaskHeight = 0;
  }

  recordScenePass(sceneRtv, dsv, width, height, /*suppressMolecularGeometry=*/tracing);

  const bool traced = tracing && recordTracedFrame(width, height);
  if (tracing && !traced && !m_tracedFrameStatusLogged)
  {
    m_tracedFrameStatusLogged = true;
    DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerShader.status());
  }

  m_device.transitionResource(backBuffer, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

  if (traced)
  {
    presentTracedFrame(backRtv, width, height);
  }
  else if (!recordEdgeCues(backRtv, width, height))
  {
    // Resolve MSAA scene color → flip-model backbuffer (1×), matching QT/Cocoa.
    m_device.resolveSceneColorToBackBuffer(m_commandList.Get());
  }

  // Over the traced image as readily as over the rasterized one: the glow is drawn from the scene
  // depth, which the raster pass left behind either way.
  recordSelectionGlow(backRtv, width, height);

  m_device.transitionResource(backBuffer, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (!m_device.present(1))
  {
    m_status = L"Graphics device lost";
    m_ready = false;
  }
  endGpuFrame();

  // A wheel has no release to match its turn, so the frame that redraws a settled zoom at full
  // quality has to be asked for; without it the view would keep the cheaper image it was given
  // mid-zoom. Self-limiting: once the wheel has been quiet long enough the quality comes back up and
  // no further frame is requested.
  if (m_quality != RKRenderQuality::high && m_tracking == Tracking::none)
    markNeedsDisplay();
}

/// Everything that draws the scene itself, into \a sceneRtv (MSAA scene color) and \a dsv.
/// Shared by the live frame and by offscreen export, which passes targets of a different
/// size; nothing here may assume the swap chain's dimensions.
/// Rasterizes the scene into \a sceneRtv. With \a suppressMolecularGeometry the atoms, the bonds,
/// the ribbons and the selection overlays that mark them are left out, the path tracer drawing that
/// geometry instead; everything the tracer does not handle — the background, the isosurfaces, the
/// unit cell, the primitives, the text and the axes — is still rasterized, and the depth it leaves
/// behind is what the traced image is composited against.
void DirectXRenderer::recordScenePass(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv,
                                      D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                                      int width, int height,
                                      bool suppressMolecularGeometry)
{
  // "Fast" imposter mode while interacting (rotating, panning, zooming): the render quality drops
  // to medium/low during interaction and the imposters are then shaded per-pixel; per-sample
  // anti-aliased shading is used for high-quality still frames and pictures. Set once here so
  // every imposter pass of this frame (scene, selection glow) agrees.
  DirectXDeviceHelpers::setPerSampleImposterShading(m_quality == RKRenderQuality::high ||
                                                    m_quality == RKRenderQuality::picture);

  ID3D12Device *dev = m_device.device();
  m_backgroundShader.ensureTextureUploaded(dev, m_commandList.Get());
  m_textShader.ensureTexturesUploaded(dev, m_commandList.Get());
  m_globalAxesShader.ensureTexturesUploaded(dev, m_commandList.Get());

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  ID3D12DescriptorHeap *heaps[] = {m_srvHeap.Get()};
  m_commandList->SetDescriptorHeaps(1, heaps);

  m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
  bindShadowMask();
  if (isosurfaceCB())
    m_commandList->SetGraphicsRootConstantBufferView(4, isosurfaceCB()->GetGPUVirtualAddress());
  if (globalAxesCB())
    m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());

  m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
  m_commandList->ClearRenderTargetView(sceneRtv, m_clearColor, 0, nullptr);
  m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MaxDepth = 1.0f;
  m_commandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissor = {};
  scissor.right = width;
  scissor.bottom = height;
  m_commandList->RSSetScissorRects(1, &scissor);

  m_backgroundShader.paint(m_commandList.Get(), m_srvHeap->GetGPUDescriptorHandleForHeapStart());

  if (!m_structures.empty())
  {
    ID3D12DescriptorHeap *windowHeaps[] = {m_srvHeap.Get()};

    if (!suppressMolecularGeometry)
    {
      if (ID3D12DescriptorHeap *aoHeap = m_atomShader.aoSrvHeap())
      {
        ID3D12DescriptorHeap *aoHeaps[] = {aoHeap};
        m_commandList->SetDescriptorHeaps(1, aoHeaps);
      }
      m_atomShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                         m_camera);

      m_commandList->SetDescriptorHeaps(1, windowHeaps);

      m_bondShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    }

    m_commandList->SetDescriptorHeaps(1, windowHeaps);

    m_objectShader.paintOpaque(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_unitCellShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_localAxesShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);

    if (!suppressMolecularGeometry)
    {
      // The ribbon reads its occlusion out of the bake's own heap, so swap heaps around it and put
      // the window heap back for everything that follows.
      if (ID3D12DescriptorHeap *ribbonAoHeap = m_ribbonAmbientOcclusionShader.srvHeap())
      {
        ID3D12DescriptorHeap *ribbonAoHeaps[] = {ribbonAoHeap};
        m_commandList->SetDescriptorHeaps(1, ribbonAoHeaps);
      }
      m_ribbonShader.paintOpaque(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(),
                                 m_structureCBVStride, &m_ribbonAmbientOcclusionShader);
      m_commandList->SetDescriptorHeaps(1, windowHeaps);
    }

    m_boundingBoxShader.paint(m_commandList.Get());

    m_energySurfaceShader.paintOpaque(m_commandList.Get(),
                                      structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                      isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride);

    auto copySceneDepthForVolume = [&]() {
      if (!m_energyVolumeShader.needsSceneDepth())
        return;

      // Cannot sample the live MSAA DSV while bound — resolve to 1× then bind as SRV.
      m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
      if (ID3D12Resource *resolved = m_device.resolveSceneDepth(m_commandList.Get()))
        m_energyVolumeShader.bindSceneDepthSRV(dev, resolved);

      m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsv);
      m_commandList->RSSetViewports(1, &viewport);
      m_commandList->RSSetScissorRects(1, &scissor);
    };

    // The volume shader swaps in its own root signature and descriptor heap, so the main
    // ones have to be restored before any other shader draws again.
    auto bindSceneRootSignature = [&]() {
      m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
      ID3D12DescriptorHeap *sceneHeaps[] = {m_srvHeap.Get()};
      m_commandList->SetDescriptorHeaps(1, sceneHeaps);
      m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
      m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
      m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
      bindShadowMask();
      if (isosurfaceCB())
        m_commandList->SetGraphicsRootConstantBufferView(4, isosurfaceCB()->GetGPUVirtualAddress());
      if (globalAxesCB())
        m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());
    };

    const std::vector<RenderOrderItem> renderOrder = backToFrontRenderOrder();

    // QT resolves depth before opaque volume and again before transparent volume so
    // CoolWarm rays stop at RASPA_PES walls written into the DSV.
    copySceneDepthForVolume();

    for (const RenderOrderItem &item : renderOrder)
    {
      m_energyVolumeShader.paintOpaque(m_commandList.Get(),
                                       frameCB()->GetGPUVirtualAddress(),
                                       structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                       isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride,
                                       lightsCB()->GetGPUVirtualAddress(),
                                       item.sceneIndex, item.movieIndex, item.structureIndex);
    }

    copySceneDepthForVolume();

    bindSceneRootSignature();

    // Composite all transparent objects back-to-front per structure, interleaving the shader
    // types, so overlapping transparent objects from different movies blend correctly.
    for (const RenderOrderItem &item : renderOrder)
    {
      if (m_energyVolumeShader.paintTransparent(m_commandList.Get(),
                                                frameCB()->GetGPUVirtualAddress(),
                                                structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                                isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride,
                                                lightsCB()->GetGPUVirtualAddress(),
                                                item.sceneIndex, item.movieIndex, item.structureIndex))
      {
        bindSceneRootSignature();
      }

      m_objectShader.paintTransparent(m_commandList.Get(),
                                      structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                      item.sceneIndex, item.movieIndex, item.structureIndex);
      m_energySurfaceShader.paintTransparent(m_commandList.Get(),
                                             structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                             isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride,
                                             item.sceneIndex, item.movieIndex, item.structureIndex);
      m_blockingPocketsShader.paintTransparent(m_commandList.Get(),
                                               structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                               blockingPocketCB()->GetGPUVirtualAddress(),
                                               m_blockingPocketCBVStride,
                                               item.sceneIndex, item.movieIndex, item.structureIndex);
    }

    // The overlays mark the very surfaces the tracer is drawing, and they are traced with it, so
    // they follow the geometry they belong to out of the raster pass.
    if (!suppressMolecularGeometry)
    {
      m_ribbonSelectionShader.paintOverlay(m_commandList.Get(),
                                           structureCB()->GetGPUVirtualAddress(),
                                           m_structureCBVStride);

      m_selectionShader.paintOverlays(m_commandList.Get(),
                                      structureCB()->GetGPUVirtualAddress(),
                                      m_structureCBVStride,
                                      m_camera && m_camera->isOrthographic());
    }

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
    bindShadowMask();
    if (globalAxesCB())
      m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());
    m_textShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
  }
  else
  {
    m_boundingBoxShader.paint(m_commandList.Get());
  }

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
  bindShadowMask();
  if (globalAxesCB())
    m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());
  m_globalAxesShader.paint(m_commandList.Get(),
                           frameCB()->GetGPUVirtualAddress(),
                           lightsCB()->GetGPUVirtualAddress(),
                           globalAxesCB() ? globalAxesCB()->GetGPUVirtualAddress() : 0,
                           width, height);
}

/// Selection glow → blur → composite onto \a destRtv, which already holds the resolved
/// 1× scene. The shells are drawn against the scene's own multisampled depth, so nothing
/// here needs the scene colour target.
void DirectXRenderer::recordSelectionGlow(D3D12_CPU_DESCRIPTOR_HANDLE destRtv,
                                          int width, int height)
{
  if (!m_selectionShader.hasGlowWork() && !m_ribbonSelectionShader.hasGlowWork())
    return;
  D3D12_VIEWPORT viewport = {};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MaxDepth = 1.0f;

  D3D12_RECT scissor = {};
  scissor.right = width;
  scissor.bottom = height;

  m_commandList->RSSetViewports(1, &viewport);
  m_commandList->RSSetScissorRects(1, &scissor);
  m_commandList->OMSetRenderTargets(1, &destRtv, FALSE, nullptr);

  if (m_glowTexture && m_glowRtvHeap)
  {
    // The shells clear their own atom by a thousandth of a radius and test against it, so they are
    // drawn multisampled against the scene's own depth: resolving that depth first collapses each
    // pixel's samples to one value, which beats so slight a clearance wherever the sphere is steep
    // and leaves the glow showing only over the flat centre of an atom.
    const bool multisampled = m_glowMsaaTexture && m_glowSampleCount > 1;
    ID3D12Resource *glowTarget = multisampled ? m_glowMsaaTexture.Get() : m_glowTexture.Get();
    D3D12_RESOURCE_STATES &glowTargetState = multisampled ? m_glowMsaaState : m_glowState;

    if (glowTargetState != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
      m_device.transitionResource(glowTarget, m_commandList.Get(),
                                 glowTargetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
      glowTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE glowRtv = m_glowRtvHeap->GetCPUDescriptorHandleForHeapStart();
    if (multisampled)
      glowRtv.ptr += m_device.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Written read-only, the scene depth still being needed as it stands; the shells only mark a
    // selection and never occlude one another.
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv = m_device.depthStencilCPUHandle();
    m_commandList->OMSetRenderTargets(1, &glowRtv, FALSE, &sceneDsv);
    const float glowClear[] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_commandList->ClearRenderTargetView(glowRtv, glowClear, 0, nullptr);

    m_selectionShader.paintGlow(m_commandList.Get(),
                                structureCB()->GetGPUVirtualAddress(),
                                m_structureCBVStride,
                                m_camera && m_camera->isOrthographic());

    m_ribbonSelectionShader.paintGlow(m_commandList.Get(),
                                      structureCB()->GetGPUVirtualAddress(),
                                      m_structureCBVStride);

    // The blur reads a single-sampled image, and resolving is also what turns the fraction of
    // samples a shell won into the soft coverage the halo is made of.
    if (multisampled)
    {
      m_device.transitionResource(glowTarget, m_commandList.Get(),
                                 glowTargetState, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      glowTargetState = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

      if (m_glowState != D3D12_RESOURCE_STATE_RESOLVE_DEST)
      {
        m_device.transitionResource(m_glowTexture.Get(), m_commandList.Get(),
                                   m_glowState, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        m_glowState = D3D12_RESOURCE_STATE_RESOLVE_DEST;
      }

      m_commandList->ResolveSubresource(m_glowTexture.Get(), 0, glowTarget, 0, kGlowFormat);
    }

    m_blurShader.paint(m_commandList.Get(), m_glowTexture.Get(), m_glowState);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
    bindShadowMask();
    if (isosurfaceCB())
      m_commandList->SetGraphicsRootConstantBufferView(4, isosurfaceCB()->GetGPUVirtualAddress());

    if (ID3D12DescriptorHeap *blurHeap = m_blurShader.srvHeap())
    {
      ID3D12DescriptorHeap *blurHeaps[] = {blurHeap};
      m_commandList->SetDescriptorHeaps(1, blurHeaps);
    }

    m_commandList->OMSetRenderTargets(1, &destRtv, FALSE, nullptr);
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_compositeShader.paint(m_commandList.Get(), m_blurShader.blurredSrv());
  }
}

RKImage DirectXRenderer::renderSceneToImage(int width, int height, RKRenderQuality quality)
{
  if (!m_ready || !m_sceneReady || !m_commandList || !m_rootSignature || !m_device.device())
    return RKImage();

  const int w = (std::min)((std::max)(1, width), static_cast<int>(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION));
  const int h = (std::min)((std::max)(1, height), static_cast<int>(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION));

  if (m_fence)
    m_device.waitForGPU(m_fence);

  // The export resolution is not the window's, so everything scaled to the window has to
  // follow it: the camera's aspect ratio, the MSAA scene targets, and the glow chain.
  // The picking texture deliberately does not, since export draws no pick pass and the
  // live view keeps hit-testing against the window.
  const int windowWidth = (std::max)(1, static_cast<int>(m_device.width()));
  const int windowHeight = (std::max)(1, static_cast<int>(m_device.height()));
  const RKRenderQuality savedQuality = m_quality;

  if (!m_device.beginOffscreenCapture(static_cast<UINT>(w), static_cast<UINT>(h)))
    return RKImage();

  m_quality = quality;
  if (m_camera)
    m_camera->updateCameraForWindowResize(w, h);
  resizeGlowAndBlur(w, h);

  // What the document asks its exports to look like, which the environment can override for tracing
  // a picture out of a project that does not ask for one. A device that cannot trace rasterizes
  // instead, so that a project authored on one that can still produces an image.
  uint32_t sampleCount = 0;
  uint32_t maximumBounces = 2;
  bool tracing = tracePicturesFromEnvironment(sampleCount);
  if (!tracing && m_dataSource && m_dataSource->renderPictureRayTracing())
  {
    tracing = true;
    sampleCount = static_cast<uint32_t>(m_dataSource->picturePathTracerSampleCount());
    maximumBounces = static_cast<uint32_t>(m_dataSource->picturePathTracerMaximumBounces());
  }
  if (tracing && !m_supportsRaytracing)
  {
    tracing = false;
    std::fprintf(stderr, "DirectXRenderer: ray tracing was asked for, but %s; rasterized instead\n",
                 m_raytracingStatus.c_str());
  }

  RKImage image = tracing ? captureTracedOffscreenFrame(w, h, sampleCount, maximumBounces)
                          : captureOffscreenFrame(w, h);

  m_device.endOffscreenCapture();
  m_quality = savedQuality;
  if (m_camera)
    m_camera->updateCameraForWindowResize(windowWidth, windowHeight);
  resizeGlowAndBlur(windowWidth, windowHeight);
  updateConstantBuffers();
  markNeedsDisplay();

  return image;
}

RKImage DirectXRenderer::captureOffscreenFrame(int width, int height)
{
  ID3D12Device *dev = m_device.device();
  const DXGI_FORMAT format = m_device.backBufferFormat();

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  ComPtr<ID3D12DescriptorHeap> rtvHeap;
  if (FAILED(dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))
    return RKImage();

  // Stands in for the backbuffer: the 1× surface the MSAA scene resolves into and the
  // glow composites onto.
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(width);
  desc.Height = static_cast<UINT>(height);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear = {};
  clear.Format = format;
  clear.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  ComPtr<ID3D12Resource> target;
  if (FAILED(dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                          IID_PPV_ARGS(&target))))
  {
    std::fprintf(stderr, "DirectXRenderer: cannot allocate a %dx%d export target", width, height);
    return RKImage();
  }
  dev->CreateRenderTargetView(target.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

  updateConstantBuffers();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE targetRtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();

  // Before the raster pass, which reads what it writes. An export traces the mask whatever the
  // machine-wide setting says, a picture being what the document describes rather than what this
  // machine finds cheap; the adapter was chosen with that in mind.
  recordShadowMask(width, height);

  recordScenePass(sceneRtv, dsv, width, height);
  if (!recordEdgeCues(targetRtv, width, height))
    m_device.resolveSceneColorTo(m_commandList.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
  recordSelectionGlow(targetRtv, width, height);

  m_device.transitionResource(target.Get(), m_commandList.Get(),
                             D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  return readbackTexture(target.Get(), format, width, height);
}

std::string DirectXRenderer::describeCentreRay() const
{
  // The ray through the middle of the image, worked out the way the kernels work it out: from the
  // inverse of the very matrices they are handed. Printed beside the traced geometry's own bounds,
  // the pair says whether a trace that found nothing was aimed anywhere near the structure.
  if (!m_camera)
    return "there is no camera to trace from";

  const double4x4 inverseProjection = double4x4::inverse(m_camera->projectionMatrix());
  const double4x4 inverseModelView = double4x4::inverse(m_camera->modelViewMatrix());

  // The near plane of an OpenGL clip space, which is where a camera ray starts.
  double4 nearPoint = inverseProjection * double4(0.0, 0.0, -1.0, 1.0);
  double4 farPoint = inverseProjection * double4(0.0, 0.0, 1.0, 1.0);
  if (nearPoint.w == 0.0 || farPoint.w == 0.0)
    return "the projection matrix cannot be unprojected";
  nearPoint = double4(nearPoint.x / nearPoint.w, nearPoint.y / nearPoint.w,
                      nearPoint.z / nearPoint.w, 1.0);
  farPoint = double4(farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w, 1.0);

  const double4 origin = inverseModelView * nearPoint;
  const double4 target = inverseModelView * farPoint;
  const double3 direction = double3::normalize(double3(target.x - origin.x, target.y - origin.y,
                                                       target.z - origin.z));

  char description[256] = {};
  std::snprintf(description, sizeof(description),
                "the centre ray starts at (%.2f,%.2f,%.2f) heading (%.3f,%.3f,%.3f), reaching "
                "(%.2f,%.2f,%.2f)",
                origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, target.x,
                target.y, target.z);
  return description;
}

RKImage DirectXRenderer::failedCapture(const std::string &reason)
{
  // An export runs in a process with no window to put a message in, so the reason travels back to
  // the application as the status: a null image on its own says only that there is no picture.
  setStatusMessage(RKString(reason).toStdWString());
  return RKImage();
}

RKImage DirectXRenderer::captureTracedOffscreenFrame(int width, int height, uint32_t sampleCount,
                                                     uint32_t maximumBounces)
{
  ID3D12Device *dev = m_device.device();
  const DXGI_FORMAT format = m_device.backBufferFormat();

  // Compiled on the first traced image rather than at start-up: two ray-tracing kernels cost real
  // time to compile, and a session that never exports a traced image should not pay for them.
  if (!m_pathTracerShader.isReady())
  {
    m_pathTracerShader.initialize(m_device);
    DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerShader.status());
    if (!m_pathTracerShader.isReady())
      return failedCapture(m_pathTracerShader.status());
  }

  const bool built = m_pathTracerGeometry.build(m_device);
  // Reported either way: what an image was traced from is worth as much as why it could not be, and
  // an export traces one image, so this is one line.
  DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerGeometry.status());
  if (!built)
    return failedCapture(m_pathTracerGeometry.status());

  DirectXDxcCompiler::logDiagnostics("Path tracer", describeCentreRay());

  if (const std::string complaints = m_device.takeDebugMessages(); !complaints.empty())
    DirectXDxcCompiler::logDiagnostics("Path tracer, building", complaints);

  // The scene the trace is composited over: everything the tracer does not draw, resolved to a
  // single sample. Read by the resolve kernel rather than drawn on, so it is not a render target
  // beyond the resolve that fills it.
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(width);
  desc.Height = static_cast<UINT>(height);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear = {};
  clear.Format = format;
  clear.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  // Created in the state the resolve kernel reads it in, which is also the state
  // resolveSceneColorTo() expects to find it in and returns it to.
  ComPtr<ID3D12Resource> scene;
  if (FAILED(dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear,
                                          IID_PPV_ARGS(&scene))))
  {
    return failedCapture("a " + std::to_string(width) + "x" + std::to_string(height)
                         + " export target could not be allocated");
  }

  // No shadow mask: nothing is left in the raster pass for it to shade, the guide geometry that
  // does remain being drawn unshadowed. The traced image works out its own shadows, one ray per
  // light per hit, which is what the mask stands in for. Zeroed before the frame uniforms are
  // written, those being what carries the size to the raster passes.
  m_shadowMaskWidth = 0;
  m_shadowMaskHeight = 0;

  updateConstantBuffers();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();

  recordScenePass(sceneRtv, dsv, width, height, /*suppressMolecularGeometry=*/true);

  // The depth of what the raster pass did draw, which is what decides where the traced image is
  // allowed to show. Left in a state a compute shader can read rather than the pixel-shader one the
  // volume pass wants.
  ID3D12Resource *sceneDepth =
      m_device.resolveSceneDepth(m_commandList.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_device.resolveSceneColorTo(m_commandList.Get(), scene.Get(),
                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  if (!sceneDepth)
    return failedCapture("the rasterized depth the trace composites against was not produced");

  DirectXPathTracerShader::Settings settings;
  settings.sampleCount = sampleCount;
  settings.maximumBounces = maximumBounces;
  // A look rather than a cost, so the traced image is graded the same way the rasterized one is.
  settings.ambientOcclusionStrength =
      m_dataSource ? float(std::clamp(m_dataSource->renderAmbientOcclusionStrength(), 0.0, 1.0))
                   : 0.0f;

  const bool traced = m_pathTracerShader.render(
      m_device, m_pathTracerGeometry, frameCB()->GetGPUVirtualAddress(),
      lightsCB()->GetGPUVirtualAddress(), scene.Get(), sceneDepth, static_cast<UINT>(width),
      static_cast<UINT>(height), settings);
  DirectXDxcCompiler::logDiagnostics("Path tracer", m_pathTracerShader.status());
  if (const std::string complaints = m_device.takeDebugMessages(); !complaints.empty())
    DirectXDxcCompiler::logDiagnostics("Path tracer, tracing", complaints);
  if (!traced)
    return failedCapture(m_pathTracerShader.status());

  // The composite is written RGBA whichever way round the swap chain stores its channels, so the
  // readback is told so rather than being told the swap chain's format.
  if (RKImage cued = captureTracedFrameWithCues(width, height); !cued.isNull())
    return cued;

  return readbackTexture(m_pathTracerShader.compositeTexture(), DXGI_FORMAT_R8G8B8A8_UNORM, width,
                         height);
}

/// Draws the cues over the traced composite and returns that, or a null image when there are no cues
/// to draw or nothing to draw them into, in which case the caller reads the composite back as it is.
RKImage DirectXRenderer::captureTracedFrameWithCues(int width, int height)
{
  ID3D12Device *dev = m_device.device();
  ID3D12Resource *composite = m_pathTracerShader.compositeTexture();
  ID3D12Resource *tracedDepth = m_pathTracerShader.compositeDepthBuffer();
  ID3D12Resource *tracedCueMask = m_pathTracerShader.compositeCueMaskBuffer();
  if (!dev || !composite || !tracedDepth || !tracedCueMask)
    return RKImage();
  if (!drawsEdgeCues() || !m_edgeCueingShader.canPaintTraced())
    return RKImage();

  // The composite cannot be both read and written, so the cues are drawn into a second image of the
  // same format, which is what is read back.
  const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  ComPtr<ID3D12DescriptorHeap> rtvHeap;
  if (FAILED(dev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))
    return RKImage();

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(width);
  desc.Height = static_cast<UINT>(height);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear = {};
  clear.Format = format;
  clear.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES defaultHeap = {};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  ComPtr<ID3D12Resource> target;
  if (FAILED(dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                          IID_PPV_ARGS(&target))))
    return RKImage();
  dev->CreateRenderTargetView(target.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  // render() leaves the composite ready to be copied away rather than to be sampled.
  m_device.transitionResource(composite, m_commandList.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_device.transitionResource(tracedDepth, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_device.transitionResource(tracedCueMask, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  m_edgeCueingShader.paintTraced(m_commandList.Get(),
                                 rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                 frameCB()->GetGPUVirtualAddress(), composite, tracedDepth,
                                 tracedCueMask, width, height);

  m_device.transitionResource(tracedCueMask, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_device.transitionResource(tracedDepth, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_device.transitionResource(composite, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_device.transitionResource(target.Get(), m_commandList.Get(),
                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);

  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  // The fallback in the caller is silent by design, so anything the device objected to is reported
  // here: a cue pass that drew nothing looks exactly like a picture that asked for no cues.
  if (const std::string complaints = m_device.takeDebugMessages(); !complaints.empty())
    DirectXDxcCompiler::logDiagnostics("Edge cueing, traced", complaints);

  return readbackTexture(target.Get(), format, width, height);
}

RKImage DirectXRenderer::readbackTexture(ID3D12Resource *texture, DXGI_FORMAT format, int width,
                                         int height)
{
  ID3D12Device *dev = m_device.device();
  if (!texture || !dev)
    return RKImage();

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = static_cast<UINT64>(width);
  desc.Height = static_cast<UINT>(height);
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT64 readbackSize = 0;
  dev->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = readbackSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(dev->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&readback))))
    return RKImage();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = texture;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;

  m_commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  m_commandList->Close();
  ID3D12CommandList *lists[] = {m_commandList.Get()};
  m_device.commandQueue()->ExecuteCommandLists(1, lists);
  if (m_fence)
    m_device.waitForGPU(m_fence);

  uint8_t *mapped = nullptr;
  const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(readbackSize)};
  if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void **>(&mapped))) || !mapped)
    return RKImage();

  RKImage image(width, height, RKImage::Format_RGBA8888);
  uint8_t *out = image.bits();
  // Rows in the readback buffer are padded to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, and the
  // composition swap chain's format is BGRA where RKImage wants RGBA. Alpha is forced
  // opaque: the scene is composited against the clear colour, and a movie frame has
  // nowhere to put transparency anyway.
  const bool swapRedAndBlue = (format == DXGI_FORMAT_B8G8R8A8_UNORM);
  for (int y = 0; y < height; ++y)
  {
    const uint8_t *sourceRow = mapped + static_cast<size_t>(footprint.Footprint.RowPitch) * static_cast<size_t>(y);
    uint8_t *destinationRow = out + static_cast<size_t>(width) * 4u * static_cast<size_t>(y);
    for (int x = 0; x < width; ++x)
    {
      const uint8_t *pixel = sourceRow + static_cast<size_t>(x) * 4u;
      uint8_t *outPixel = destinationRow + static_cast<size_t>(x) * 4u;
      outPixel[0] = swapRedAndBlue ? pixel[2] : pixel[0];
      outPixel[1] = pixel[1];
      outPixel[2] = swapRedAndBlue ? pixel[0] : pixel[2];
      outPixel[3] = 255;
    }
  }

  const D3D12_RANGE noWrite = {0, 0};
  readback->Unmap(0, &noWrite);
  return image;
}
