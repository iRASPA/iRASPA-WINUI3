/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include "rkstring.h"

using Microsoft::WRL::ComPtr;

class DirectXShader
{
public:
  DirectXShader() = default;
  virtual ~DirectXShader() = default;
  virtual void loadShader(ID3D12Device *device) = 0;

protected:
  static ComPtr<ID3DBlob> compileShader(const std::string &source, const char *entryPoint, const char *target);
  static void logBlobErrors(ID3DBlob *errors, const RKString &context);
};
