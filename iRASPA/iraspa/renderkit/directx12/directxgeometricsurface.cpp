/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxgeometricsurface.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <simulationkit.h>
#include "directxuniformstringliterals.h"
#include "geometry/quadgeometry.h"

void DirectXGeometricSurface::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXGeometricSurface::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializePSOs(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXGeometricSurface::initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCECELLORIGIN", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, cellOrigin)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEFIRSTCLIP", 0, DXGI_FORMAT_R32_UINT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, firstClip)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCECLIPCOUNT", 0, DXGI_FORMAT_R32_UINT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, clipCount)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCECLIPTOCELL", 0, DXGI_FORMAT_R32_UINT, 1,
      static_cast<UINT>(offsetof(RKGeometricSurfacePatchInstance, clipToCell)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  _psoReady = true;
  for (int perspective = 0; perspective < 2; ++perspective)
  {
    const bool orthographic = perspective == 0;
    ComPtr<ID3DBlob> vs = compileShader(vertexShaderSource(orthographic), "VSMain", "vs_5_0");
    if (!vs)
    {
      _psoReady = false;
      return;
    }

    for (int opaque = 0; opaque < 2; ++opaque)
    {
      for (int perSample = 0; perSample < 2; ++perSample)
      {
        // perSample true → exact silhouette discard; false → analytic coverage (Cocoa "fast" path).
        const bool analyticCoverage = perSample == 0;
        ComPtr<ID3DBlob> ps =
            compileShader(pixelShaderSource(orthographic, analyticCoverage), "PSMain", "ps_5_0");
        if (!ps)
        {
          _psoReady = false;
          return;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSignature;
        psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask =
            opaque ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = rtvFormat;
        psoDesc.DSVFormat = dsvFormat;
        psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

        if (!opaque)
        {
          psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
          psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
          psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
          psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
          psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
          psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
          psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        else if (analyticCoverage)
        {
          // Cocoa opaque per-pixel path: alpha-to-coverage turns silhouette coverage into MSAA samples.
          psoDesc.BlendState.AlphaToCoverageEnable = TRUE;
        }

        if (FAILED(device->CreateGraphicsPipelineState(
                &psoDesc, IID_PPV_ARGS(&_psos[perspective][opaque][perSample]))))
        {
          std::cerr << "DirectXGeometricSurface: failed to create PSO\n";
          _psoReady = false;
          return;
        }
      }
    }
  }
}

void DirectXGeometricSurface::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXGeometricSurface::deleteBuffers()
{
  _buffers.clear();
}

void DirectXGeometricSurface::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXGeometricSurface::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  QuadGeometry quad;
  const auto vertices = quad.vertices();
  const auto indices = quad.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _quadMesh,
                                          vertices.data(), vertices.size() * sizeof(RKVertex),
                                          sizeof(RKVertex), indices.data(),
                                          indices.size() * sizeof(short));
  initializeVertexBuffers(device);
}

