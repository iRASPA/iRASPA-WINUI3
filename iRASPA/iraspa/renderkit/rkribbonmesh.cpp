/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
 ********************************************************************************************************************/

#include "rkribbonmesh.h"
#include <algorithm>
#include <optional>

std::tuple<int, int, int> RKRibbonMesh::ambientOcclusionAtlasDimensions(int maxSplineSampleCount,
                                                                        int numberOfChains,
                                                                        int numberOfAtoms,
                                                                        int maxTextureDimension)
{
  const int stripHeight = lightmapStripHeight;
  const int alignedRingCount = std::max(maxSplineSampleCount - 1, 1);
  const int ringBasedWidth = ((alignedRingCount + 127) / 128) * 128;
  const int atomCap = RKAmbientOcclusionSizing::maxTextureSize(numberOfAtoms, maxTextureDimension);
  const int width = std::min(maxTextureDimension, std::max(256, std::min(atomCap, ringBasedWidth)));
  const int height = std::min(maxTextureDimension, std::max(stripHeight, stripHeight * std::max(numberOfChains, 1)));
  return {width, height, stripHeight};
}

std::vector<RKRibbonChainDrawRange> RKRibbonMesh::mergedVisibleDrawRanges(
    const std::vector<RKRibbonChainDrawRange> &ranges,
    const std::vector<bool> &visible)
{
  std::vector<RKRibbonChainDrawRange> merged;
  if (visible.size() != ranges.size()) return merged;
  merged.reserve(ranges.size());

  std::optional<int> currentStart;
  int currentEnd = 0;

  for (size_t index = 0; index < ranges.size(); ++index)
  {
    const RKRibbonChainDrawRange &range = ranges[index];
    if (!visible[index] || range.indexCount <= 0)
    {
      if (currentStart.has_value())
      {
        merged.emplace_back(*currentStart, currentEnd - *currentStart);
        currentStart.reset();
      }
      continue;
    }

    const int rangeEnd = range.indexStart + range.indexCount;
    if (currentStart.has_value() && range.indexStart <= currentEnd)
    {
      currentEnd = std::max(currentEnd, rangeEnd);
    }
    else
    {
      if (currentStart.has_value())
      {
        merged.emplace_back(*currentStart, currentEnd - *currentStart);
      }
      currentStart = range.indexStart;
      currentEnd = rangeEnd;
    }
  }

  if (currentStart.has_value())
  {
    merged.emplace_back(*currentStart, currentEnd - *currentStart);
  }
  return merged;
}
