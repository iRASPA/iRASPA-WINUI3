/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "directxpathtracercommon.h"
#include "directxpathtracergeometry.h"
#include "dx12devicecontext.h"
#include <string>

/// Traces which lights reach the surface at each pixel, into one bit per light, for the raster
/// passes to gate their shading with.
///
/// This is the smallest useful piece of ray tracing in the renderer: the picture stays rasterized
/// and only gains the shadows a rasterizer cannot work out for itself. It is also what makes an
/// off-axis light rig usable, a raster pass otherwise lighting every surface a light faces whether
/// or not anything stands in the way.
///
/// The mask is a buffer rather than a texture so the raster passes can take it as a root
/// shader-resource view, which survives the descriptor-heap swaps the renderer makes between passes.
/// Everything the kernel reads is bound the same way, so this pass needs no descriptor heap at all.
///
/// When the mask is absent, unavailable or has not been traced, shadowMaskAtFragment reports every
/// light lit, and the raster passes shade exactly as they did before any of this existed.
class DirectXShadowMaskShader
{
public:
  DirectXShadowMaskShader() = default;
  ~DirectXShadowMaskShader();

  DirectXShadowMaskShader(const DirectXShadowMaskShader &) = delete;
  DirectXShadowMaskShader &operator=(const DirectXShadowMaskShader &) = delete;

  /// Compiles the kernel and creates the root signature. Safe to call when the device cannot trace:
  /// isReady() is then false and status() says why.
  void initialize(Dx12DeviceContext &context);

  void release();

  bool isReady() const { return m_ready; }

  /// Why the last initialize() or encode() did what it did. Picture export runs where the log window
  /// does not exist, so the caller can hand this to the application.
  const std::string &status() const { return m_status; }

  /// Traces the mask for a viewport of `width` x `height`. `commandList` has to be the ray-tracing
  /// capable list, and the geometry has to have been built. Returns false and leaves the mask
  /// untraced when anything is missing, in which case the caller must report no mask to the raster
  /// passes rather than a stale one.
  bool encode(ID3D12GraphicsCommandList4 *commandList, Dx12DeviceContext &context,
              const DirectXPathTracerGeometry &geometry, D3D12_GPU_VIRTUAL_ADDRESS frameConstants,
              D3D12_GPU_VIRTUAL_ADDRESS lightConstants, UINT width, UINT height);

  /// The traced mask, in a state the raster passes can read as a root shader-resource view. Null
  /// until the first successful encode().
  ID3D12Resource *maskBuffer() const { return m_maskBuffer.Get(); }

  /// A one-element buffer of "every light lit", to keep the raster root signature satisfied on the
  /// frames and devices where nothing is traced. A root shader-resource view has no null binding, so
  /// something valid has to be there even when the shaders will not look at it.
  ID3D12Resource *allLitBuffer() const { return m_allLitBuffer.Get(); }

  /// Size of the last traced mask, which is what the raster passes must index it by. Zero when
  /// nothing has been traced.
  UINT maskWidth() const { return m_maskWidth; }
  UINT maskHeight() const { return m_maskHeight; }

  /// Creates the one-element all-lit buffer, which is needed whether or not tracing is possible.
  bool createFallback(ID3D12Device *device);

private:
  bool ensureMaskBuffer(ID3D12Device *device, UINT width, UINT height);
  bool fail(const std::string &reason);

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12PipelineState> m_pipelineState;

  ComPtr<ID3D12Resource> m_maskBuffer;
  ComPtr<ID3D12Resource> m_allLitBuffer;
  ComPtr<ID3D12Resource> m_uniformBuffers[Dx12DeviceContext::kInflightFrameCount];

  /// What the mask buffer can hold, which is kept at the high-water mark so that resizing a window
  /// does not reallocate on the way back down.
  UINT m_maskCapacity = 0;
  UINT m_maskWidth = 0;
  UINT m_maskHeight = 0;
  /// Set once the buffer has been transitioned out of its initial state, so that the first encode
  /// transitions from the state it was created in and later ones from the raster passes' state.
  bool m_maskReadable = false;
  bool m_ready = false;
  std::string m_status = "the shadow mask has not been initialized";
};