void DirectXGeometricSurface::initializeVertexBuffers(ID3D12Device *device)
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      PatchBuffers &bufs = _buffers[i][j];
      bufs = PatchBuffers{};

      auto *renderStructure = dynamic_cast<RKRenderObject *>(_renderStructures[i][j].get());
      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      if (!renderStructure || !source || !source->drawAdsorptionSurface()
          || !isGeometricSurface(source->adsorptionSurfaceRenderingMethod()))
        continue;

      const std::vector<double3> positions = source->atomUnitCellPositions();
      const double probeSigma = source->adsorptionSurfaceProbeParameters().y;
      const std::shared_ptr<SKCell> cell = renderStructure->cell();
      if (!cell || positions.empty())
      {
        source->setAdsorptionSurfaceNumberOfTriangles(0);
        continue;
      }

      SKGeometricSurface surface;
      if (source->adsorptionSurfaceRenderingMethod() == RKEnergySurfaceType::vdwGeometricSurface)
      {
        const std::vector<int> elementIdentifiers = source->atomUnitCellElementIdentifiers();
        if (elementIdentifiers.empty())
        {
          source->setAdsorptionSurfaceNumberOfTriangles(0);
          continue;
        }
        surface = SKGeometricSurface::buildVanDerWaals(positions, elementIdentifiers, probeSigma, *cell,
                                                       source->appliedBlockingPockets());
      }
      else
      {
        const std::vector<double2> parameters = source->potentialParameters();
        if (parameters.empty())
        {
          source->setAdsorptionSurfaceNumberOfTriangles(0);
          continue;
        }
        surface = SKGeometricSurface::build(positions, parameters, probeSigma, *cell,
                                            source->appliedBlockingPockets());
      }

      const double3x3 unitCell = cell->unitCell();
      const bool wrapIntoCell = source->isPeriodic();
      std::vector<float4> replicas = cell->renderTranslationVectors();
      if (replicas.empty())
        replicas.push_back(float4(0.0f, 0.0f, 0.0f, 0.0f));

      std::vector<RKGeometricSurfacePatchInstance> instances;
      std::vector<RKGeometricSurfaceClip> clips;
      instances.reserve(surface.patches.size() * replicas.size() * (wrapIntoCell ? 4u : 1u));

      for (const float4 &replica : replicas)
      {
        const double3 translation =
            unitCell * double3(double(replica.x), double(replica.y), double(replica.z));
        for (const SKGeometricSurfacePatch &patch : surface.patches)
        {
          std::vector<SKGeometricSurfacePatchCopy> copies;
          if (wrapIntoCell)
            copies = patch.copiesInsideUnitCell(*cell);
          else
            copies.push_back(SKGeometricSurfacePatchCopy(patch.center, patch.clips, double3()));

          for (const SKGeometricSurfacePatchCopy &copy : copies)
          {
            std::vector<SKGeometricSurfaceClip> gpuClips = copy.clips;
            if (gpuClips.size() > 64)
            {
              std::partial_sort(
                  gpuClips.begin(), gpuClips.begin() + 64, gpuClips.end(),
                  [&](const SKGeometricSurfaceClip &a, const SKGeometricSurfaceClip &b) {
                    return (a.center - copy.center).length() < (b.center - copy.center).length();
                  });
              gpuClips.resize(64);
            }

            const uint32_t first = static_cast<uint32_t>(clips.size());
            for (const SKGeometricSurfaceClip &clip : gpuClips)
            {
              const double3 clipCenter = clip.center + translation;
              clips.emplace_back(float3(float(clipCenter.x), float(clipCenter.y), float(clipCenter.z)),
                                 float(clip.radius));
            }
            const double3 center = copy.center + translation;
            const double3 cellOrigin = copy.cellOrigin + translation;
            instances.emplace_back(float3(float(center.x), float(center.y), float(center.z)),
                                   float(patch.radius),
                                   float3(float(cellOrigin.x), float(cellOrigin.y), float(cellOrigin.z)),
                                   first, static_cast<uint32_t>(gpuClips.size()), wrapIntoCell);
          }
        }
      }

      source->setAdsorptionSurfaceNumberOfTriangles(static_cast<int64_t>(instances.size()));

      if (instances.empty())
        continue;

      DirectXDeviceHelpers::uploadInstanceBuffer(
          device, bufs.instanceBuffer, bufs.instanceVbv, bufs.instanceCount, instances.data(),
          instances.size(), sizeof(RKGeometricSurfacePatchInstance));

      if (clips.empty())
      {
        RKGeometricSurfaceClip dummy;
        bufs.clipBuffer =
            DirectXDeviceHelpers::createUploadBuffer(device, sizeof(RKGeometricSurfaceClip));
        DirectXDeviceHelpers::writeUploadBuffer(bufs.clipBuffer.Get(), &dummy,
                                                sizeof(RKGeometricSurfaceClip));
      }
      else
      {
        const size_t clipBytes = clips.size() * sizeof(RKGeometricSurfaceClip);
        bufs.clipBuffer = DirectXDeviceHelpers::createUploadBuffer(device, clipBytes);
        DirectXDeviceHelpers::writeUploadBuffer(bufs.clipBuffer.Get(), clips.data(), clipBytes);
      }
    }
  }
}

