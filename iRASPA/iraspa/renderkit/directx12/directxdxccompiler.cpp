/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxdxccompiler.h"
#include <fstream>
#include <iostream>

namespace
{
  struct DxcRuntime
  {
    HMODULE library = nullptr;
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    std::string reason;
  };

  DxcRuntime loadDxc()
  {
    DxcRuntime runtime;

    // dxcompiler.dll loads dxil.dll itself, to sign the DXIL it emits; the D3D12 runtime
    // rejects an unsigned shader unless the machine has developer mode on. Both DLLs are
    // deployed next to the exe, which the default search order looks in first.
    LoadLibraryW(L"dxil.dll");

    runtime.library = LoadLibraryW(L"dxcompiler.dll");
    if (!runtime.library)
    {
      runtime.reason = "dxcompiler.dll could not be loaded (error "
                       + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
      return runtime;
    }

    const DxcCreateInstanceProc createInstance =
        reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(runtime.library, "DxcCreateInstance"));
    if (!createInstance)
    {
      runtime.reason = "dxcompiler.dll does not export DxcCreateInstance";
      return runtime;
    }

    if (FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&runtime.utils))))
    {
      runtime.reason = "dxcompiler.dll could not create IDxcUtils";
      return runtime;
    }
    // IDxcCompiler3 dates from the same release as Shader Model 6.5, so a DLL too old for
    // inline ray tracing tends to fail here rather than on the first compile.
    if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&runtime.compiler))))
    {
      runtime.reason = "dxcompiler.dll is too old: it could not create IDxcCompiler3";
      runtime.utils.Reset();
      return runtime;
    }
    return runtime;
  }

  DxcRuntime &dxc()
  {
    static DxcRuntime runtime = loadDxc();
    return runtime;
  }

  // Smallest kernel that needs everything inline ray tracing brings with it: the Shader Model
  // 6.5 RayQuery template, an acceleration-structure binding and a traversal.
  const char *const inlineRaytracingProbe = R"(
RaytracingAccelerationStructure probeScene : register(t0);
RWTexture2D<float> probeOutput : register(u0);

[numthreads(8, 8, 1)]
void probeKernel(uint3 threadPosition : SV_DispatchThreadID)
{
  RayDesc ray;
  ray.Origin = float3(0.0, 0.0, 0.0);
  ray.Direction = float3(0.0, 0.0, 1.0);
  ray.TMin = 0.0;
  ray.TMax = 1.0;

  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
  query.TraceRayInline(probeScene, RAY_FLAG_NONE, 0xff, ray);
  query.Proceed();
  probeOutput[threadPosition.xy] = (query.CommittedStatus() == COMMITTED_NOTHING) ? 1.0 : 0.0;
}
)";
}

bool DirectXDxcCompiler::isAvailable()
{
  const DxcRuntime &runtime = dxc();
  return runtime.compiler != nullptr && runtime.utils != nullptr;
}

const std::string &DirectXDxcCompiler::unavailableReason()
{
  return dxc().reason;
}

DirectXDxcCompiler::Result DirectXDxcCompiler::compile(const std::string &source, const wchar_t *entryPoint,
                                                      const wchar_t *target,
                                                      const std::vector<std::wstring> &extraArguments)
{
  Result result;

  DxcRuntime &runtime = dxc();
  if (!runtime.compiler)
  {
    result.diagnostics = runtime.reason;
    return result;
  }

  DxcBuffer buffer = {};
  buffer.Ptr = source.data();
  buffer.Size = source.size();
  buffer.Encoding = DXC_CP_UTF8;

  std::vector<std::wstring> owned;
  owned.push_back(L"-E");
  owned.push_back(entryPoint);
  owned.push_back(L"-T");
  owned.push_back(target);
  // matches the D3DCOMPILE_ENABLE_STRICTNESS the D3DCompile path asks for
  owned.push_back(L"-Ges");
  owned.insert(owned.end(), extraArguments.begin(), extraArguments.end());

  std::vector<LPCWSTR> arguments;
  arguments.reserve(owned.size());
  for (const std::wstring &argument : owned)
    arguments.push_back(argument.c_str());

  ComPtr<IDxcResult> compiled;
  if (FAILED(runtime.compiler->Compile(&buffer, arguments.data(), static_cast<UINT32>(arguments.size()),
                                       nullptr, IID_PPV_ARGS(&compiled)))
      || !compiled)
  {
    result.diagnostics = "DXC refused the compile request";
    return result;
  }

  ComPtr<IDxcBlobUtf8> errors;
  if (SUCCEEDED(compiled->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))
      && errors && errors->GetStringLength() > 0)
  {
    result.diagnostics.assign(errors->GetStringPointer(), errors->GetStringLength());
  }

  HRESULT status = E_FAIL;
  if (FAILED(compiled->GetStatus(&status)) || FAILED(status))
    return result;

  compiled->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&result.object), nullptr);
  return result;
}

bool DirectXDxcCompiler::canCompileInlineRaytracing()
{
  static const bool supported = []
  {
    if (!isAvailable())
    {
      logDiagnostics("Inline ray tracing unavailable", unavailableReason());
      return false;
    }
    const Result probe = compile(inlineRaytracingProbe, L"probeKernel", L"cs_6_5");
    if (!probe.succeeded())
    {
      logDiagnostics("dxcompiler.dll cannot compile cs_6_5 RayQuery", probe.diagnostics);
      return false;
    }
    return true;
  }();
  return supported;
}

void DirectXDxcCompiler::logDiagnostics(const std::string &context, const std::string &message)
{
  std::string line = context;
  if (!message.empty())
  {
    line += ": ";
    line += message;
  }
  std::cerr << line << '\n';

  char tempPath[MAX_PATH] = {};
  if (GetTempPathA(MAX_PATH, tempPath))
  {
    const std::string path = std::string(tempPath) + "iraspa_volume.log";
    std::ofstream out(path, std::ios::app);
    if (out)
      out << line << '\n';
  }
}
