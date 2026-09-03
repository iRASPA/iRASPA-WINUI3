/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxpathtracergeometry.h"
#include "directxdevicehelpers.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{
  float3 normalized(const float3 &v)
  {
    const float length = v.length();
    return (length > 0.0f) ? v * (1.0f / length) : float3(0.0f, 0.0f, 0.0f);
  }

  float3 transformPoint(const float4x4 &matrix, const float3 &point)
  {
    const float4 transformed = matrix * float4(point.x, point.y, point.z, 1.0f);
    return float3(transformed.x, transformed.y, transformed.z);
  }

  D3D12_RAYTRACING_AABB makeAabb(const float3 &minimum, const float3 &maximum)
  {
    D3D12_RAYTRACING_AABB box = {};
    box.MinX = minimum.x;
    box.MinY = minimum.y;
    box.MinZ = minimum.z;
    box.MaxX = maximum.x;
    box.MaxY = maximum.y;
    box.MaxZ = maximum.z;
    return box;
  }

  /// Number of sub-cylinders the bond hull of \a type carries, matching Hulls::upload.
  int subCylinderCount(int type)
  {
    switch (type)
    {
    case 1: return 2;   // double
    case 3: return 3;   // triple
    default: return 1;  // single and partial double
    }
  }

  /// In-plane displacement of a sub-cylinder, in the model x/z of the bond, and the thickness
  /// of the thinner cylinders a multiple bond is drawn with. Mirrors the displacement lists
  /// DirectXBondImposter::Hulls::upload builds its hull meshes from.
  float2 subCylinderOffset(int type, int sub, float &radiusFactor)
  {
    radiusFactor = 1.0f;
    if (type == 1)
    {
      radiusFactor = 0.8f;
      return float2((sub == 0) ? -1.0f : 1.0f, 0.0f);
    }
    if (type == 3)
    {
      radiusFactor = 0.8f;
      const float dz = 0.5f * std::sqrt(3.0f);
      if (sub == 0) return float2(-1.0f, -dz);
      if (sub == 1) return float2(1.0f, -dz);
      return float2(0.0f, dz);
    }
    return float2(0.0f, 0.0f);
  }

  /// A selection scaling as the shaders receive it. RKStructureUniforms never passes one
  /// through unenlarged, so neither does the packing here: a shell exactly on the surface it
  /// marks would be at the very limit the selection ray stops at, and would be met or missed
  /// by rounding alone.
  float selectionScaling(double scaling)
  {
    return float((std::max)(1.001, scaling));
  }

  /// The draw ranges of the selected residues and segments of a ribbon, hidden ones left out.
  /// The same set DirectXRibbonSelectionShader draws its overlay over.
  std::vector<RKRibbonChainDrawRange> selectedRibbonDrawRanges(const RKRenderRibbonSource &ribbon)
  {
    std::vector<RKRibbonChainDrawRange> ranges;

    const std::vector<RKRibbonChainDrawRange> segments = ribbon.ribbonSegmentDrawRanges();
    for (int index : ribbon.renderSelectedRibbonSegmentDrawRangeIndices())
    {
      if (index < 0 || index >= static_cast<int>(segments.size()))
        continue;
      if (ribbon.ribbonUsesSegmentVisibility() && !ribbon.isRibbonSegmentDrawRangeVisible(index))
        continue;
      ranges.push_back(segments[index]);
    }

    const std::vector<RKRibbonChainDrawRange> residues = ribbon.ribbonResidueDrawRanges();
    for (int index : ribbon.renderSelectedRibbonResidueDrawRangeIndices())
    {
      if (index < 0 || index >= static_cast<int>(residues.size()))
        continue;
      if (ribbon.ribbonUsesResidueVisibility() && !ribbon.isRibbonResidueDrawRangeVisible(index))
        continue;
      ranges.push_back(residues[index]);
    }

    return ranges;
  }

  /// An upload-heap buffer holding \a values, or one zeroed element when there are none, so
  /// that every shader binding is valid even for a scene without ribbons or without bonds.
  template <typename T>
  ComPtr<ID3D12Resource> makeBuffer(ID3D12Device *device, const std::vector<T> &values)
  {
    const size_t count = (std::max)(values.size(), size_t(1));
    ComPtr<ID3D12Resource> buffer = DirectXDeviceHelpers::createUploadBuffer(device, count * sizeof(T));
    if (!buffer)
      return nullptr;
    if (values.empty())
    {
      const T zero = T();
      DirectXDeviceHelpers::writeUploadBuffer(buffer.Get(), &zero, sizeof(T));
    }
    else
    {
      DirectXDeviceHelpers::writeUploadBuffer(buffer.Get(), values.data(), values.size() * sizeof(T));
    }
    return buffer;
  }

  /// Acceleration structures and their scratch live in device memory and are written by the
  /// build, so unlike every other buffer of the renderkit they need the unordered-access flag.
  ComPtr<ID3D12Resource> createUnorderedAccessBuffer(ID3D12Device *device, UINT64 sizeInBytes,
                                                     D3D12_RESOURCE_STATES initialState)
  {
    ComPtr<ID3D12Resource> buffer;
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (std::max)(sizeInBytes, UINT64(1));
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                                               initialState, nullptr, IID_PPV_ARGS(&buffer))))
    {
      return nullptr;
    }
    return buffer;
  }

  D3D12_RESOURCE_BARRIER unorderedAccessBarrier(ID3D12Resource *resource)
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
  }

}