void DirectXGeometricSurface::paintOpaque(ID3D12GraphicsCommandList *commandList,
                                          D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                          UINT structureCBVStride,
                                          D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                                          UINT isosurfaceCBVStride,
                                          std::shared_ptr<RKCamera> camera)
{
  paint(commandList, true, structureCBVBase, structureCBVStride, isosurfaceCBVBase,
        isosurfaceCBVStride, camera, nullptr, nullptr, nullptr);
}

void DirectXGeometricSurface::paintTransparent(ID3D12GraphicsCommandList *commandList,
                                               D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                               UINT structureCBVStride,
                                               D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                                               UINT isosurfaceCBVStride,
                                               std::shared_ptr<RKCamera> camera, size_t sceneIndex,
                                               size_t movieIndex, size_t structureIndex)
{
  paint(commandList, false, structureCBVBase, structureCBVStride, isosurfaceCBVBase,
        isosurfaceCBVStride, camera, &sceneIndex, &movieIndex, &structureIndex);
}

void DirectXGeometricSurface::paint(ID3D12GraphicsCommandList *commandList, bool opaque,
                                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                    UINT structureCBVStride,
                                    D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                                    UINT isosurfaceCBVStride, std::shared_ptr<RKCamera> camera,
                                    size_t *sceneIndex, size_t *movieIndex, size_t *structureIndex)
{
  if (!_psoReady || _quadMesh.indexCount == 0)
    return;

  const bool orthographic = !camera || camera->isOrthographic();
  const bool perSample = DirectXDeviceHelpers::perSampleImposterShading();
  ID3D12PipelineState *pso =
      _psos[orthographic ? 0 : 1][opaque ? 1 : 0][perSample ? 1 : 0].Get();
  if (!pso)
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      const bool drawThis =
          !(sceneIndex && movieIndex) || (i == *sceneIndex && j == *movieIndex);

      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      const PatchBuffers &bufs = _buffers[i][j];
      if (drawThis && source && _renderStructures[i][j]->isVisible()
          && source->drawAdsorptionSurface()
          && isGeometricSurface(source->adsorptionSurfaceRenderingMethod())
          && bufs.instanceCount > 0 && bufs.instanceBuffer && bufs.clipBuffer)
      {
        const bool isOpaque = source->adsorptionSurfaceOpacity() > 0.99999;
        if (opaque == isOpaque)
        {
          const size_t structureOffset = structureIndex ? *structureIndex : index;
          commandList->SetGraphicsRootConstantBufferView(
              1, structureCBVBase
                     + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureOffset) * structureCBVStride);
          commandList->SetGraphicsRootConstantBufferView(
              4, isosurfaceCBVBase
                     + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureOffset) * isosurfaceCBVStride);
          commandList->SetGraphicsRootShaderResourceView(
              DirectXDeviceHelpers::kGeometricSurfaceClipRootParameter,
              bufs.clipBuffer->GetGPUVirtualAddress());

          D3D12_VERTEX_BUFFER_VIEW views[2] = { _quadMesh.vbv, bufs.instanceVbv };
          commandList->IASetVertexBuffers(0, 2, views);
          commandList->IASetIndexBuffer(&_quadMesh.ibv);
          commandList->DrawIndexedInstanced(_quadMesh.indexCount, bufs.instanceCount, 0, 0, 0);
        }
      }
      ++index;
    }
  }
}

