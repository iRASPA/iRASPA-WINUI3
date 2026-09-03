/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "skwellsurface.h"
#include <skcomputeisosurface.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

float SKWellSurface::effectiveTrimIsovalue(const std::vector<float> &field, double isovalue, int3 dimensions)
{
  const size_t numberOfGridPoints =
      static_cast<size_t>(dimensions.x) * dimensions.y * dimensions.z;
  float minimumEnergy = std::numeric_limits<float>::max();
  for (size_t i = 0; i < numberOfGridPoints; ++i)
    minimumEnergy = std::min(minimumEnergy, field[3 * i]);

  float iso = static_cast<float>(isovalue);
  if (!(iso > minimumEnergy))
  {
    iso = 0.25f * minimumEnergy;
    std::cerr << "SKWellSurface: iso " << isovalue << " K is below the deepest well (" << minimumEnergy
              << " K); trimming the well surface at " << iso << " K instead.\n";
  }
  return iso;
}

std::vector<float4> SKWellSurface::constructWellSurface(const std::vector<float> &field, double isovalue,
                                                        int3 dimensions)
{
  const size_t numberOfGridPoints = static_cast<size_t>(dimensions.x) * dimensions.y * dimensions.z;
  if (numberOfGridPoints == 0 || field.size() < 3 * numberOfGridPoints)
  {
    std::cerr << "SKWellSurface: the field grid is empty or does not match the dimensions\n";
    return {};
  }

  const float iso = effectiveTrimIsovalue(field, isovalue, dimensions);

  // The pore, trimmed: inside is negative, like the energy grid the isosurface machinery expects.
  std::vector<float> combined(numberOfGridPoints);
  float distanceMax = -std::numeric_limits<float>::max();
  float minimumCombined = std::numeric_limits<float>::max();
  float maximumCombined = -std::numeric_limits<float>::max();
  for (size_t i = 0; i < numberOfGridPoints; ++i)
  {
    combined[i] = std::max(-field[3 * i + 1], energyScale * (field[3 * i] - iso));
    distanceMax = std::max(distanceMax, field[3 * i + 1]);
    minimumCombined = std::min(minimumCombined, combined[i]);
    maximumCombined = std::max(maximumCombined, combined[i]);
  }

  // { d > 0 and U < iso } empty: the probe's contact diameter does not fit, so there is no sheet. Marching
  // cubes must not invent a high isovalue on this mixed-unit field --- that surface sits inside the atoms,
  // closer to them than the 0 K isosurface. The merged-well filament along the channel axis is the overlay,
  // not a stand-in.
  if (!(minimumCombined < 0.0f && maximumCombined > 0.0f))
  {
    std::cerr << "SKWellSurface: the probe does not fit as a contact sheet (largest opening " << distanceMax
              << " A relative to the contact diameter). The adsorbed molecule sits on the channel axis "
                 "instead.\n";
    return {};
  }

  return SKComputeIsosurface::computeIsosurface(dimensions, &combined, 0.0);
}

