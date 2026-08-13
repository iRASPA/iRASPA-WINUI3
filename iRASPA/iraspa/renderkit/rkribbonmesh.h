/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
 ********************************************************************************************************************/

#pragma once

#include <vector>
#include <algorithm>
#include <tuple>
#include <cstdint>
#include "rkrenderuniforms.h"
#include "ribbonaolayout.h"

// A ribbon carries two coordinate pairs a sphere or cylinder does not: 'st' for the baked
// ambient-occlusion lightmap and 'stripeST' for the selection stripes. Rather than widen the RKVertex
// every other shader is pinned to, the ribbon keeps a vertex of its own.
struct RKRibbonVertex
{
  RKRibbonVertex(): position(float4()), normal(float4()), st(float2()) {}
  RKRibbonVertex(float4 pos, float4 norm, float2 c): position(pos), normal(norm), st(c) {}

  float4 position;
  float4 normal;
  float2 st;
  float2 pad = float2();
  float2 stripeST = float2();
};

/// Sub-range within the ribbon index buffer for one chain / segment / residue draw call
/// (Cocoa RKRibbonChainDrawRange).
struct RKRibbonChainDrawRange
{
  int indexStart = 0;
  int indexCount = 0;

  RKRibbonChainDrawRange() = default;
  RKRibbonChainDrawRange(int indexStart, int indexCount): indexStart(indexStart), indexCount(indexCount) {}
};

struct RKRibbonMesh
{
  static constexpr int subdivisionsPerSegment = 24;
  static constexpr int lightmapStripHeight = 32;
  static constexpr int lightmapWidth = 2048;

  std::vector<RKRibbonVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<RKRibbonChainDrawRange> chainDrawRanges;
  std::vector<RKRibbonChainDrawRange> segmentDrawRanges;
  std::vector<RKRibbonChainDrawRange> residueDrawRanges;
  std::vector<int> chainSplineSampleCounts;

  // The tag of the alpha carbon each segment and residue range was swept from, parallel to the range
  // vectors above. A residue the sweep skipped leaves no range, so counting ranges says nothing about
  // which residue a range belongs to; whoever owns the atoms can only recover that from these.
  std::vector<int64_t> segmentAlphaCarbonTags;
  std::vector<int64_t> residueAlphaCarbonTags;

  int numberOfChains() const { return static_cast<int>(chainDrawRanges.size()); }

  int maxSplineSampleCount() const
  {
    if (chainSplineSampleCounts.empty()) { return 1; }
    return *std::max_element(chainSplineSampleCounts.begin(), chainSplineSampleCounts.end());
  }

  int numberOfRings() const { return maxSplineSampleCount(); }

  static std::tuple<int, int, int> ambientOcclusionAtlasDimensions(int maxSplineSampleCount,
                                                                   int numberOfChains,
                                                                   int numberOfAtoms,
                                                                   int maxTextureDimension = 16384);

  /// Merges contiguous or overlapping visible ranges into fewer draw calls.
  /// Residue/segment ribbon ranges intentionally overlap by one ring pair at boundaries.
  static std::vector<RKRibbonChainDrawRange> mergedVisibleDrawRanges(
      const std::vector<RKRibbonChainDrawRange> &ranges,
      const std::vector<bool> &visible);

  RKRibbonMesh() = default;
  RKRibbonMesh(std::vector<RKRibbonVertex> vertices,
               std::vector<uint32_t> indices,
               std::vector<RKRibbonChainDrawRange> chainDrawRanges):
    vertices(std::move(vertices)), indices(std::move(indices)), chainDrawRanges(std::move(chainDrawRanges)) {}
};