std::string DirectXGeometricSurface::vertexShaderSource(bool orthographic)
{
  const char *billboardScale = orthographic ? "" : "1.5 * ";
  return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral
       + DirectXUniformStringLiterals::StructureUniformBlockStringLiteral
       + std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
  float4 cellOrigin : INSTANCECELLORIGIN;
  uint firstClip : INSTANCEFIRSTCLIP;
  uint clipCount : INSTANCECLIPCOUNT;
  uint clipToCell : INSTANCECLIPTOCELL;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  nointerpolation float4 instancePosition : TEXCOORD1;
  float2 texcoords : TEXCOORD2;
  float3 frag_pos : TEXCOORD3;
  nointerpolation float3 frag_center : TEXCOORD4;
  float3 V : TEXCOORD5;
  nointerpolation float4 sphere_radius : TEXCOORD6;
  nointerpolation uint firstClip : TEXCOORD7;
  nointerpolation uint clipCount : TEXCOORD8;
  nointerpolation uint clipToCell : TEXCOORD9;
  nointerpolation float3 cellOrigin : TEXCOORD10;
  nointerpolation float4 modelFromView1 : TEXCOORD11;
  nointerpolation float4 modelFromView2 : TEXCOORD12;
  nointerpolation float4 modelFromView3 : TEXCOORD13;
  nointerpolation float4 modelFromView4 : TEXCOORD14;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 scale = input.instanceScale;
  output.instancePosition = input.instancePosition;
  output.firstClip = input.firstClip;
  output.clipCount = input.clipCount;
  output.clipToCell = input.clipToCell;
  output.cellOrigin = input.cellOrigin.xyz;
  output.sphere_radius = scale;

  float4x4 modelFromView = transpose(mul(frameUniforms.normalMatrix, structureUniforms.modelMatrix));
  output.modelFromView1 = modelFromView[0];
  output.modelFromView2 = modelFromView[1];
  output.modelFromView3 = modelFromView[2];
  output.modelFromView4 = modelFromView[3];

  output.eye_position = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, input.instancePosition));
  output.V = -output.eye_position.xyz;
  output.frag_center = output.eye_position.xyz;
  output.texcoords = input.vertexPosition.xy;

  float4 pos2 = output.eye_position;
  pos2.xy += )foo")
       + billboardScale
       + std::string(R"foo(scale.xy * input.vertexPosition.xy;
  output.frag_pos = pos2.xyz;

  float4 clip = mul(frameUniforms.projectionMatrix, pos2);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");
}