std::vector<float4> SKWellSurface::constructWellFilament(const std::vector<float> &field, double isovalue,
                                                         int3 dimensions, double3x3 unitCell)
{
  const int nx = dimensions.x;
  const int ny = dimensions.y;
  const int nz = dimensions.z;
  const size_t numberOfGridPoints = static_cast<size_t>(nx) * ny * nz;
  if (nx <= 0 || ny <= 0 || nz <= 0 || field.size() < 3 * numberOfGridPoints)
    return {};

  const float iso = effectiveTrimIsovalue(field, isovalue, dimensions);

  std::vector<bool> candidate(numberOfGridPoints, false);
  std::vector<bool> seeds(numberOfGridPoints, false);
  bool anySeed = false;
  for (size_t i = 0; i < numberOfGridPoints; ++i)
  {
    const float energyTerm = energyScale * (field[3 * i] - iso);
    const float distance = field[3 * i + 1];
    if (distance < 0.0f && energyTerm < 0.0f)
    {
      candidate[i] = true;
      if (field[3 * i + 2] < filamentReliabilityThreshold)
      {
        seeds[i] = true;
        anySeed = true;
      }
    }
  }
  if (!anySeed)
    return {};

  // Geodesic closing inside the contact/energy region: fill reliability gaps, then restore any rim seeds
  // that erosion ate because the pinch is only a voxel or two thick.
  std::vector<bool> closed = seeds;
  for (int step = 0; step < filamentClosingRadius; ++step)
  {
    closed = dilateBinary(closed, nx, ny, nz);
    for (size_t i = 0; i < numberOfGridPoints; ++i)
      if (!candidate[i])
        closed[i] = false;
  }
  for (int step = 0; step < filamentClosingRadius; ++step)
    closed = erodeBinary(closed, nx, ny, nz);
  for (size_t i = 0; i < numberOfGridPoints; ++i)
  {
    if (!candidate[i])
      closed[i] = false;
    else if (seeds[i])
      closed[i] = true;
  }
  fillEnclosedCavities(closed, candidate, nx, ny, nz);

  std::vector<float> combined(numberOfGridPoints);
  size_t interiorPoints = 0;
  for (size_t i = 0; i < numberOfGridPoints; ++i)
  {
    const float energyTerm = energyScale * (field[3 * i] - iso);
    const float distance = field[3 * i + 1];
    const float raw = std::max(field[3 * i + 2] - filamentReliabilityThreshold,
                               std::max(distance, energyTerm));
    // Filled holes keep max(d, s(U-iso)), the same two fields the well surface is built from, so the d = 0
    // cap still meets the sheet. Reliability stays in the raw field on the tube's side wall.
    const float value =
        (closed[i] && raw >= 0.0f) ? std::min(-1.0e-4f, std::max(distance, energyTerm)) : raw;
    combined[i] = value;
    if (value < 0.0f)
      ++interiorPoints;
  }
  if (interiorPoints == 0)
    return {};

  std::vector<float4> triangleData = SKComputeIsosurface::computeIsosurface(dimensions, &combined, 0.0);
  if (triangleData.empty())
    return {};
  return removeFilamentSpecks(triangleData, unitCell);
}

std::vector<bool> SKWellSurface::dilateBinary(const std::vector<bool> &mask, int nx, int ny, int nz)
{
  std::vector<bool> out = mask;
  const int nxy = nx * ny;
  for (int z = 0; z < nz; ++z)
  {
    const int zz[3] = { (z == 0) ? nz - 1 : z - 1, z, (z + 1 == nz) ? 0 : z + 1 };
    for (int y = 0; y < ny; ++y)
    {
      const int yy[3] = { (y == 0) ? ny - 1 : y - 1, y, (y + 1 == ny) ? 0 : y + 1 };
      for (int x = 0; x < nx; ++x)
      {
        const int i = x + nx * y + nxy * z;
        if (mask[i])
          continue;
        const int xm = (x == 0) ? nx - 1 : x - 1;
        const int xp = (x + 1 == nx) ? 0 : x + 1;
        bool found = false;
        for (int zb : zz)
        {
          for (int yb : yy)
          {
            const int row = nx * yb + nxy * zb;
            if (mask[xm + row] || mask[x + row] || mask[xp + row])
            {
              found = true;
              break;
            }
          }
          if (found)
            break;
        }
        if (found)
          out[i] = true;
      }
    }
  }
  return out;
}

std::vector<bool> SKWellSurface::erodeBinary(const std::vector<bool> &mask, int nx, int ny, int nz)
{
  std::vector<bool> out = mask;
  const int nxy = nx * ny;
  for (int z = 0; z < nz; ++z)
  {
    const int zz[3] = { (z == 0) ? nz - 1 : z - 1, z, (z + 1 == nz) ? 0 : z + 1 };
    for (int y = 0; y < ny; ++y)
    {
      const int yy[3] = { (y == 0) ? ny - 1 : y - 1, y, (y + 1 == ny) ? 0 : y + 1 };
      for (int x = 0; x < nx; ++x)
      {
        const int i = x + nx * y + nxy * z;
        if (!mask[i])
          continue;
        const int xm = (x == 0) ? nx - 1 : x - 1;
        const int xp = (x + 1 == nx) ? 0 : x + 1;
        bool keep = true;
        for (int zb : zz)
        {
          for (int yb : yy)
          {
            const int row = nx * yb + nxy * zb;
            if (!mask[xm + row] || !mask[x + row] || !mask[xp + row])
            {
              keep = false;
              break;
            }
          }
          if (!keep)
            break;
        }
        if (!keep)
          out[i] = false;
      }
    }
  }
  return out;
}

