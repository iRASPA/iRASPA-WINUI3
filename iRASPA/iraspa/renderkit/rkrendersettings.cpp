#include "rkrendersettings.h"

#include <algorithm>
#include <optional>

#include <windows.h>

namespace
{
  // Per user rather than per machine: these describe what the person at the keyboard finds
  // responsive, and two accounts on one machine may disagree.
  constexpr wchar_t settingsKeyPath[] = L"Software\\iRASPA\\Render";

  constexpr wchar_t interactiveRenderModeName[] = L"InteractiveRenderMode";
  constexpr wchar_t interactiveSampleCountName[] = L"InteractiveSampleCount";
  constexpr wchar_t interactiveRotatingSampleCountName[] = L"InteractiveRotatingSampleCount";
  constexpr wchar_t interactiveMaximumBouncesName[] = L"InteractiveMaximumBounces";
  constexpr wchar_t interactiveShadowsName[] = L"InteractiveShadows";

  /// Absent is distinct from zero throughout: a setting that has never been written takes a default
  /// that in one case depends on the hardware, so the two cannot be conflated.
  std::optional<DWORD> readValue(const wchar_t *name)
  {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKeyPath, name, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS)
      return std::nullopt;
    return value;
  }

  /// A setting that cannot be stored is not worth failing over: the session keeps the value it was
  /// given, and only the memory of it across runs is lost.
  void writeValue(const wchar_t *name, DWORD value)
  {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settingsKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
      return;
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
  }

  int readClamped(const wchar_t *name, int fallback, int lowest, int highest)
  {
    const std::optional<DWORD> stored = readValue(name);
    if (!stored)
      return fallback;
    // Through int64_t, a DWORD written by hand being able to exceed what an int holds.
    return int(std::clamp(int64_t(*stored), int64_t(lowest), int64_t(highest)));
  }

  bool g_isRayTracingSupported = false;
  bool g_tracesRaysInHardware = false;
}

RKRenderSettings &RKRenderSettings::shared()
{
  static RKRenderSettings settings;
  return settings;
}

void RKRenderSettings::setRaytracingCapability(bool isSupported, bool tracesInHardware)
{
  g_isRayTracingSupported = isSupported;
  g_tracesRaysInHardware = isSupported && tracesInHardware;
}

bool RKRenderSettings::isRayTracingSupported()
{
  return g_isRayTracingSupported;
}

bool RKRenderSettings::tracesRaysInHardware()
{
  return g_tracesRaysInHardware;
}

RKRenderMode RKRenderSettings::interactiveRenderMode() const
{
  const std::optional<DWORD> stored = readValue(interactiveRenderModeName);
  if (!stored || *stored != DWORD(RKRenderMode::rayTracing))
    return RKRenderMode::rasterization;
  return RKRenderMode::rayTracing;
}

void RKRenderSettings::setInteractiveRenderMode(RKRenderMode mode)
{
  writeValue(interactiveRenderModeName, DWORD(mode));
}

int RKRenderSettings::interactiveSampleCount() const
{
  return readClamped(interactiveSampleCountName, 32, 1, maximumSupportedInteractiveSamples);
}

void RKRenderSettings::setInteractiveSampleCount(int count)
{
  writeValue(interactiveSampleCountName,
             DWORD(std::clamp(count, 1, maximumSupportedInteractiveSamples)));
}

int RKRenderSettings::interactiveRotatingSampleCount() const
{
  return readClamped(interactiveRotatingSampleCountName, 8, 1, maximumSupportedInteractiveSamples);
}

void RKRenderSettings::setInteractiveRotatingSampleCount(int count)
{
  writeValue(interactiveRotatingSampleCountName,
             DWORD(std::clamp(count, 1, maximumSupportedInteractiveSamples)));
}

int RKRenderSettings::interactiveMaximumBounces() const
{
  return readClamped(interactiveMaximumBouncesName, 2, 0, maximumSupportedInteractiveBounces);
}

void RKRenderSettings::setInteractiveMaximumBounces(int bounces)
{
  writeValue(interactiveMaximumBouncesName,
             DWORD(std::clamp(bounces, 0, maximumSupportedInteractiveBounces)));
}

bool RKRenderSettings::interactiveShadows() const
{
  const std::optional<DWORD> stored = readValue(interactiveShadowsName);
  if (!stored)
    return tracesRaysInHardware();
  return *stored != 0;
}

void RKRenderSettings::setInteractiveShadows(bool shadows)
{
  writeValue(interactiveShadowsName, shadows ? 1u : 0u);
}

int RKRenderSettings::samplesPerInteractiveFrame(RKRenderQuality quality)
{
  switch (quality)
  {
  case RKRenderQuality::high:
  case RKRenderQuality::picture:
    return RKRenderSettings::shared().interactiveSampleCount();
  case RKRenderQuality::medium:
  case RKRenderQuality::low:
    return RKRenderSettings::shared().interactiveRotatingSampleCount();
  }
  return RKRenderSettings::shared().interactiveSampleCount();
}
