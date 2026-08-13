/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#include "directxrenderer.h"
#include "directxdevicehelpers.h"
#include "rkrenderuniforms.h"
#include "atomviewer.h"
#include "bondviewer.h"
#include "proteinribbonmixin.h"
#include "proteinribbonsegmentsupport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

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

  RKTransformationUniforms frame{};
  RKStructureUniforms structure{};
  RKLightsUniforms lights{};
  RKIsosurfaceUniforms isosurface{};
  RKGlobalAxesUniforms globalAxes(nullptr);

  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
  {
    m_frameCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(sizeof(RKTransformationUniforms)));
    m_lightsCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(4 * sizeof(RKLightUniform)));
    m_structureCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, m_structureCBVStride * m_structureCBVCapacity);
    m_isosurfaceCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, m_isosurfaceCBVStride * m_isosurfaceCBVCapacity);
    m_globalAxesCBV[i] = DirectXDeviceHelpers::createUploadBuffer(dev, DirectXDeviceHelpers::alignedCBSize(sizeof(RKGlobalAxesUniforms)));

    DirectXDeviceHelpers::writeUploadBuffer(m_frameCBV[i].Get(), &frame, sizeof(frame));
    DirectXDeviceHelpers::writeUploadBuffer(m_structureCBV[i].Get(), &structure, sizeof(structure));
    DirectXDeviceHelpers::writeUploadBuffer(m_lightsCBV[i].Get(), lights.lights.data(), lights.lights.size() * sizeof(RKLightUniform));
    DirectXDeviceHelpers::writeUploadBuffer(m_isosurfaceCBV[i].Get(), &isosurface, sizeof(isosurface));
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
}