void SKWellSurface::fillEnclosedCavities(std::vector<bool> &closed, const std::vector<bool> &candidate,
                                         int nx, int ny, int nz)
{
  const size_t n = static_cast<size_t>(nx) * ny * nz;
  const int nxy = nx * ny;
  std::vector<bool> exterior(n, false);
  std::vector<int> stack;
  stack.reserve(n / 8);
  for (size_t i = 0; i < n; ++i)
  {
    if (!closed[i] && !candidate[i])
    {
      exterior[i] = true;
      stack.push_back(static_cast<int>(i));
    }
  }
  while (!stack.empty())
  {
    const int i = stack.back();
    stack.pop_back();
    const int x = i % nx;
    const int y = (i / nx) % ny;
    const int z = i / nxy;
    const int xm = (x == 0) ? nx - 1 : x - 1;
    const int xp = (x + 1 == nx) ? 0 : x + 1;
    const int ym = (y == 0) ? ny - 1 : y - 1;
    const int yp = (y + 1 == ny) ? 0 : y + 1;
    const int zm = (z == 0) ? nz - 1 : z - 1;
    const int zp = (z + 1 == nz) ? 0 : z + 1;
    const int neighbors[6] = {
      xm + nx * y + nxy * z, xp + nx * y + nxy * z,
      x + nx * ym + nxy * z, x + nx * yp + nxy * z,
      x + nx * y + nxy * zm, x + nx * y + nxy * zp
    };
    for (int nb : neighbors)
    {
      if (!closed[nb] && !exterior[nb])
      {
        exterior[nb] = true;
        stack.push_back(nb);
      }
    }
  }
  for (size_t i = 0; i < n; ++i)
    if (!closed[i] && !exterior[i])
      closed[i] = true;
}

std::vector<float4> SKWellSurface::removeFilamentSpecks(const std::vector<float4> &triangleData,
                                                        double3x3 unitCell)
{
  // Nine float4 per triangle: three vertices of (position, normal, pad).
  const size_t triangles = triangleData.size() / 9;
  if (triangles == 0)
    return {};

  std::vector<size_t> parent(triangles);
  std::iota(parent.begin(), parent.end(), size_t(0));
  std::function<size_t(size_t)> findRoot = [&](size_t i) {
    size_t r = i;
    while (parent[r] != r)
      r = parent[r];
    size_t w = i;
    while (parent[w] != r)
    {
      const size_t next = parent[w];
      parent[w] = r;
      w = next;
    }
    return r;
  };

  // A vertex on the face x = 1 is the same point as its twin on x = 0.
  auto quantize = [](float x) {
    const int32_t r = static_cast<int32_t>(std::lround(double(x) * 1048576.0));
    return r == 1048576 ? 0 : r;
  };

  std::unordered_map<uint64_t, size_t> seen;
  seen.reserve(3 * triangles);
  auto key = [&](const float4 &p) {
    // 21 bits per axis: the quantization is 2^20 steps over the unit cell, which fits with the sign.
    const uint64_t x = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.x))) & 0x1FFFFFull;
    const uint64_t y = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.y))) & 0x1FFFFFull;
    const uint64_t z = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.z))) & 0x1FFFFFull;
    return (x << 42) | (y << 21) | z;
  };

  for (size_t t = 0; t < triangles; ++t)
  {
    for (size_t v = 0; v < 3; ++v)
    {
      const uint64_t k = key(triangleData[9 * t + 3 * v]);
      auto it = seen.find(k);
      if (it != seen.end())
      {
        const size_t a = findRoot(t);
        const size_t b = findRoot(it->second);
        if (a != b)
          parent[std::max(a, b)] = std::min(a, b);
      }
      else
      {
        seen[k] = t;
      }
    }
  }

  std::unordered_map<size_t, double> area;
  for (size_t t = 0; t < triangles; ++t)
  {
    double3 corners[3];
    for (size_t v = 0; v < 3; ++v)
    {
      const float4 &p = triangleData[9 * t + 3 * v];
      corners[v] = unitCell * double3(double(p.x), double(p.y), double(p.z));
    }
    area[findRoot(t)] += 0.5 * double3::cross(corners[1] - corners[0], corners[2] - corners[0]).length();
  }

  std::vector<size_t> kept;
  kept.reserve(triangles);
  for (size_t t = 0; t < triangles; ++t)
    if (area[findRoot(t)] >= filamentMinimumArea)
      kept.push_back(t);

  if (kept.empty())
    return {};
  if (kept.size() == triangles)
    return triangleData;

  std::vector<float4> filtered;
  filtered.reserve(kept.size() * 9);
  for (size_t t : kept)
    filtered.insert(filtered.end(), triangleData.begin() + 9 * t, triangleData.begin() + 9 * t + 9);
  return filtered;
}