DirectXPathTracerGeometry::~DirectXPathTracerGeometry()
{
  release();
}

void DirectXPathTracerGeometry::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  m_renderStructures = std::move(structures);
  invalidate();
}

void DirectXPathTracerGeometry::invalidate()
{
  m_valid = false;
  m_bottomLevel.clear();
  m_topLevel.Reset();
}

void DirectXPathTracerGeometry::release()
{
  invalidate();
  m_sphereBuffer.Reset();
  m_cylinderBuffer.Reset();
  m_instanceDataBuffer.Reset();
  m_ribbonVertexBuffer.Reset();
  m_ribbonIndexBuffer.Reset();
  m_structureUniformBuffer.Reset();
  m_structureUniformCount = 0;
  m_sphereBoxBuffer.Reset();
  m_cylinderBoxBuffer.Reset();
  m_ribbonPositionBuffer.Reset();
  m_sphereCount = 0;
  m_cylinderCount = 0;
  m_triangleCount = 0;
  m_ribbonVertexCount = 0;
  m_instanceCount = 0;
  m_sceneRadius = 1.0f;
  m_status = "the geometry has not been packed";
}

bool DirectXPathTracerGeometry::fail(const std::string &reason)
{
  m_status = reason;
  std::fprintf(stderr, "DirectXPathTracerGeometry: %s\n", reason.c_str());
  invalidate();
  return false;
}

bool DirectXPathTracerGeometry::build(Dx12DeviceContext &context)
{
  // Appearance is not geometry: a change of colour, of edge cueing or of any other per-structure
  // setting moves nothing and so invalidates nothing, which would leave the packed copy holding
  // whatever it was built with. Refreshed in place instead, which is a write of a few structures.
  if (isValid())
    return updateStructureUniforms(context.device());

  ID3D12Device *device = context.device();
  if (!device)
    return fail("there is no device to build the acceleration structures on");

  invalidate();

  const PackedScene scene = packGeometry();
  m_sphereCount = static_cast<UINT>(scene.spheres.size());
  m_cylinderCount = static_cast<UINT>(scene.cylinders.size());
  m_triangleCount = static_cast<UINT>(scene.ribbonIndices.size() / 3);
  m_ribbonVertexCount = static_cast<UINT>(scene.ribbonVertices.size());
  m_instanceCount = static_cast<UINT>(scene.instances.size());
  m_sceneRadius = scene.sceneRadius;
  m_worldMinimum = scene.worldMinimum;
  m_worldMaximum = scene.worldMaximum;

  if (scene.pending.empty())
    return fail("the scene holds no traceable geometry: " + describeStructures());

  if (!uploadPackedScene(device, scene))
    return fail("the packed geometry could not be uploaded");

  if (!updateStructureUniforms(device))
    return fail("the structure uniforms could not be uploaded");

  if (!buildAccelerationStructures(context, scene))
    return false;

  m_valid = true;

  // The world-space box is worth as much as the counts: geometry that is packed but placed where no
  // camera ray goes traces exactly like geometry that was never packed at all.
  char bounds[128] = {};
  std::snprintf(bounds, sizeof(bounds), "(%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f)",
                double(m_worldMinimum.x), double(m_worldMinimum.y), double(m_worldMinimum.z),
                double(m_worldMaximum.x), double(m_worldMaximum.y), double(m_worldMaximum.z));

  m_status = std::to_string(m_sphereCount) + " atoms, " + std::to_string(m_cylinderCount)
             + " bond cylinders, " + std::to_string(m_triangleCount) + " ribbon triangles in "
             + std::to_string(m_instanceCount) + " acceleration structures, spanning " + bounds;
  return true;
}

