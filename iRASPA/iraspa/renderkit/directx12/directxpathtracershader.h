/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "directxpathtracercommon.h"
#include "directxpathtracergeometry.h"
#include "directxshadervisibleheap.h"
#include "dx12devicecontext.h"

#include <memory>
#include <string>

/// The path tracer: traces the molecular geometry and composites the result over the rasterized
/// rest of the scene.
///
/// Two kernels. The accumulate kernel traces samples into running per-pixel sums, and the resolve
/// kernel averages those sums, tone-maps them and mixes them over the raster image by how much of
/// each pixel the trace covered. They are apart because a still image is traced in batches: no one
/// dispatch may run long enough for the display driver to decide the device has hung, so the
/// samples of an image are spread over several command lists with a wait between them, and only
/// then resolved.
///
/// The pass owns its command list rather than joining the frame's. It is the still-image path, and
/// a still image is rendered outside the frame loop with the GPU idle either side of it.
class DirectXPathTracerShader
{
public:
  /// How thoroughly to trace. Sample count buys convergence, bounce count buys the light that
  /// arrives by way of another surface, and neither is capped by anything but patience: render
  /// time grows about linearly in both, there being no Russian roulette to cut long paths short.
  struct Settings
  {
    uint32_t sampleCount = 256;
    uint32_t maximumBounces = 2;
    // Kept small so that a single command list never runs long enough to trip the watchdog on a
    // large image.
    uint32_t samplesPerDispatch = 8;
    float ambientOcclusionStrength = 1.0f;

    /// Ambient occlusion is measured from where the ray leaving the primary hit gets to, so asking
    /// for it asks for at least one bounce however few were requested.
    uint32_t effectiveMaximumBounces() const
    {
      return (ambientOcclusionStrength > 0.0f) ? ((maximumBounces < 1) ? 1u : maximumBounces)
                                               : maximumBounces;
    }
  };

  DirectXPathTracerShader() = default;
  ~DirectXPathTracerShader();

  DirectXPathTracerShader(const DirectXPathTracerShader &) = delete;
  DirectXPathTracerShader &operator=(const DirectXPathTracerShader &) = delete;

  void initialize(Dx12DeviceContext &context);
  void release();

  bool isReady() const { return m_ready; }
  const std::string &status() const { return m_status; }

  /// Traces \a settings.sampleCount samples of a \a width by \a height image and composites them
  /// over \a sceneColor, leaving the result in compositeTexture(). Returns once the GPU is done
  /// with all of it, the caller being free to copy the result away.
  ///
  /// \a sceneColor must hold the rasterized scene without its molecular geometry, and \a sceneDepth
  /// the depth that went with it; both are read, neither is written. Both must already be in a state
  /// a compute shader can read them in.
  bool render(Dx12DeviceContext &context, const DirectXPathTracerGeometry &geometry,
              D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
              D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress, ID3D12Resource *sceneColor,
              ID3D12Resource *sceneDepth, UINT width, UINT height, const Settings &settings);

  /// Traces \a samplesThisFrame samples of one frame of the render view into \a commandList, which
  /// is the frame's own: this encodes and returns, waiting on nothing, the frame being submitted as a
  /// whole once the rest of it has been recorded. The result is left in compositeTexture() in
  /// PIXEL_SHADER_RESOURCE, ready to be drawn to the back buffer.
  ///
  /// Every frame starts its sums afresh rather than adding to the last one's. Accumulating across
  /// frames would converge a still view, but it would also smear a moving one and change its colours
  /// as it settled; \a settings.sampleCount is ignored for the same reason, the caller deciding what
  /// this frame can afford from whether the camera is moving.
  bool encodeInteractive(ID3D12GraphicsCommandList4 *commandList, Dx12DeviceContext &context,
                         const DirectXPathTracerGeometry &geometry,
                         D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
                         D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress,
                         ID3D12Resource *sceneColor, ID3D12Resource *sceneDepth, UINT width,
                         UINT height, const Settings &settings, UINT samplesThisFrame);

  /// The composited image, in COPY_SOURCE state after a successful render(). Always
  /// R8G8B8A8_UNORM, whichever way round the scene it was composited over stores its channels.
  ID3D12Resource *compositeTexture() const { return m_composite.Get(); }

