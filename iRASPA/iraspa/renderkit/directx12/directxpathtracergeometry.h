/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "directxpathtracercommon.h"
#include "dx12devicecontext.h"
#include "rkrenderkitprotocols.h"
#include "rkribbonmesh.h"
#include <memory>
#include <string>
#include <vector>

/// The molecular geometry of the visible structures, packed into the buffers and acceleration
/// structures the ray-tracing kernels read.
///
/// Atoms and bonds become analytic spheres and capped cylinders in procedural-primitive
/// (bounding-box) acceleration structures, so the kernels intersect the same surfaces the raster
/// imposters do rather than a tessellation of them. Ribbons become the indexed triangle mesh of
/// RKRibbonMesh. Geometry stays in structure space and the structure's model matrix becomes the
/// instance transform, which is what lets one packed buffer serve every replica of a structure.
///
/// Whatever is selected is packed a second time, enlarged as its selection style asks, as instances
/// only primary rays can see. That shell is what the striped and Worley-noise patterns are drawn on,
/// and it stands to the model in the same relation as the enlarged imposter the rasterizer draws
/// over a selected atom. Atoms and bonds grow by a factor about their own axis; a ribbon, being a
/// surface with no centre to grow from, is instead pushed out along its normals.
///
/// Every radius, displacement and expansion here is taken from the HLSL of the corresponding raster
/// pass rather than from the Cocoa original, because the traced surfaces are composited against the
/// rasterized ones and have to agree with them to the pixel.
///
/// The structures bake the current atom and bond scale factors, so they are only valid for as long
/// as the scene is unchanged: invalidate() drops them and the next build repacks. Building has to be
/// waited on, so it costs a frame and cannot happen per frame.
class DirectXPathTracerGeometry
{
public:
  DirectXPathTracerGeometry() = default;
  ~DirectXPathTracerGeometry();

  DirectXPathTracerGeometry(const DirectXPathTracerGeometry &) = delete;
  DirectXPathTracerGeometry &operator=(const DirectXPathTracerGeometry &) = delete;

  void setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures);

  /// Drops the acceleration structures so the next build repacks the geometry. Called whenever
  /// the structures, their scale factors or their visibility change.
  void invalidate();

  void release();

  /// Packs the geometry and builds the acceleration structures, or reuses the ones already built.
  /// Blocks until the build has completed, the kernels having no way to wait on it themselves.
  /// False when there is nothing traceable or the build failed, with the reason in status().
  bool build(Dx12DeviceContext &context);

  bool isValid() const { return m_valid && m_topLevel != nullptr; }

  /// The top-level structure the kernels trace against.
  ID3D12Resource *topLevelAccelerationStructure() const { return m_topLevel.Get(); }

  ID3D12Resource *sphereBuffer() const { return m_sphereBuffer.Get(); }
  ID3D12Resource *cylinderBuffer() const { return m_cylinderBuffer.Get(); }
  ID3D12Resource *instanceDataBuffer() const { return m_instanceDataBuffer.Get(); }
  ID3D12Resource *ribbonVertexBuffer() const { return m_ribbonVertexBuffer.Get(); }
  ID3D12Resource *ribbonIndexBuffer() const { return m_ribbonIndexBuffer.Get(); }

  /// The uniforms of every structure, which the kernels index by the structureIndex of the instance
  /// they hit. The raster passes take the same data one structure at a time out of a constant buffer
  /// whose stride is rounded up to the constant-buffer alignment; a structured buffer cannot carry
  /// that padding, so the tracer keeps its own tightly packed copy.
  ID3D12Resource *structureUniformBuffer() const { return m_structureUniformBuffer.Get(); }
  UINT structureUniformCount() const { return m_structureUniformCount; }

  /// Refreshes the structure uniforms in place, for a change of colour, material or clipping that
  /// moves no geometry and so needs no rebuild.
  bool updateStructureUniforms(ID3D12Device *device);

  UINT sphereCount() const { return m_sphereCount; }
  UINT cylinderCount() const { return m_cylinderCount; }
  UINT triangleCount() const { return m_triangleCount; }
  UINT ribbonVertexCount() const { return m_ribbonVertexCount; }
  UINT instanceCount() const { return m_instanceCount; }

  /// Diagonal of the world-space bounds, at least 1. The kernels scale their secondary-ray
  /// offset by it, so that the offset means the same thing in a unit cell and in a protein.
  float sceneRadius() const { return m_sceneRadius; }

  /// The world-space box the built instances occupy.
  float3 worldMinimum() const { return m_worldMinimum; }
  float3 worldMaximum() const { return m_worldMaximum; }

  /// What the last build did, or why it did nothing. The picture export runs where the log
  /// window does not exist, so the caller can hand this back to the application.
  const std::string &status() const { return m_status; }