std::string DirectXPathTracerGeometry::describeStructures() const
{
  if (m_renderStructures.empty())
    return "no structures were handed to the renderer";

  std::string description;
  size_t count = 0;
  for (const std::vector<std::shared_ptr<RKRenderObject>> &scene : m_renderStructures)
  {
    for (const std::shared_ptr<RKRenderObject> &structure : scene)
    {
      ++count;
      if (!description.empty())
        description += "; ";
      if (!structure)
      {
        description += "empty slot";
        continue;
      }

      description += "visible=" + std::to_string(structure->isVisible() ? 1 : 0);

      if (auto *atoms = dynamic_cast<RKRenderAtomSource *>(structure.get()))
      {
        const std::vector<RKInPerInstanceAttributesAtoms> &instances = atoms->renderAtoms();
        description += " atoms=" + std::to_string(instances.size())
                       + " drawn=" + std::to_string(atoms->drawAtoms() ? 1 : 0);

        // Which of the two filters in appendSpheres turned them all away, the scale factor being
        // the one thing the raster passes apply the same way and so the one worth printing.
        const float radiusScale = static_cast<float>(atoms->atomScaleFactor());
        size_t hidden = 0;
        size_t tooSmall = 0;
        for (const RKInPerInstanceAttributesAtoms &atom : instances)
        {
          if (atom.position.w < 0.0f)
            ++hidden;
          else if (radiusScale * atom.scale.z <= 0.0f)
            ++tooSmall;
        }
        description += " scaleFactor=" + std::to_string(radiusScale) + " hidden="
                       + std::to_string(hidden) + " zeroRadius=" + std::to_string(tooSmall);
      }
      else
        description += " no atoms";

      if (auto *bonds = dynamic_cast<RKRenderBondSource *>(structure.get()))
        description += " bonds=" + std::to_string(bonds->renderInternalBonds().size())
                       + " drawn=" + std::to_string(bonds->drawBonds() ? 1 : 0);

      if (auto *ribbon = dynamic_cast<RKRenderRibbonSource *>(structure.get()))
        description += " ribbon=" + std::to_string(ribbon->ribbonNumberOfIndices())
                       + " drawn=" + std::to_string(ribbon->drawRibbon() ? 1 : 0);
    }
  }
  return std::to_string(count) + " structures: " + description;
}

