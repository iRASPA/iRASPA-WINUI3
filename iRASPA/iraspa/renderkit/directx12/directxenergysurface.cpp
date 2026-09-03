/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxenergysurface.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include <simulationkit.h>
#include <skcomputeisosurface.h>
#include <skwellsurface.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

DirectXEnergySurface::~DirectXEnergySurface()
{
  for (RKCache<RKRenderObject *, std::vector<float>> &cache : _caches)
    cache.clear();
  for (RKCache<RKRenderObject *, std::vector<float>> &cache : _wellFieldCaches)
    cache.clear();
}

void DirectXEnergySurface::loadShader(ID3D12Device * /*device*/)
{
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC DirectXEnergySurface::basePsoDesc(
    ID3D12RootSignature *rootSignature, ID3DBlob *vs, ID3DBlob *ps,
    DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
    const D3D12_INPUT_ELEMENT_DESC *inputLayout, UINT inputLayoutCount) const
{
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
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
  psoDesc.InputLayout = { inputLayout, inputLayoutCount };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();
  return psoDesc;
}

void DirectXEnergySurface::initializeOpaquePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                               DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc =
      basePsoDesc(rootSignature, vs.Get(), ps.Get(), rtvFormat, dsvFormat, inputLayout, _countof(inputLayout));
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_opaquePso))))
  {
    std::cerr << "DirectXEnergySurface: failed to create opaque PSO";
    return;
  }
  _opaquePsoReady = true;
}

void DirectXEnergySurface::initializeTransparentPSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
      D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  auto makeTransparent = [&](D3D12_CULL_MODE cullMode, ComPtr<ID3D12PipelineState> &outPso, const char *label) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc =
        basePsoDesc(rootSignature, vs.Get(), ps.Get(), rtvFormat, dsvFormat, inputLayout, _countof(inputLayout));
    psoDesc.RasterizerState.CullMode = cullMode;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPso))))
    {
      std::cerr << "DirectXEnergySurface: failed to create" << label;
      return false;
    }
    return true;
  };

  if (makeTransparent(D3D12_CULL_MODE_FRONT, _transparentFrontCullPso, "transparent front-cull PSO")
      && makeTransparent(D3D12_CULL_MODE_BACK, _transparentBackCullPso, "transparent back-cull PSO"))
  {
    _transparentPsoReady = true;
  }
}

