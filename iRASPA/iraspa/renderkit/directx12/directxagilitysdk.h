#pragma once

#include <windows.h>

/// Opts the executable including this into the DirectX 12 Agility SDK, which is what makes the
/// redistributable D3D12 runtime in the D3D12\ subfolder be used in place of the one that shipped
/// with Windows.
///
/// This is not a nicety. Tracing an exported picture on a machine whose card has no DXR 1.1 falls
/// back to the WARP software adapter, and that fallback needs the modern d3d10warp.dll shipped
/// beside the application, because the WARP in Windows offers no ray tracing at all. Shipping that
/// driver without also shipping the runtime it belongs to pairs a recent WARP with whatever D3D12
/// the operating system happens to have, and that pairing fails in the worst possible way: it
/// reports ray-tracing tier 1.1, builds acceleration structures without complaint, and then finds
/// nothing along any ray. Every atom is missed, no shadow is cast, and nothing anywhere says why.
/// Measured with tools\warpprobe: one box and one ray straight through it, which the OS runtime
/// reports as no intersection and the runtime here reports as a hit.
///
/// Two rules come with this:
///
/// - Include it in exactly one translation unit of each executable. These are exported symbols, so
///   a second inclusion in the same binary will not link, and an executable that omits it gets the
///   operating system's runtime no matter what sits in its output directory.
/// - kD3D12SDKVersion must equal the version of the D3D12Core.dll actually deployed. D3D12 fails
///   device creation outright on a mismatch, which at least fails loudly. The deploy target in the
///   project file copies that DLL, and external\d3d12 records which release it came from.
namespace DirectXAgilitySDK
{
  /// Minor version of the Microsoft.Direct3D.D3D12 release deployed into D3D12\, so 1.619.5 is 619.
  inline constexpr UINT kD3D12SDKVersion = 619;
}

extern "C"
{
  __declspec(dllexport) extern const UINT D3D12SDKVersion = DirectXAgilitySDK::kD3D12SDKVersion;
  // Relative to the executable. A subfolder rather than the executable's own directory, which is
  // what Microsoft advise: side by side, the debug layer of one release can be picked up against
  // the core of another.
  __declspec(dllexport) extern const char *D3D12SDKPath = ".\\D3D12\\";
}