DirectXPathTracerGeometry::PackedScene DirectXPathTracerGeometry::packGeometry() const
{
  PackedScene scene;

  float3 minimum(FLT_MAX, FLT_MAX, FLT_MAX);
  float3 maximum(-FLT_MAX, -FLT_MAX, -FLT_MAX);

  // The structure being packed. Held here rather than passed, because the three helpers below
  // are otherwise identical for the model and for a selection shell.
  float4x4 modelMatrix = float4x4();
  size_t structureIndex = 0;

  // Grows the world-space bounds, used only to scale the secondary-ray offset.
  auto expand = [&](const float3 &center, float radius)
  {
    const float3 world = transformPoint(modelMatrix, center);
    const float3 padding(radius, radius, radius);
    minimum = float3::min(minimum, world - padding);
    maximum = float3::max(maximum, world + padding);
  };

  auto appendInstance = [&](RKPathTracer::Kind kind, size_t primitiveBase, size_t primitiveCount,
                            size_t primitiveOffset, bool opaque, RKPathTracer::Selection selection,
                            bool clipAtUnitCell)
  {
    PendingInstance pending;
    pending.kind = kind;
    pending.primitiveCount = static_cast<UINT>(primitiveCount);
    pending.primitiveOffset = static_cast<UINT>(primitiveOffset);
    pending.opaque = opaque;
    pending.transform = modelMatrix;
    pending.mask = RKPathTracer::instanceMask(selection);
    scene.pending.push_back(pending);

    RKPathTracerInstance instance;
    instance.kind = static_cast<uint32_t>(kind);
    instance.primitiveBase = static_cast<uint32_t>(primitiveBase);
    instance.structureIndex = static_cast<uint32_t>(structureIndex);
    instance.clipAtUnitCell = clipAtUnitCell ? 1u : 0u;
    instance.selectionStyle = static_cast<uint32_t>(selection);
    scene.instances.push_back(instance);
  };

  // Packs `atoms` as spheres of `radiusScale` times their own scale and records the instance
  // that places them. Does nothing when they all turn out to be invisible.
  auto appendSpheres = [&](const std::vector<RKInPerInstanceAttributesAtoms> &atoms, float radiusScale,
                           RKPathTracer::Selection selection, bool clipAtUnitCell)
  {
    const size_t sphereBase = scene.spheres.size();
    const size_t boxBase = scene.sphereBoxes.size();

    for (const RKInPerInstanceAttributesAtoms &atom : atoms)
    {
      // invisible atoms are marked with a negative w, as in the imposter shaders
      if (atom.position.w < 0.0f)
        continue;
      const float radius = radiusScale * atom.scale.z;
      if (radius <= 0.0f)
        continue;

      const float3 center(atom.position.x, atom.position.y, atom.position.z);
      const float3 padding(radius, radius, radius);

      RKPathTracerSphere sphere;
      sphere.center = float4(center, radius);
      sphere.ambient = atom.ambient;
      sphere.diffuse = atom.diffuse;
      sphere.specular = atom.specular;
      scene.spheres.push_back(sphere);

      scene.sphereBoxes.push_back(makeAabb(center - padding, center + padding));
      expand(center, radius);
    }

    if (scene.sphereBoxes.size() == boxBase)
      return;

    appendInstance(RKPathTracer::Kind::sphere, sphereBase, scene.sphereBoxes.size() - boxBase, boxBase,
                   true, selection, clipAtUnitCell);
  };

  // Packs the given bonds as capped cylinders, expanding double and triple bonds into their
  // sub-cylinders, and records the instance that places them. `radiusScale` multiplies the
  // radius the model itself uses, which is how a selection shell comes to enclose its bond.
  auto appendCylinders = [&](const std::vector<RKInPerInstanceAttributesBonds> &internalBonds,
                             const std::vector<RKInPerInstanceAttributesBonds> &externalBonds,
                             float bondScaling, bool isUnity, float radiusScale,
                             RKPathTracer::Selection selection, bool clipAtUnitCell)
  {
    const size_t cylinderBase = scene.cylinders.size();
    const size_t boxBase = scene.cylinderBoxes.size();

    auto appendBond = [&](const RKInPerInstanceAttributesBonds &bond)
    {
      if (bond.position1.w < 0.0f || bond.position2.w < 0.0f)
        return;

      // Unity draws every bond with the single hull, whatever its order says.
      const int type = isUnity ? 0 : static_cast<int>(bond.type);
      const int subCylinders = subCylinderCount(type);

      const float3 position1(bond.position1.x, bond.position1.y, bond.position1.z);
      const float3 position2(bond.position2.x, bond.position2.y, bond.position2.z);

      // bondImposterHull builds its basis from pos1 - pos2 and maps the model x-axis to -v1
      // and the model z-axis to -v2, for internal and external bonds alike.
      const float3 separation = position1 - position2;
      const float bondLength = separation.length();
      if (bondLength <= 0.0f)
        return;
      const float3 direction = separation * (1.0f / bondLength);

      const float3 v1 = normalized((std::abs(direction.x) > std::abs(direction.z))
                                       ? float3(-direction.y, direction.x, 0.0f)
                                       : float3(0.0f, -direction.z, direction.y));
      const float3 v2 = normalized(float3::cross(direction, v1));
      const float3 axisX = -v1;
      const float3 axisZ = -v2;

      for (int sub = 0; sub < subCylinders; ++sub)
      {
        float radiusFactor = 1.0f;
        const float2 offset = subCylinderOffset(type, sub, radiusFactor);
        const float3 displacement = (axisX * offset.x + axisZ * offset.y) * bondScaling;
        const float radius = bondScaling * radiusFactor * radiusScale;
        if (radius <= 0.0f)
          continue;

        const float3 pointA = position1 + displacement;
        const float3 pointB = position2 + displacement;

        RKPathTracerCylinder cylinder;
        cylinder.pointA = float4(pointA, radius);
        cylinder.pointB = float4(pointB, 0.0f);
        cylinder.color1 = bond.color1;
        cylinder.color2 = bond.color2;
        cylinder.axisX = float4(axisX, 0.0f);
        cylinder.axisZ = float4(axisZ, 0.0f);
        scene.cylinders.push_back(cylinder);

        const float3 padding(radius, radius, radius);
        scene.cylinderBoxes.push_back(makeAabb(float3::min(pointA, pointB) - padding,
                                               float3::max(pointA, pointB) + padding));
        expand(pointA, radius);
        expand(pointB, radius);
      }
    };

    for (const RKInPerInstanceAttributesBonds &bond : internalBonds)
      appendBond(bond);
    for (const RKInPerInstanceAttributesBonds &bond : externalBonds)
      appendBond(bond);

    if (scene.cylinderBoxes.size() == boxBase)
      return;

    appendInstance(RKPathTracer::Kind::cylinder, cylinderBase, scene.cylinderBoxes.size() - boxBase,
                   boxBase, true, selection, clipAtUnitCell);
  };

  // Packs the triangles of the given ribbon draw `ranges`, along with the mesh vertices they
  // index pushed `expansion` along their own normals, and records the instance that places
  // them. The displacement is nothing for a ribbon itself and is what stands a selection shell
  // off it otherwise, which is how ribbonSelectionExpandedPosition builds the raster overlay: a
  // ribbon is a surface, so a shell over it cannot be had by scaling about a centre as for an
  // atom. Does nothing when every range turns out to be empty.
  auto appendRibbon = [&](const RKRenderRibbonSource &ribbon,
                          const std::vector<RKRibbonChainDrawRange> &ranges, float expansion,
                          RKPathTracer::Selection selection)
  {
    const std::vector<RKRibbonVertex> &sourceVertices = ribbon.renderRibbonVertices();
    const std::vector<uint32_t> &sourceIndices = ribbon.renderRibbonIndices();
    if (sourceVertices.empty())
      return;

    const uint32_t vertexBase = static_cast<uint32_t>(scene.ribbonVertices.size());
    const size_t triangleBase = scene.ribbonIndices.size() / 3;

    for (const RKRibbonChainDrawRange &range : ranges)
    {
      if (range.indexCount <= 0)
        continue;
      const size_t start = static_cast<size_t>(range.indexStart);
      const size_t end = (std::min)(start + static_cast<size_t>(range.indexCount), sourceIndices.size());
      if (start >= end)
        continue;
      for (size_t index = start; index < end; ++index)
        scene.ribbonIndices.push_back(sourceIndices[index] + vertexBase);
    }

    if (scene.ribbonIndices.size() / 3 <= triangleBase)
    {
      // nothing was selected, or every range was hidden: drop the indices appended above
      scene.ribbonIndices.resize(triangleBase * 3);
      return;
    }

    for (const RKRibbonVertex &vertex : sourceVertices)
    {
      RKRibbonVertex displaced = vertex;
      const float3 normal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
      const float length = normal.length();
      if (expansion != 0.0f && length > 0.0f)
      {
        const float3 position(vertex.position.x, vertex.position.y, vertex.position.z);
        displaced.position = float4(position + normal * (expansion / length), vertex.position.w);
      }
      scene.ribbonVertices.push_back(displaced);

      const float3 point(displaced.position.x, displaced.position.y, displaced.position.z);
      scene.ribbonPositions.push_back(point);
      expand(point, 0.0f);
    }

    // The striped pattern has gaps, so which of the built-in triangle test's hits count is
    // settled by the kernel rather than by the acceleration structure.
    const bool opaque = (selection != RKPathTracer::Selection::striped);
    appendInstance(RKPathTracer::Kind::ribbon, triangleBase,
                   scene.ribbonIndices.size() / 3 - triangleBase, triangleBase, opaque, selection,
                   false);
  };

  for (size_t sceneIdentifier = 0; sceneIdentifier < m_renderStructures.size(); ++sceneIdentifier)
  {
    for (size_t movieIdentifier = 0; movieIdentifier < m_renderStructures[sceneIdentifier].size();
         ++movieIdentifier)
    {
      const std::shared_ptr<RKRenderObject> &structure =
          m_renderStructures[sceneIdentifier][movieIdentifier];
      if (!structure)
      {
        ++structureIndex;
        continue;
      }

      // The very uniforms the raster passes are handed, so that traced geometry lands exactly
      // where rasterized geometry does. The kernels index the same array by structureIndex,
      // which counts every structure whether or not anything was packed for it.
      const RKStructureUniforms uniforms(sceneIdentifier, movieIdentifier, structure);
      modelMatrix = uniforms.modelMatrix;

      const bool isVisible = structure->isVisible();
      auto *bondSource = dynamic_cast<RKRenderBondSource *>(structure.get());
      const bool isUnity = (bondSource != nullptr) && bondSource->isUnity();

      auto *atomSource = dynamic_cast<RKRenderAtomSource *>(structure.get());
      if (atomSource && atomSource->drawAtoms() && isVisible)
      {
        // The imposters take their radius from atomScaleFactor times the per-instance scale
        // alone; which representation is drawn has already been folded into the latter.
        const float radiusScale = static_cast<float>(atomSource->atomScaleFactor());

        // The instances the raster passes draw, which is the only account of the atoms that is
        // asked here: appendSpheres does nothing when there are none.
        appendSpheres(atomSource->renderAtoms(), radiusScale, RKPathTracer::Selection::none,
                      atomSource->clipAtomsAtUnitCell());

        const RKPathTracer::Selection selection =
            RKPathTracer::selectionStyle(atomSource->atomSelectionStyle());
        if (selection != RKPathTracer::Selection::none)
        {
          appendSpheres(atomSource->renderSelectedAtoms(),
                        radiusScale * selectionScaling(atomSource->atomSelectionScaling()), selection,
                        atomSource->clipAtomsAtUnitCell());
        }
      }

      if (bondSource && bondSource->drawBonds() && isVisible)
      {
        // the floor bondImposterHull applies, so a scale of zero does not collapse the bond
        const float bondScaling =
            (std::max)(0.02f, static_cast<float>(bondSource->bondScaleFactor()));

        appendCylinders(bondSource->renderInternalBonds(), bondSource->renderExternalBonds(),
                        bondScaling, isUnity, 1.0f, RKPathTracer::Selection::none,
                        bondSource->clipBondsAtUnitCell());

        const RKPathTracer::Selection selection =
            RKPathTracer::selectionStyle(bondSource->bondSelectionStyle());
        if (selection != RKPathTracer::Selection::none)
        {
          // the 1.01 is the selection imposter's, which lifts the shell clear of its own bond
          appendCylinders(bondSource->renderSelectedInternalBonds(),
                          bondSource->renderSelectedExternalBonds(), bondScaling, isUnity,
                          1.01f * selectionScaling(bondSource->bondSelectionScaling()), selection,
                          bondSource->clipBondsAtUnitCell());
        }
      }

      auto *ribbonSource = dynamic_cast<RKRenderRibbonSource *>(structure.get());
      if (ribbonSource && ribbonSource->drawRibbon() && isVisible
          && ribbonSource->ribbonNumberOfIndices() > 0 && !ribbonSource->renderRibbonVertices().empty())
      {
        // only the visible draw ranges are included, so hidden chains and residues do not show
        // up in the traced image either
        appendRibbon(*ribbonSource, ribbonSource->ribbonDrawRangesForEncoding(), 0.0f,
                     RKPathTracer::Selection::none);

        // The selected residues and segments, marked on a shell standing off the ribbon. Which
        // style that shell wears is decided by the atom setting, there being no ribbon-specific
        // one, exactly as DirectXRibbonSelectionShader decides it.
        if (atomSource)
        {
          const RKPathTracer::Selection selection =
              RKPathTracer::selectionStyle(atomSource->atomSelectionStyle());
          if (selection != RKPathTracer::Selection::none)
          {
            const float expansion = (selectionScaling(atomSource->atomSelectionScaling()) - 1.0f)
                                    * RKPathTracer::ribbonExpansionScale(selection);
            appendRibbon(*ribbonSource, selectedRibbonDrawRanges(*ribbonSource), expansion, selection);
          }
        }
      }

      ++structureIndex;
    }
  }

  if (!scene.pending.empty())
  {
    const float3 extent = maximum - minimum;
    scene.sceneRadius = (std::max)(extent.length(), 1.0f);
    scene.worldMinimum = minimum;
    scene.worldMaximum = maximum;
  }
  return scene;
}

