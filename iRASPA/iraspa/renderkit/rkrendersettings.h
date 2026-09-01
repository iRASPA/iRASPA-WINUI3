#pragma once

#include "rkrenderuniforms.h"

/// The render settings that describe the machine rather than the scene, and so are kept beside the
/// application instead of in the project.
///
/// Only the interactive path lives here. How many samples a frame can afford before it stops feeling
/// responsive depends on the GPU in front of the user, so carrying these in a document would mean a
/// file authored on a fast machine crawls when opened on a slow one. The corresponding export
/// settings belong to the project.
///
/// Cocoa keeps these in UserDefaults; the registry is what plays that part here. The one difference
/// is that whether the machine can trace at all is published by the renderer rather than asked of a
/// device: creating a second D3D12 device merely to ask would be the more expensive way to learn
/// something the renderer has already worked out.
class RKRenderSettings
{
public:
  static RKRenderSettings &shared();

  /// Neither cap is a limit of the tracer, which takes any bounce count: they exist because both
  /// fields are freely editable and render time grows about linearly with the count, there being no
  /// Russian roulette to cut long paths short.
  ///
  /// Interactive frames are capped tightly, since a value entered by mistake there stalls the frame
  /// loop and can trip the GPU watchdog. An export only takes longer, so it is allowed much more than
  /// the image needs: energy decays by the surface albedo at every bounce, and on these open, mostly
  /// diffuse scenes the image stops changing visibly after a handful.
  static constexpr int maximumSupportedInteractiveBounces = 8;
  static constexpr int maximumSupportedPictureBounces = 32;
  static constexpr int maximumSupportedInteractiveSamples = 4096;
  static constexpr int maximumSupportedPictureSamples = 65536;

  /// Told to us by the renderer once its device is up, \a tracesInHardware being false for a device
  /// that walks the acceleration structures in a shader, as the software adapter does. Asked for by
  /// the camera pane, which is only reachable once a renderer exists; before then the answer is that
  /// the machine cannot trace, which is the safe way round.
  static void setRaytracingCapability(bool isSupported, bool tracesInHardware);
  static bool isRayTracingSupported();
  static bool tracesRaysInHardware();

  /// Ray tracing silently falls back to rasterization on unsupported hardware, so that a stale
  /// setting can never leave the user without an image.
  RKRenderMode interactiveRenderMode() const;
  void setInteractiveRenderMode(RKRenderMode mode);

  /// Paths traced per pixel while the camera is at rest.
  int interactiveSampleCount() const;
  void setInteractiveSampleCount(int count);

  /// Paths traced per pixel while the camera is being moved. Fewer samples mean a noisier but faster
  /// frame; the colours do not change.
  int interactiveRotatingSampleCount() const;
  void setInteractiveRotatingSampleCount(int count);

  int interactiveMaximumBounces() const;
  void setInteractiveMaximumBounces(int bounces);

  /// Whether the render view spends a ray per light per pixel on shadows. Separate from the project's
  /// own shadow setting, which an export obeys wherever it is opened: an export may take as long as
  /// it needs, while a frame may not, so this defaults on only where the rays are traced in hardware.
  /// A machine that traces them in a shader can still be asked for them.
  bool interactiveShadows() const;
  void setInteractiveShadows(bool shadows);

  /// How many paths a frame of this quality may trace per pixel. The quality is what the view sets
  /// while it is being dragged, so this is what makes a rotating frame cheaper than one at rest.
  static int samplesPerInteractiveFrame(RKRenderQuality quality);

private:
  RKRenderSettings() = default;
};
