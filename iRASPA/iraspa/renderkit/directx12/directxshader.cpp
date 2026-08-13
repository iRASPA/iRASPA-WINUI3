/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxshader.h"
#include "rkstring.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

ComPtr<ID3DBlob> DirectXShader::compileShader(const std::string &source, const char *entryPoint, const char *target)
{
  ComPtr<ID3DBlob> shaderBlob;
  ComPtr<ID3DBlob> errorBlob;

  HRESULT hr = D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr,
                          entryPoint, target, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                          &shaderBlob, &errorBlob);
  if (FAILED(hr))
  {
    logBlobErrors(errorBlob.Get(), RKString("Failed to compile %1 (%2)").arg(entryPoint).arg(target));
    return nullptr;
  }
  if (errorBlob)
  {
    logBlobErrors(errorBlob.Get(), RKString("Warnings compiling %1").arg(entryPoint));
  }
  return shaderBlob;
}

void DirectXShader::logBlobErrors(ID3DBlob *errors, const RKString &context)
{
  std::string message = context.toStdString();
  if (errors)
  {
    const char *msg = static_cast<const char *>(errors->GetBufferPointer());
    message += ":";
    message += msg ? msg : "";
  }
  std::cerr << message;

  char tempPath[MAX_PATH] = {};
  if (GetTempPathA(MAX_PATH, tempPath))
  {
    const std::string path = std::string(tempPath) + "iraspa_volume.log";
    std::ofstream out(path, std::ios::app);
    if (out)
      out << message << '\n';
  }
}