bool DirectXPathTracerGeometry::uploadPackedScene(ID3D12Device *device, const PackedScene &scene)
{
  m_sphereBuffer = makeBuffer(device, scene.spheres);
  m_cylinderBuffer = makeBuffer(device, scene.cylinders);
  m_instanceDataBuffer = makeBuffer(device, scene.instances);
  m_ribbonVertexBuffer = makeBuffer(device, scene.ribbonVertices);
  m_ribbonIndexBuffer = makeBuffer(device, scene.ribbonIndices);
  m_sphereBoxBuffer = makeBuffer(device, scene.sphereBoxes);
  m_cylinderBoxBuffer = makeBuffer(device, scene.cylinderBoxes);
  m_ribbonPositionBuffer = makeBuffer(device, scene.ribbonPositions);

  return m_sphereBuffer && m_cylinderBuffer && m_instanceDataBuffer && m_ribbonVertexBuffer
         && m_ribbonIndexBuffer && m_sphereBoxBuffer && m_cylinderBoxBuffer && m_ribbonPositionBuffer;
}

bool DirectXPathTracerGeometry::updateStructureUniforms(ID3D12Device *device)
{
  if (!device)
    return false;

  // Indexed exactly as packGeometry indexes the structures, so that the structureIndex it wrote into
  // each instance record reaches the same structure here.
  std::vector<RKStructureUniforms> uniforms;
  for (size_t i = 0; i < m_renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < m_renderStructures[i].size(); ++j)
    {
      uniforms.push_back(RKStructureUniforms(i, j, m_renderStructures[i][j]));
    }
  }

  const UINT count = static_cast<UINT>(uniforms.size());
  if (!m_structureUniformBuffer || count != m_structureUniformCount)
  {
    m_structureUniformBuffer = makeBuffer(device, uniforms);
    m_structureUniformCount = count;
    return m_structureUniformBuffer != nullptr;
  }

  DirectXDeviceHelpers::writeUploadBuffer(m_structureUniformBuffer.Get(), uniforms.data(),
                                          uniforms.size() * sizeof(RKStructureUniforms));
  return true;
}

