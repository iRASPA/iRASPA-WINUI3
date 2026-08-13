/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
 ********************************************************************************************************************/

#include "ribbonaolayout.h"
#include <algorithm>

int RKAmbientOcclusionSizing::maxTextureSize(int numberOfAtoms, int maxTextureDimension)
{
  const int cappedMax = std::min(maxTextureDimension, 16384);
  if (numberOfAtoms <= 64) return std::min(256, cappedMax);
  if (numberOfAtoms <= 256) return std::min(512, cappedMax);
  if (numberOfAtoms <= 1024) return std::min(1024, cappedMax);
  if (numberOfAtoms <= 65536) return std::min(2048, cappedMax);
  if (numberOfAtoms <= 524288) return std::min(4096, cappedMax);
  return std::min(8192, cappedMax);
}