void DirectXRenderer::createGlowTarget(ID3D12Device *device, int width, int height)
{
  if (!device)
    return;

  m_glowWidth = (std::max)(1, width);
  m_glowHeight = (std::max)(1, height);
  m_glowTexture.Reset();

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = 1;
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

  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  rtvDesc.Format = kGlowFormat;
  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  device->CreateRenderTargetView(m_glowTexture.Get(), &rtvDesc,
                                 m_glowRtvHeap->GetCPUDescriptorHandleForHeapStart());
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
  m_rootSignature.Reset();
  m_srvHeap.Reset();
  for (UINT i = 0; i < Dx12DeviceContext::kInflightFrameCount; ++i)
  {
    m_frameCBV[i].Reset();
    m_structureCBV[i].Reset();
    m_lightsCBV[i].Reset();
    m_isosurfaceCBV[i].Reset();
    m_globalAxesCBV[i].Reset();
    m_frameFenceValues[i] = 0;
  }
  m_gpuFrameStarted = false;
  m_structureCBVStride = 0;
  m_structureCBVCapacity = 0;
  m_isosurfaceCBVStride = 0;
  m_isosurfaceCBVCapacity = 0;

  m_glowTexture.Reset();
  m_glowRtvHeap.Reset();
  m_glowState = D3D12_RESOURCE_STATE_COMMON;
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
  if (!initializeScene())
  {
    m_ready = false;
    return false;
  }
  return true;
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

bool DirectXRenderer::initializeOffscreen(UINT width, UINT height, const LUID *avoidAdapter)
{
  release();
  if (!m_device.initializeOffscreen(width, height, avoidAdapter))
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
  resetSceneResources();
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
      m_camera = camera;
      m_camera->updateCameraForWindowResize(m_device.width(), m_device.height());
      m_camera->resetForNewBoundingBox(m_dataSource->renderBoundingBox());
      // Frame the structure in the current viewport (centers + distance).
      m_camera->resetCameraToDirection();
      m_camera->updateCameraForWindowResize(m_device.width(), m_device.height());
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
  markNeedsDisplay();
  notifyCameraChanged();
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

  DirectXDeviceHelpers::writeUploadBuffer(frameCB(), &transformationUniforms, sizeof(transformationUniforms));
}

void DirectXRenderer::updateStructureUniforms()
{
  if (!structureCB())
    return;

  std::vector<RKStructureUniforms> structureUniforms;
  for (size_t i = 0; i < m_structures.size(); ++i)
  {
    for (size_t j = 0; j < m_structures[i].size(); ++j)
      structureUniforms.push_back(RKStructureUniforms(i, j, m_structures[i][j]));
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

void DirectXRenderer::updateLightUniforms()
{
  if (!lightsCB())
    return;

  RKLightsUniforms lightUniforms = RKLightsUniforms(m_dataSource);
  DirectXDeviceHelpers::writeUploadBuffer(lightsCB(), lightUniforms.lights.data(),
                                          lightUniforms.lights.size() * sizeof(RKLightUniform));
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

  updateConstantBuffers();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());

  // Pick is on demand in pickTexture() (click / rubber-band). A full-viewport pick
  // pass every frame redraws every atom and bond into an R32G32B32A32 target and
  // is what TDRs the GPU on large Gallery structures.

  ID3D12Resource *backBuffer = m_device.backBufferRenderTarget();
  const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE backRtv = m_device.backBufferRenderTargetCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();
  const int width = static_cast<int>(m_device.width());
  const int height = static_cast<int>(m_device.height());

  recordScenePass(sceneRtv, dsv, width, height);

  // Resolve MSAA scene color → flip-model backbuffer (1×), matching QT/Cocoa.
  m_device.transitionResource(backBuffer, m_commandList.Get(),
                             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_device.resolveSceneColorToBackBuffer(m_commandList.Get());

  recordSelectionGlow(sceneRtv, backRtv, width, height);

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
}

/// Everything that draws the scene itself, into \a sceneRtv (MSAA scene color) and \a dsv.
/// Shared by the live frame and by offscreen export, which passes targets of a different
/// size; nothing here may assume the swap chain's dimensions.
void DirectXRenderer::recordScenePass(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv,
                                      D3D12_CPU_DESCRIPTOR_HANDLE dsv,
                                      int width, int height)
{
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
    if (ID3D12DescriptorHeap *aoHeap = m_atomShader.aoSrvHeap())
    {
      ID3D12DescriptorHeap *aoHeaps[] = {aoHeap};
      m_commandList->SetDescriptorHeaps(1, aoHeaps);
    }
    m_atomShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                       m_camera, m_quality);

    ID3D12DescriptorHeap *windowHeaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, windowHeaps);

    m_bondShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_objectShader.paintOpaque(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_unitCellShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_localAxesShader.paint(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    // The ribbon reads its occlusion out of the bake's own heap, so swap heaps around it and put the
    // window heap back for everything that follows.
    if (ID3D12DescriptorHeap *ribbonAoHeap = m_ribbonAmbientOcclusionShader.srvHeap())
    {
      ID3D12DescriptorHeap *ribbonAoHeaps[] = {ribbonAoHeap};
      m_commandList->SetDescriptorHeaps(1, ribbonAoHeaps);
    }
    m_ribbonShader.paintOpaque(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(),
                               m_structureCBVStride, &m_ribbonAmbientOcclusionShader);
    m_commandList->SetDescriptorHeaps(1, windowHeaps);

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

    // QT resolves depth before opaque volume and again before transparent volume so
    // CoolWarm rays stop at RASPA_PES walls written into the DSV.
    copySceneDepthForVolume();

    m_energyVolumeShader.paintOpaque(m_commandList.Get(),
                                     frameCB()->GetGPUVirtualAddress(),
                                     structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                     isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride,
                                     lightsCB()->GetGPUVirtualAddress());

    copySceneDepthForVolume();

    m_energyVolumeShader.paintTransparent(m_commandList.Get(),
                                          frameCB()->GetGPUVirtualAddress(),
                                          structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                          isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride,
                                          lightsCB()->GetGPUVirtualAddress());

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap *afterVolumeHeaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, afterVolumeHeaps);
    m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
    if (isosurfaceCB())
      m_commandList->SetGraphicsRootConstantBufferView(4, isosurfaceCB()->GetGPUVirtualAddress());
    if (globalAxesCB())
      m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());

    m_objectShader.paintTransparent(m_commandList.Get(), structureCB()->GetGPUVirtualAddress(), m_structureCBVStride);
    m_energySurfaceShader.paintTransparent(m_commandList.Get(),
                                           structureCB()->GetGPUVirtualAddress(), m_structureCBVStride,
                                           isosurfaceCB()->GetGPUVirtualAddress(), m_isosurfaceCBVStride);

    m_ribbonSelectionShader.paintOverlay(m_commandList.Get(),
                                         structureCB()->GetGPUVirtualAddress(),
                                         m_structureCBVStride);

    m_selectionShader.paintOverlays(m_commandList.Get(),
                                    structureCB()->GetGPUVirtualAddress(),
                                    m_structureCBVStride);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
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
  if (globalAxesCB())
    m_commandList->SetGraphicsRootConstantBufferView(5, globalAxesCB()->GetGPUVirtualAddress());
  m_globalAxesShader.paint(m_commandList.Get(),
                           frameCB()->GetGPUVirtualAddress(),
                           lightsCB()->GetGPUVirtualAddress(),
                           globalAxesCB() ? globalAxesCB()->GetGPUVirtualAddress() : 0,
                           width, height);
}

/// Selection glow → blur → composite onto \a destRtv, which already holds the resolved
/// 1× scene. \a sceneRtv is only bound as a scratch target while depth is resolved,
/// which cannot happen with the destination bound.
void DirectXRenderer::recordSelectionGlow(D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv,
                                          D3D12_CPU_DESCRIPTOR_HANDLE destRtv,
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
    // Resolve latest scene depth for 1× glow depth testing.
    m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, nullptr);
    m_device.resolveSceneDepth(m_commandList.Get());
    m_device.prepareResolvedDepthForDepthTest(m_commandList.Get());
    const D3D12_CPU_DESCRIPTOR_HANDLE resolvedDsv = m_device.resolvedDepthCPUHandle();

    if (m_glowState != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
      m_device.transitionResource(m_glowTexture.Get(), m_commandList.Get(),
                                 m_glowState, D3D12_RESOURCE_STATE_RENDER_TARGET);
      m_glowState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE glowRtv = m_glowRtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &glowRtv, FALSE, &resolvedDsv);
    const float glowClear[] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_commandList->ClearRenderTargetView(glowRtv, glowClear, 0, nullptr);

    m_selectionShader.paintGlow(m_commandList.Get(),
                                structureCB()->GetGPUVirtualAddress(),
                                m_structureCBVStride);

    m_ribbonSelectionShader.paintGlow(m_commandList.Get(),
                                      structureCB()->GetGPUVirtualAddress(),
                                      m_structureCBVStride);

    m_blurShader.paint(m_commandList.Get(), m_glowTexture.Get(), m_glowState);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, frameCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(1, structureCB()->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootConstantBufferView(2, lightsCB()->GetGPUVirtualAddress());
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

  RKImage image = captureOffscreenFrame(w, h);

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

  updateConstantBuffers();

  m_device.commandAllocator()->Reset();
  m_commandList->Reset(m_device.commandAllocator(), nullptr);

  const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = m_device.sceneColorCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_device.depthStencilCPUHandle();
  const D3D12_CPU_DESCRIPTOR_HANDLE targetRtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();

  recordScenePass(sceneRtv, dsv, width, height);
  m_device.resolveSceneColorTo(m_commandList.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
  recordSelectionGlow(sceneRtv, targetRtv, width, height);

  m_device.transitionResource(target.Get(), m_commandList.Get(),
                             D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = target.Get();
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