  /// The depth of what is visible at each pixel, and which edge cues that surface asked for, one
  /// value per pixel in row-major order. Written by the resolve and left in UNORDERED_ACCESS, so a
  /// pass that reads them has to transition them and put them back.
  ID3D12Resource *compositeDepthBuffer() const { return m_compositeDepth.Get(); }
  ID3D12Resource *compositeCueMaskBuffer() const { return m_compositeCueMask.Get(); }

private:
  bool fail(const std::string &reason);
  bool createPipelines(ID3D12Device *device);
  bool ensureBuffers(ID3D12Device *device, UINT width, UINT height);
  bool ensureCommandList(ID3D12Device *device);
  bool executeAndWait(Dx12DeviceContext &context);

  /// How many pixels a primary ray found a surface at, and between which depths. Stalls on a
  /// readback, so it belongs to the still path alone.
  std::string describePrimaryRays(Dx12DeviceContext &context, UINT pixels);

  /// The two halves of a trace, so that the still path and the frame path bind them identically. The
  /// accumulate half adds a dispatch's samples to the per-pixel sums; the resolve half averages
  /// them, tone-maps and mixes the result over the rasterized scene.
  void encodeAccumulate(ID3D12GraphicsCommandList4 *commandList,
                        const DirectXPathTracerGeometry &geometry,
                        D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress,
                        D3D12_GPU_VIRTUAL_ADDRESS lightConstantsAddress,
                        ID3D12Resource *uniforms, UINT groupsX, UINT groupsY);
  bool encodeResolve(ID3D12GraphicsCommandList4 *commandList, Dx12DeviceContext &context,
                     const DirectXPathTracerGeometry &geometry, ID3D12Resource *uniforms,
                     ID3D12Resource *sceneColor, ID3D12Resource *sceneDepth, UINT groupsX,
                     UINT groupsY, D3D12_RESOURCE_STATES compositeEndState);

  ComPtr<ID3D12RootSignature> m_accumulateRootSignature;
  ComPtr<ID3D12PipelineState> m_accumulatePipeline;
  ComPtr<ID3D12RootSignature> m_resolveRootSignature;
  ComPtr<ID3D12PipelineState> m_resolvePipeline;

  ComPtr<ID3D12CommandAllocator> m_commandAllocator;
  ComPtr<ID3D12GraphicsCommandList4> m_commandList;
  // Waited on between the batches an image is traced in, so the pass needs one of its own: the
  // renderer's belongs to the frame loop this does not run in.
  std::unique_ptr<Dx12DeviceContext::Fence> m_fence;

  // Sums over the samples of a pixel: the direct term and its hit count, the indirect term and the
  // escape count that measures the occlusion, and what the primary ray met.
  ComPtr<ID3D12Resource> m_accumulation;
  ComPtr<ID3D12Resource> m_indirect;
  ComPtr<ID3D12Resource> m_surfaceInfo;
  // What is finally in front at a pixel, which the rasterizer's own depth buffer cannot say because
  // the molecular geometry never went through it, and which edge cues that surface asked for, which
  // for the same reason is not in the stencil either.
  ComPtr<ID3D12Resource> m_compositeDepth;
  ComPtr<ID3D12Resource> m_compositeCueMask;
  ComPtr<ID3D12Resource> m_composite;
  ComPtr<ID3D12Resource> m_uniformBuffer;
  // The frame path cannot share the one above: it does not wait, so the uniforms of the frame being
  // recorded would overwrite those of a frame the GPU has yet to read. One per frame in flight, used
  // in turn, is what the renderer does with its own constant buffers for the same reason.
  ComPtr<ID3D12Resource> m_interactiveUniformBuffers[Dx12DeviceContext::kInflightFrameCount];
  UINT m_interactiveUniformIndex = 0;
  /// Advanced per frame so that no two consecutive frames trace the same paths, which would make the
  /// noise of a moving image stand still and read as a fixed pattern on the surfaces.
  UINT m_interactiveFrameCounter = 0;

  // The two input textures are the only things here that need descriptors rather than root views,
  // and a texture cannot be a root view.
  DirectXShaderVisibleHeap m_heap;

  UINT m_pixelCapacity = 0;
  UINT m_compositeWidth = 0;
  UINT m_compositeHeight = 0;
  D3D12_RESOURCE_STATES m_compositeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  bool m_ready = false;
  std::string m_status = "the path tracer has not been initialized";
};