std::string DirectXGeometricSurface::pixelShaderSource(bool orthographic, bool analyticCoverage)
{
  const char *sampleInterp = analyticCoverage ? "" : "sample ";

  std::string common =
      DirectXUniformStringLiterals::FrameUniformBlockStringLiteral
    + DirectXUniformStringLiterals::StructureUniformBlockStringLiteral
    + DirectXUniformStringLiterals::IsosurfaceUniformBlockStringLiteral
    + DirectXUniformStringLiterals::LightUniformBlockStringLiteral
    + DirectXUniformStringLiterals::LightingStringLiteral
    + DirectXUniformStringLiterals::RGBHSVStringLiteral
    + R"foo(
struct GeometricSurfaceClip
{
  float4 sphere;
};
StructuredBuffer<GeometricSurfaceClip> geometricSurfaceClips : register(t2);

float coverageFromEdge(float edge)
{
  float width = fwidth(edge);
  return width > 0.0 ? saturate(0.5 + edge / width) : (edge > 0.0 ? 1.0 : 0.0);
}

bool geometricSurfaceHitIsInsideCell(float3 hitModel, float3 cellOrigin)
{
  float3 frac = mul(isosurfaceUniforms.inverseUnitCellMatrix,
                    float4(hitModel - cellOrigin, 1.0)).xyz;
  return frac.x >= 0.0 && frac.x < 1.0 &&
         frac.y >= 0.0 && frac.y < 1.0 &&
         frac.z >= 0.0 && frac.z < 1.0;
}

bool geometricSurfaceHitIsExposed(float3 hitModel, uint firstClip, uint clipCount)
{
  uint count = min(clipCount, 64u);
  for (uint i = 0; i < count; ++i)
  {
    float4 sphere = geometricSurfaceClips[firstClip + i].sphere;
    float3 delta = hitModel - sphere.xyz;
    float radius = sphere.w;
    if (dot(delta, delta) < radius * radius)
      return false;
  }
  return true;
}

bool geometricSurfaceHitIsValid(float3 hitModel, uint clipToCell, float3 cellOrigin,
                                uint firstClip, uint clipCount)
{
  if (clipToCell != 0u && !geometricSurfaceHitIsInsideCell(hitModel, cellOrigin))
    return false;
  return geometricSurfaceHitIsExposed(hitModel, firstClip, clipCount);
}

float4 shadeGeometricSurface(float3 N, float3 V, float4 surfaceEyePosition, bool frontfacing)
{
  float3 normal = frontfacing ? N : -N;
  LightingWeights lighting = accumulateLighting(normal, V, surfaceEyePosition,
      frontfacing ? isosurfaceUniforms.shininessFrontSide : isosurfaceUniforms.shininessBackSide);

  float4 ambient = float4(lighting.ambient, 1.0)
                 * (frontfacing ? isosurfaceUniforms.ambientFrontSide : isosurfaceUniforms.ambientBackSide);
  float4 diffuse = float4(lighting.diffuse, 1.0)
                 * (frontfacing ? isosurfaceUniforms.diffuseFrontSide : isosurfaceUniforms.diffuseBackSide);
  float4 specular = float4(lighting.specular, 1.0)
                  * (frontfacing ? isosurfaceUniforms.specularFrontSide : isosurfaceUniforms.specularBackSide);

  float4 color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
  bool hdr = frontfacing ? isosurfaceUniforms.frontHDR : isosurfaceUniforms.backHDR;
  float exposure = frontfacing ? isosurfaceUniforms.frontHDRExposure : isosurfaceUniforms.backHDRExposure;
  if (hdr)
  {
    color = 1.0 - exp2(-color * exposure);
    color.a = 1.0;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * isosurfaceUniforms.hue;
  hsv.y = hsv.y * isosurfaceUniforms.saturation;
  hsv.z = hsv.z * isosurfaceUniforms.value;
  float opacity = isosurfaceUniforms.diffuseFrontSide.w;
  return float4(hsv2rgb(hsv) * opacity, opacity);
}

struct PSInput
{
  float4 position : SV_POSITION;
  float4 eye_position : TEXCOORD0;
  nointerpolation float4 instancePosition : TEXCOORD1;
)foo";

  common += sampleInterp;
  common += R"foo(float2 texcoords : TEXCOORD2;
)foo";
  common += sampleInterp;
  common += R"foo(float3 frag_pos : TEXCOORD3;
  nointerpolation float3 frag_center : TEXCOORD4;
  float3 V : TEXCOORD5;
  nointerpolation float4 sphere_radius : TEXCOORD6;
  nointerpolation uint firstClip : TEXCOORD7;
  nointerpolation uint clipCount : TEXCOORD8;
  nointerpolation uint clipToCell : TEXCOORD9;
  nointerpolation float3 cellOrigin : TEXCOORD10;
  nointerpolation float4 modelFromView1 : TEXCOORD11;
  nointerpolation float4 modelFromView2 : TEXCOORD12;
  nointerpolation float4 modelFromView3 : TEXCOORD13;
  nointerpolation float4 modelFromView4 : TEXCOORD14;
};

struct PSOutput
{
  float4 color : SV_TARGET;
  float depth : SV_Depth;
};

PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo";

  if (orthographic)
  {
    common += R"foo(
  float x = input.texcoords.x;
  float y = input.texcoords.y;
  float zz = 1.0 - x * x - y * y;
  float coverage = 1.0;
)foo";
    if (analyticCoverage)
      common += R"foo(
  coverage = coverageFromEdge(zz);
  zz = max(zz, 0.0);
)foo";
    else
      common += R"foo(
  if (zz <= 0.0)
    discard;
)foo";
    common += R"foo(
  float z = sqrt(zz);

  float4x4 modelFromView = float4x4(input.modelFromView1, input.modelFromView2,
                                    input.modelFromView3, input.modelFromView4);
  float3 N = float3(x, y, z);
  float3 hitModel = input.instancePosition.xyz
                  + mul(modelFromView, (input.sphere_radius * float4(N, 1.0))).xyz;
  float4 pos = input.eye_position;
  bool frontfacing = true;
  if (geometricSurfaceHitIsValid(hitModel, input.clipToCell, input.cellOrigin,
                                 input.firstClip, input.clipCount))
  {
    pos.z += input.sphere_radius.z * z;
  }
  else
  {
    N = float3(x, y, -z);
    hitModel = input.instancePosition.xyz
             + mul(modelFromView, (input.sphere_radius * float4(N, 1.0))).xyz;
    if (!geometricSurfaceHitIsValid(hitModel, input.clipToCell, input.cellOrigin,
                                    input.firstClip, input.clipCount))
      discard;
    pos.z -= input.sphere_radius.z * z;
    frontfacing = false;
  }

  float4 projected = mul(frameUniforms.projectionMatrix, pos);
  output.depth = 0.5 * (projected.z / projected.w) + 0.5;

  float3 V = normalize(input.V);
  output.color = shadeGeometricSurface(N, V, pos, frontfacing);
)foo";
  }
  else
  {
    common += R"foo(
  float3 rij = -input.frag_center;
  float3 vij = input.frag_pos;
  float A = dot(vij, vij);
  float B = 2.0 * dot(rij, vij);
  float C = dot(rij, rij) - input.sphere_radius.z * input.sphere_radius.z;
  float argument = B * B - 4.0 * A * C;
  float coverage = 1.0;
)foo";
    if (analyticCoverage)
      common += R"foo(
  coverage = coverageFromEdge(argument);
  argument = max(argument, 0.0);
)foo";
    else
      common += R"foo(
  if (argument < 0.0)
    discard;
)foo";
    common += R"foo(
  float disc = sqrt(argument);
  float tNear = 0.5 * (-B - disc) / A;
  float tFar = 0.5 * (-B + disc) / A;

  float4x4 modelFromView = float4x4(input.modelFromView1, input.modelFromView2,
                                    input.modelFromView3, input.modelFromView4);

  float t = tNear;
  float3 hit = t * vij;
  float3 N = normalize(hit - input.frag_center);
  float3 hitModel = input.instancePosition.xyz
                  + mul(modelFromView, (input.sphere_radius * float4(N, 1.0))).xyz;
  bool frontfacing = true;
  bool nearOK = tNear > 0.0
             && geometricSurfaceHitIsValid(hitModel, input.clipToCell, input.cellOrigin,
                                           input.firstClip, input.clipCount);
  if (!nearOK)
  {
    t = tFar;
    hit = t * vij;
    N = normalize(hit - input.frag_center);
    hitModel = input.instancePosition.xyz
             + mul(modelFromView, (input.sphere_radius * float4(N, 1.0))).xyz;
    if (tFar <= 0.0
        || !geometricSurfaceHitIsValid(hitModel, input.clipToCell, input.cellOrigin,
                                       input.firstClip, input.clipCount))
      discard;
    frontfacing = false;
  }

  float4 screen_pos = mul(frameUniforms.projectionMatrix, float4(hit, 1.0));
  output.depth = 0.5 * (screen_pos.z / screen_pos.w) + 0.5;

  float3 V = normalize(input.V);
  output.color = shadeGeometricSurface(N, V, float4(hit, 1.0), frontfacing);
)foo";
  }

  if (analyticCoverage)
  {
    common += R"foo(
  if (!(coverage > 0.0))
    discard;
  output.color.a *= coverage;
)foo";
  }

  common += R"foo(
  return output;
}
)foo";
  return common;
}
