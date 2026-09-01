/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

/// Shader Model 6 compiler for the ray-tracing kernels. The rest of the renderkit goes through
/// D3DCompile, which stops at Shader Model 5.1 and so cannot compile a RayQuery; inline ray
/// tracing needs cs_6_5, which only DXC emits.
///
/// dxcompiler.dll is loaded at first use rather than linked, because the Windows SDK ships the
/// DLL but no import library, and because a machine without it has to keep working: the whole
/// ray-tracing path is optional and the renderer falls back to rasterizing when it is missing.
class DirectXDxcCompiler
{
public:
  /// Compiled DXIL. Keeps the DXC blob alive, the bytecode pointing into it.
  struct Result
  {
    ComPtr<IDxcBlob> object;
    /// Compiler diagnostics, present on both failure and a warning-only success.
    std::string diagnostics;

    bool succeeded() const { return object != nullptr && object->GetBufferSize() > 0; }
    D3D12_SHADER_BYTECODE bytecode() const
    {
      if (!succeeded())
        return D3D12_SHADER_BYTECODE{nullptr, 0};
      return D3D12_SHADER_BYTECODE{object->GetBufferPointer(), object->GetBufferSize()};
    }
  };

  /// True when dxcompiler.dll could be loaded and the compiler objects created.
  static bool isAvailable();

  /// Why isAvailable() is false, for the log line the caller writes when it gives up on
  /// ray tracing. Empty when the compiler did load.
  static const std::string &unavailableReason();

  /// Compiles \a source as a single translation unit. There is no include handler: the
  /// renderkit shaders are string literals that already carry everything they need.
  static Result compile(const std::string &source, const wchar_t *entryPoint, const wchar_t *target,
                        const std::vector<std::wstring> &extraArguments = {});

  /// Compiles a minimal cs_6_5 RayQuery kernel, so a stale dxcompiler.dll that cannot emit
  /// inline ray tracing is caught before the tracer is built rather than through a wall of
  /// shader errors. Cached after the first call.
  static bool canCompileInlineRaytracing();

  /// Writes \a message to stderr and to the renderkit shader log, as the D3DCompile path does.
  static void logDiagnostics(const std::string &context, const std::string &message);
};