bool DirectXPathTracerGeometry::buildAccelerationStructures(Dx12DeviceContext &context,
                                                            const PackedScene &scene)
{
  ID3D12Device5 *device = context.device5();
  if (!device)
    return fail("the device has no DirectX Raytracing support");

  // A build has to be waited on, so it is recorded on a list of its own rather than on the one
  // drawing the frame.
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList4> commandList;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    return fail("could not create the acceleration-structure command allocator");
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&commandList))))
    return fail("could not create the acceleration-structure command list");

  const D3D12_GPU_VIRTUAL_ADDRESS sphereBoxes = m_sphereBoxBuffer->GetGPUVirtualAddress();
  const D3D12_GPU_VIRTUAL_ADDRESS cylinderBoxes = m_cylinderBoxBuffer->GetGPUVirtualAddress();
  const D3D12_GPU_VIRTUAL_ADDRESS ribbonPositions = m_ribbonPositionBuffer->GetGPUVirtualAddress();
  const D3D12_GPU_VIRTUAL_ADDRESS ribbonIndices = m_ribbonIndexBuffer->GetGPUVirtualAddress();

  // Sized up front: the inputs hold pointers into the geometry array, so it must not move.
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(scene.pending.size());
  std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> inputs(scene.pending.size());
  UINT64 scratchSize = 0;

  m_bottomLevel.reserve(scene.pending.size());
  for (size_t i = 0; i < scene.pending.size(); ++i)
  {
    const PendingInstance &entry = scene.pending[i];
    D3D12_RAYTRACING_GEOMETRY_DESC &geometry = geometries[i];

    if (entry.kind == RKPathTracer::Kind::ribbon)
    {
      geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
      geometry.Flags = entry.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                                    : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
      geometry.Triangles.Transform3x4 = 0;
      geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
      geometry.Triangles.IndexCount = entry.primitiveCount * 3;
      geometry.Triangles.IndexBuffer =
          ribbonIndices + UINT64(entry.primitiveOffset) * 3 * sizeof(uint32_t);
      geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
      // The indices are global across the concatenated vertex buffer, so every instance
      // reaches the whole of it.
      geometry.Triangles.VertexCount = m_ribbonVertexCount;
      geometry.Triangles.VertexBuffer.StartAddress = ribbonPositions;
      geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(float3);
    }
    else
    {
      geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
      // A procedural primitive is intersected by the kernel, which decides for itself which
      // hits count, so it can never be declared opaque.
      geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
      geometry.AABBs.AABBCount = entry.primitiveCount;
      geometry.AABBs.AABBs.StartAddress =
          ((entry.kind == RKPathTracer::Kind::sphere) ? sphereBoxes : cylinderBoxes)
          + UINT64(entry.primitiveOffset) * sizeof(D3D12_RAYTRACING_AABB);
      geometry.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &input = inputs[i];
    input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    // The geometry is rebuilt from scratch whenever it changes rather than refitted, so the
    // build may take whatever time a faster traversal costs.
    input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    input.NumDescs = 1;
    input.pGeometryDescs = &geometry;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&input, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0)
      return fail("the driver sized a bottom-level acceleration structure at zero bytes");

    ComPtr<ID3D12Resource> bottomLevel = createUnorderedAccessBuffer(
        device, prebuild.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    if (!bottomLevel)
      return fail("could not allocate a bottom-level acceleration structure");
    m_bottomLevel.push_back(bottomLevel);
    scratchSize = (std::max)(scratchSize, prebuild.ScratchDataSizeInBytes);
  }

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
  topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  topLevelInputs.NumDescs = static_cast<UINT>(scene.pending.size());

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuild = {};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuild);
  if (topLevelPrebuild.ResultDataMaxSizeInBytes == 0)
    return fail("the driver sized the top-level acceleration structure at zero bytes");

  m_topLevel = createUnorderedAccessBuffer(device, topLevelPrebuild.ResultDataMaxSizeInBytes,
                                           D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
  if (!m_topLevel)
    return fail("could not allocate the top-level acceleration structure");
  scratchSize = (std::max)(scratchSize, topLevelPrebuild.ScratchDataSizeInBytes);

  // One scratch buffer for every build, which keeps the peak allocation to the largest single
  // structure rather than the sum of them all. Reusing it means a barrier between builds, but
  // a build only happens when the geometry changes, so serializing them costs nothing that
  // matters.
  ComPtr<ID3D12Resource> scratch =
      createUnorderedAccessBuffer(device, scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (!scratch)
    return fail("could not allocate the acceleration-structure scratch buffer");

  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescriptors(scene.pending.size());
  for (size_t i = 0; i < scene.pending.size(); ++i)
  {
    const PendingInstance &entry = scene.pending[i];
    D3D12_RAYTRACING_INSTANCE_DESC &descriptor = instanceDescriptors[i];

    // float4x4 stores its columns first; the instance transform is the upper three rows of the
    // same matrix, written row by row.
    const float4x4 &m = entry.transform;
    descriptor.Transform[0][0] = m.m11;
    descriptor.Transform[0][1] = m.m12;
    descriptor.Transform[0][2] = m.m13;
    descriptor.Transform[0][3] = m.m14;
    descriptor.Transform[1][0] = m.m21;
    descriptor.Transform[1][1] = m.m22;
    descriptor.Transform[1][2] = m.m23;
    descriptor.Transform[1][3] = m.m24;
    descriptor.Transform[2][0] = m.m31;
    descriptor.Transform[2][1] = m.m32;
    descriptor.Transform[2][2] = m.m33;
    descriptor.Transform[2][3] = m.m34;

    // How the kernels find the instance record: inline ray tracing has no per-instance shader
    // to carry the geometry kind, so the index into the instance-data buffer travels here.
    descriptor.InstanceID = static_cast<UINT>(i);
    // keeps the selection shells out of every ray but the primary one
    descriptor.InstanceMask = entry.mask;
    descriptor.InstanceContributionToHitGroupIndex = 0;
    descriptor.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    descriptor.AccelerationStructure = m_bottomLevel[i]->GetGPUVirtualAddress();
  }

  ComPtr<ID3D12Resource> instanceDescriptorBuffer = makeBuffer(device, instanceDescriptors);
  if (!instanceDescriptorBuffer)
    return fail("could not allocate the instance descriptors");
  topLevelInputs.InstanceDescs = instanceDescriptorBuffer->GetGPUVirtualAddress();

  const D3D12_GPU_VIRTUAL_ADDRESS scratchAddress = scratch->GetGPUVirtualAddress();
  const D3D12_RESOURCE_BARRIER scratchBarrier = unorderedAccessBarrier(scratch.Get());

  for (size_t i = 0; i < inputs.size(); ++i)
  {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    build.Inputs = inputs[i];
    build.DestAccelerationStructureData = m_bottomLevel[i]->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratchAddress;
    commandList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    if (i + 1 < inputs.size())
      commandList->ResourceBarrier(1, &scratchBarrier);
  }

  // The top-level build reads every bottom-level structure the loop above wrote.
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(m_bottomLevel.size() + 1);
  for (const ComPtr<ID3D12Resource> &bottomLevel : m_bottomLevel)
    barriers.push_back(unorderedAccessBarrier(bottomLevel.Get()));
  barriers.push_back(scratchBarrier);
  commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuild = {};
  topLevelBuild.Inputs = topLevelInputs;
  topLevelBuild.DestAccelerationStructureData = m_topLevel->GetGPUVirtualAddress();
  topLevelBuild.ScratchAccelerationStructureData = scratchAddress;
  commandList->BuildRaytracingAccelerationStructure(&topLevelBuild, 0, nullptr);

  if (FAILED(commandList->Close()))
    return fail("could not close the acceleration-structure command list");

  ID3D12CommandList *lists[] = {commandList.Get()};
  context.commandQueue()->ExecuteCommandLists(1, lists);

  // The kernels have no way to wait on the build themselves, so the frame that triggers it
  // pays for it.
  Dx12DeviceContext::Fence *fence = context.createFence();
  context.waitForGPU(fence);
  delete fence;
  return true;
}