void DirectXEnergySurface::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                      DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializeOpaquePSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeTransparentPSOs(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXEnergySurface::invalidateIsosurface(std::vector<std::shared_ptr<RKRenderObject>> structures)
{
  for (const std::shared_ptr<RKRenderObject> &structure : structures)
  {
    for (RKCache<RKRenderObject *, std::vector<float>> &cache : _caches)
      cache.remove(structure.get());
    for (RKCache<RKRenderObject *, std::vector<float>> &cache : _wellFieldCaches)
      cache.remove(structure.get());
  }
}

void DirectXEnergySurface::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXEnergySurface::deleteBuffers()
{
  _buffers.clear();
}

void DirectXEnergySurface::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXEnergySurface::reloadData(ID3D12Device *device)
{
  if (!device)
    return;
  initializeVertexBuffers(device);
}

void DirectXEnergySurface::initializeVertexBuffers(ID3D12Device *device)
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      MeshBuffers &bufs = _buffers[i][j];
      bufs = MeshBuffers{};

      auto *renderStructure = dynamic_cast<RKRenderObject *>(_renderStructures[i][j].get());
      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      if (!renderStructure || !source)
        continue;
      if (!source->drawAdsorptionSurface() || !isTriangulated(source->adsorptionSurfaceRenderingMethod()))
        continue;

      const double isoValue = source->adsorptionSurfaceIsoValue();

      // The well surface and its filament overlay are level sets of the analytic force field; the
      // iso-surface is a level set of the energy grid. The two fields are cached apart, so that
      // switching rendering methods cannot hand one builder the other's data. Where no well field
      // exists (imported volumetric data has no analytic form) the iso-surface stands in for it.
      const RKEnergySurfaceType renderingMethod = source->adsorptionSurfaceRenderingMethod();
      bool wellSurface = isWellSurface(renderingMethod);

      auto cacheSlot = [this](int3 dims) {
        const int largestSize = std::max({dims.x, dims.y, dims.z});
        int powerOfTwo = 1;
        while (largestSize > static_cast<int>(std::pow(2, powerOfTwo)))
          powerOfTwo += 1;
        return std::clamp(powerOfTwo, 0, static_cast<int>(_caches.size()) - 1);
      };

      std::vector<float> *fieldPointer = nullptr;
      int3 dimensions = source->dimensions();
      int powerOfTwo = cacheSlot(dimensions);
      auto &caches = wellSurface ? _wellFieldCaches : _caches;

      bool cached = caches[powerOfTwo].contains(_renderStructures[i][j].get());
      if (cached)
      {
        fieldPointer = caches[powerOfTwo].object(_renderStructures[i][j].get());
      }
      else
      {
        std::vector<float> field = wellSurface ? source->wellFieldData() : source->gridData();
        if (field.empty() && wellSurface)
        {
          std::cerr << "DirectXEnergySurface: no well field for this structure; showing the isosurface "
                       "instead\n";
          wellSurface = false;
          dimensions = source->dimensions();
          powerOfTwo = cacheSlot(dimensions);
          cached = _caches[powerOfTwo].contains(_renderStructures[i][j].get());
          if (cached)
            fieldPointer = _caches[powerOfTwo].object(_renderStructures[i][j].get());
          else
            field = source->gridData();
        }
        if (!cached)
        {
          if (field.empty())
            continue;
          dimensions = source->dimensions();
          powerOfTwo = cacheSlot(dimensions);
          fieldPointer = new std::vector<float>(std::move(field));
        }
      }

      std::vector<float4> triangleData;
      if (wellSurface && renderingMethod == RKEnergySurfaceType::wellSurfaceOverlay)
      {
        // The merged-well filament: the thin tube along channel axes too narrow for the probe's
        // contact sheet, where the adsorbate is enclosed and sits on the axis.
        triangleData = SKWellSurface::constructWellFilament(*fieldPointer, isoValue, dimensions,
                                                            renderStructure->cell()->unitCell());
      }
      else if (wellSurface)
      {
        triangleData = SKWellSurface::constructWellSurface(*fieldPointer, isoValue, dimensions);
        if (!triangleData.empty())
        {
          source->refineWellSurface(
              triangleData, SKWellSurface::effectiveTrimIsovalue(*fieldPointer, isoValue, dimensions));
        }
      }
      else
      {
        triangleData = SKComputeIsosurface::computeIsosurface(dimensions, fieldPointer, isoValue);
      }
      bufs.numberOfIndices = triangleData.size() / (3 * 3);
      bufs.vertexCount = static_cast<UINT>(3 * bufs.numberOfIndices);

      const size_t vbBytes = triangleData.size() * sizeof(float4);
      bufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbBytes, 1));
      if (!triangleData.empty())
        DirectXDeviceHelpers::writeUploadBuffer(bufs.vertexBuffer.Get(), triangleData.data(), vbBytes);
      // Stride matches OpenGL: sizeof(RKVertex) == 3 * float4 (pos, normal, pad).
      bufs.vbv = { bufs.vertexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(std::max<size_t>(vbBytes, 1)),
                   static_cast<UINT>(sizeof(RKVertex)) };

      std::vector<float4> renderLatticeVectors = renderStructure->cell()->renderTranslationVectors();
      bufs.instanceCount = static_cast<UINT>(renderLatticeVectors.size());
      const size_t instanceBytes = std::max<size_t>(renderLatticeVectors.size() * sizeof(float4), 1);
      bufs.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instanceBytes);
      if (!renderLatticeVectors.empty())
        DirectXDeviceHelpers::writeUploadBuffer(bufs.instanceBuffer.Get(), renderLatticeVectors.data(),
                                                renderLatticeVectors.size() * sizeof(float4));
      bufs.instanceVbv = { bufs.instanceBuffer->GetGPUVirtualAddress(),
                           static_cast<UINT>(instanceBytes),
                           sizeof(float4) };

      // Insert last so a failed/evicting insert does not free the grid mid-use.
      if (!cached)
      {
        (wellSurface ? _wellFieldCaches : _caches)[powerOfTwo].insert(_renderStructures[i][j].get(),
                                                                      fieldPointer);
      }
    }
  }
}

void DirectXEnergySurface::paintOpaque(ID3D12GraphicsCommandList *commandList,
                                       D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                       UINT structureCBVStride,
                                       D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                                       UINT isosurfaceCBVStride)
{
  if (!_opaquePsoReady || !_opaquePso)
    return;

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *renderStructure = dynamic_cast<RKRenderObject *>(_renderStructures[i][j].get());
      auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[i][j].get());
      const MeshBuffers &bufs = _buffers[i][j];
      if (renderStructure && source
          && renderStructure->isVisible()
          && source->drawAdsorptionSurface()
          && source->adsorptionSurfaceOpacity() > 0.99999
          && bufs.numberOfIndices > 0
          && bufs.instanceCount > 0
          && bufs.vertexBuffer && bufs.instanceBuffer)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        commandList->SetGraphicsRootConstantBufferView(
            4, isosurfaceCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * isosurfaceCBVStride);

        commandList->SetPipelineState(_opaquePso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW views[2] = { bufs.vbv, bufs.instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->DrawInstanced(bufs.vertexCount, bufs.instanceCount, 0, 0);
      }
      ++index;
    }
  }
}