private:
  /// What the structures on hand offered, for a build that found nothing to trace. "Nothing to
  /// trace" has several causes that look alike from the outside -- no structures, none of them
  /// visible, none of them drawing anything, or nothing in them to draw -- so the failure names
  /// which one it was rather than leaving it to be guessed at.
  std::string describeStructures() const;

  /// One instance of the top-level structure: the geometry to build a bottom-level structure
  /// from, the transform to place it with and the rays it answers.
  struct PendingInstance
  {
    RKPathTracer::Kind kind = RKPathTracer::Kind::sphere;
    /// Bounding boxes for a sphere or cylinder instance, triangles for a ribbon one.
    UINT primitiveCount = 0;
    /// First bounding box in the sphere or cylinder box array, or first triangle in the
    /// concatenated ribbon index buffer.
    UINT primitiveOffset = 0;
    /// Cleared only for the striped ribbon shell, whose pattern has gaps: which of the
    /// built-in triangle test's hits count is then settled by the kernel.
    bool opaque = true;
    float4x4 transform = float4x4();
    UINT mask = RKPathTracer::maskSurface;
  };

  /// Everything one pass over the structures produces, before any of it reaches the GPU.
  struct PackedScene
  {
    std::vector<RKPathTracerSphere> spheres;
    std::vector<RKPathTracerCylinder> cylinders;
    std::vector<RKPathTracerInstance> instances;
    std::vector<D3D12_RAYTRACING_AABB> sphereBoxes;
    std::vector<D3D12_RAYTRACING_AABB> cylinderBoxes;
    /// The full ribbon vertices, for the kernel to interpolate normals and pattern
    /// coordinates from.
    std::vector<RKRibbonVertex> ribbonVertices;
    /// The same vertices' positions alone, which is the form a triangle acceleration
    /// structure takes its vertex buffer in.
    std::vector<float3> ribbonPositions;
    std::vector<uint32_t> ribbonIndices;
    std::vector<PendingInstance> pending;
    float sceneRadius = 1.0f;
    /// The world-space box the instances place the geometry in, which is where a camera ray has
    /// to arrive for anything to be traced.
    float3 worldMinimum = float3();
    float3 worldMaximum = float3();
  };

  PackedScene packGeometry() const;
  bool uploadPackedScene(ID3D12Device *device, const PackedScene &scene);
  bool buildAccelerationStructures(Dx12DeviceContext &context, const PackedScene &scene);
  bool fail(const std::string &reason);

  std::vector<std::vector<std::shared_ptr<RKRenderObject>>> m_renderStructures;

  ComPtr<ID3D12Resource> m_sphereBuffer;
  ComPtr<ID3D12Resource> m_cylinderBuffer;
  ComPtr<ID3D12Resource> m_instanceDataBuffer;
  ComPtr<ID3D12Resource> m_ribbonVertexBuffer;
  ComPtr<ID3D12Resource> m_ribbonIndexBuffer;
  ComPtr<ID3D12Resource> m_structureUniformBuffer;

  /// Build inputs rather than shader inputs: the bounding boxes and the positions the
  /// acceleration structures are built from. Kept alive because a rebuild reuses them.
  ComPtr<ID3D12Resource> m_sphereBoxBuffer;
  ComPtr<ID3D12Resource> m_cylinderBoxBuffer;
  ComPtr<ID3D12Resource> m_ribbonPositionBuffer;

  std::vector<ComPtr<ID3D12Resource>> m_bottomLevel;
  ComPtr<ID3D12Resource> m_topLevel;

  UINT m_sphereCount = 0;
  UINT m_cylinderCount = 0;
  UINT m_triangleCount = 0;
  UINT m_ribbonVertexCount = 0;
  UINT m_instanceCount = 0;
  UINT m_structureUniformCount = 0;
  float m_sceneRadius = 1.0f;
  float3 m_worldMinimum = float3();
  float3 m_worldMaximum = float3();
  bool m_valid = false;
  std::string m_status = "the geometry has not been packed";
};