void DirectXEnergySurface::paintTransparent(ID3D12GraphicsCommandList *commandList,
                                            D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                            UINT structureCBVStride,
                                            D3D12_GPU_VIRTUAL_ADDRESS isosurfaceCBVBase,
                                            UINT isosurfaceCBVStride,
                                            size_t sceneIndex, size_t movieIndex, size_t structureIndex)
{
  if (!_transparentPsoReady || !_transparentFrontCullPso || !_transparentBackCullPso)
    return;
  if (sceneIndex >= _renderStructures.size() || movieIndex >= _renderStructures[sceneIndex].size())
    return;
  if (sceneIndex >= _buffers.size() || movieIndex >= _buffers[sceneIndex].size())
    return;

  auto *source = dynamic_cast<RKRenderVolumetricDataSource *>(_renderStructures[sceneIndex][movieIndex].get());
  const MeshBuffers &bufs = _buffers[sceneIndex][movieIndex];
  if (!source
      || !_renderStructures[sceneIndex][movieIndex]->isVisible()
      || !source->drawAdsorptionSurface()
      || !isTriangulated(source->adsorptionSurfaceRenderingMethod())
      || source->adsorptionSurfaceOpacity() > 0.99999
      || bufs.numberOfIndices == 0
      || bufs.instanceCount == 0
      || !bufs.vertexBuffer || !bufs.instanceBuffer)
    return;

  commandList->SetGraphicsRootConstantBufferView(
      1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureIndex) * structureCBVStride);
  commandList->SetGraphicsRootConstantBufferView(
      4, isosurfaceCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(structureIndex) * isosurfaceCBVStride);

  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VERTEX_BUFFER_VIEW views[2] = { bufs.vbv, bufs.instanceVbv };
  commandList->IASetVertexBuffers(0, 2, views);

  commandList->SetPipelineState(_transparentFrontCullPso.Get());
  commandList->DrawInstanced(bufs.vertexCount, bufs.instanceCount, 0, 0);

  commandList->SetPipelineState(_transparentBackCullPso.Get());
  commandList->DrawInstanced(bufs.vertexCount, bufs.instanceCount, 0, 0);
}

const std::string DirectXEnergySurface::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::IsosurfaceUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = mul(isosurfaceUniforms.unitCellMatrix, input.vertexPosition + input.instancePosition);

  output.N = mul(frameUniforms.normalMatrix,
                 mul(structureUniforms.modelMatrix,
                     mul(isosurfaceUniforms.unitCellNormalMatrix, input.vertexNormal))).xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXEnergySurface::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::IsosurfaceUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 V : TEXCOORD1;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 V = normalize(input.V);

  float4 ambient;
  float4 specular;
  float4 diffuse;
  float4 color;

  if (isFrontFace)
  {
    // The two sides carry their own materials and their own shininess, so the rig is summed once
    // per side rather than shared. Unshadowed: the surface is not part of the traced geometry.
    LightingWeights lighting = accumulateLighting(N, V, float4(-input.V, 1.0),
                                                  isosurfaceUniforms.shininessFrontSide);
    ambient = float4(lighting.ambient, 1.0) * isosurfaceUniforms.ambientFrontSide;
    diffuse = float4(lighting.diffuse, 1.0) * isosurfaceUniforms.diffuseFrontSide;
    specular = float4(lighting.specular, 1.0) * isosurfaceUniforms.specularFrontSide;
    color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
    if (isosurfaceUniforms.frontHDR)
    {
      float4 vLdrColor = 1.0 - exp2(-color * isosurfaceUniforms.frontHDRExposure);
      vLdrColor.a = 1.0;
      color = vLdrColor;
    }
  }
  else
  {
    // The normal is flipped so the far side is lit by what actually reaches it.
    LightingWeights lighting = accumulateLighting(-N, V, float4(-input.V, 1.0),
                                                  isosurfaceUniforms.shininessBackSide);
    ambient = float4(lighting.ambient, 1.0) * isosurfaceUniforms.ambientBackSide;
    diffuse = float4(lighting.diffuse, 1.0) * isosurfaceUniforms.diffuseBackSide;
    specular = float4(lighting.specular, 1.0) * isosurfaceUniforms.specularBackSide;
    color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
    if (isosurfaceUniforms.backHDR)
    {
      float4 vLdrColor = 1.0 - exp2(-color * isosurfaceUniforms.backHDRExposure);
      vLdrColor.a = 1.0;
      color = vLdrColor;
    }
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * isosurfaceUniforms.hue;
  hsv.y = hsv.y * isosurfaceUniforms.saturation;
  hsv.z = hsv.z * isosurfaceUniforms.value;
  return float4(hsv2rgb(hsv) * isosurfaceUniforms.diffuseFrontSide.w, isosurfaceUniforms.diffuseFrontSide.w);
}
)foo");
