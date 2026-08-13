/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxobjectshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"
#include "geometry/cappedcylindergeometry.h"
#include "geometry/uncappedcylindergeometry.h"
#include "geometry/cappednsidedprismgeometry.h"
#include "geometry/nsidedprismgeometry.h"
#include <algorithm>
#include <cstddef>

void DirectXObjectShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXObjectShader::initializePSOs(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  auto makePso = [&](bool opaque, ComPtr<ID3D12PipelineState> &outPso, const char *label) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
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
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // Match OpenGL/Metal opaque cylinders & prisms (CullNone). Ellipse is fine without cull;
    // PS shades both sides via SV_IsFrontFace. Transparent also draws both faces.
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask =
        opaque ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = dsvFormat;
    psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPso))))
    {
      std::cerr << "DirectXObjectShader: failed to create" << label;
      return false;
    }
    return true;
  };

  _opaquePsoReady = makePso(true, _opaquePso, "opaque PSO");
  _transparentPsoReady = makePso(false, _transparentPso, "transparent PSO");
}

void DirectXObjectShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                     DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  if (!device || !rootSignature)
    return;
  initializePSOs(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXObjectShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXObjectShader::deleteBuffers()
{
  for (auto &kindBuffers : _buffers)
    kindBuffers.clear();
}

void DirectXObjectShader::generateBuffers()
{
  for (size_t k = 0; k < static_cast<size_t>(Kind::count); ++k)
  {
    _buffers[k].resize(_renderStructures.size());
    for (size_t i = 0; i < _renderStructures.size(); ++i)
      _buffers[k][i].resize(_renderStructures[i].size());
  }
}

void DirectXObjectShader::reloadKind(ID3D12Device *device, Kind kind)
{
  const size_t ki = static_cast<size_t>(kind);

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      MeshBuffers &bufs = _buffers[ki][i][j];
      bufs = MeshBuffers{};

      auto *prim = dynamic_cast<RKRenderPrimitiveObjectsSource *>(_renderStructures[i][j].get());
      if (!prim || !prim->drawAtoms() || !_renderStructures[i][j]->isVisible())
        continue;

      std::vector<RKInPerInstanceAttributesAtoms> instances;
      std::vector<RKVertex> vertices;
      std::vector<short> indices;

      switch (kind)
      {
      case Kind::crystalEllipse:
        if (auto *o = dynamic_cast<RKRenderCrystalPrimitiveEllipsoidObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderCrystalPrimitiveEllipsoidObjects();
          SphereGeometry sphere(1.0, 41);
          vertices = sphere.vertices();
          indices = sphere.indices();
        }
        break;
      case Kind::ellipse:
        if (auto *o = dynamic_cast<RKRenderPrimitiveEllipsoidObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderPrimitiveEllipsoidObjects();
          SphereGeometry sphere(1.0, 41);
          vertices = sphere.vertices();
          indices = sphere.indices();
        }
        break;
      case Kind::crystalCylinder:
        if (auto *o = dynamic_cast<RKRenderCrystalPrimitiveCylinderObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderCrystalPrimitiveCylinderObjects();
          const int sides = std::max(3, prim->primitiveNumberOfSides());
          if (prim->primitiveIsCapped())
          {
            CappedCylinderGeometry cyl(1.0, sides);
            vertices = cyl.vertices();
            indices = cyl.indices();
          }
          else
          {
            UnCappedCylinderGeometry cyl(1.0, sides);
            vertices = cyl.vertices();
            indices = cyl.indices();
          }
        }
        break;
      case Kind::cylinder:
        if (auto *o = dynamic_cast<RKRenderPrimitiveCylinderObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderPrimitiveCylinderObjects();
          const int sides = std::max(3, prim->primitiveNumberOfSides());
          if (prim->primitiveIsCapped())
          {
            CappedCylinderGeometry cyl(1.0, sides);
            vertices = cyl.vertices();
            indices = cyl.indices();
          }
          else
          {
            UnCappedCylinderGeometry cyl(1.0, sides);
            vertices = cyl.vertices();
            indices = cyl.indices();
          }
        }
        break;
      case Kind::crystalPrism:
        if (auto *o = dynamic_cast<RKRenderCrystalPrimitivePolygonalPrimsObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderCrystalPrimitivePolygonalPrismObjects();
          const int sides = std::max(3, prim->primitiveNumberOfSides());
          if (prim->primitiveIsCapped())
          {
            CappedNSidedPrismGeometry prism(1.0, sides);
            vertices = prism.vertices();
            indices = prism.indices();
          }
          else
          {
            NSidedPrismGeometry prism(1.0, sides);
            vertices = prism.vertices();
            indices = prism.indices();
          }
        }
        break;
      case Kind::prism:
        if (auto *o = dynamic_cast<RKRenderPrimitivePolygonalPrimsObjectsSource *>(
                _renderStructures[i][j].get()))
        {
          instances = o->renderPrimitivePolygonalPrismObjects();
          const int sides = std::max(3, prim->primitiveNumberOfSides());
          if (prim->primitiveIsCapped())
          {
            CappedNSidedPrismGeometry prism(1.0, sides);
            vertices = prism.vertices();
            indices = prism.indices();
          }
          else
          {
            NSidedPrismGeometry prism(1.0, sides);
            vertices = prism.vertices();
            indices = prism.indices();
          }
        }
        break;
      default:
        break;
      }

      if (instances.empty() || vertices.empty() || indices.empty())
        continue;

      const size_t vbBytes = vertices.size() * sizeof(RKVertex);
      const size_t ibBytes = indices.size() * sizeof(short);
      const size_t instBytes = instances.size() * sizeof(RKInPerInstanceAttributesAtoms);

      bufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, vbBytes);
      bufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, ibBytes);
      bufs.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instBytes);
      if (!bufs.vertexBuffer || !bufs.indexBuffer || !bufs.instanceBuffer)
        continue;

      DirectXDeviceHelpers::writeUploadBuffer(bufs.vertexBuffer.Get(), vertices.data(), vbBytes);
      DirectXDeviceHelpers::writeUploadBuffer(bufs.indexBuffer.Get(), indices.data(), ibBytes);
      DirectXDeviceHelpers::writeUploadBuffer(bufs.instanceBuffer.Get(), instances.data(), instBytes);

      bufs.vbv = { bufs.vertexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(vbBytes),
                   static_cast<UINT>(sizeof(RKVertex)) };
      bufs.ibv = { bufs.indexBuffer->GetGPUVirtualAddress(),
                   static_cast<UINT>(ibBytes),
                   DXGI_FORMAT_R16_UINT };
      bufs.instanceVbv = { bufs.instanceBuffer->GetGPUVirtualAddress(),
                           static_cast<UINT>(instBytes),
                           static_cast<UINT>(sizeof(RKInPerInstanceAttributesAtoms)) };
      bufs.indexCount = static_cast<UINT>(indices.size());
      bufs.instanceCount = static_cast<UINT>(instances.size());
    }
  }
}

void DirectXObjectShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;
  for (size_t k = 0; k < static_cast<size_t>(Kind::count); ++k)
    reloadKind(device, static_cast<Kind>(k));
}

void DirectXObjectShader::paintKind(ID3D12GraphicsCommandList *commandList, Kind kind, bool opaque,
                                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                    UINT structureCBVStride)
{
  ID3D12PipelineState *pso = opaque ? _opaquePso.Get() : _transparentPso.Get();
  if (!commandList || !pso)
    return;

  commandList->SetPipelineState(pso);
  // Sphere (ellipse) meshes use strip indices; cylinders/prisms use triangle lists.
  const bool strip = (kind == Kind::crystalEllipse || kind == Kind::ellipse);
  commandList->IASetPrimitiveTopology(
      strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  const size_t ki = static_cast<size_t>(kind);
  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *prim = dynamic_cast<RKRenderPrimitiveObjectsSource *>(_renderStructures[i][j].get());
      const MeshBuffers &bufs = _buffers[ki][i][j];
      const bool draw = prim
          && prim->drawAtoms()
          && _renderStructures[i][j]->isVisible()
          && bufs.indexCount > 0
          && bufs.instanceCount > 0
          && bufs.vertexBuffer
          && bufs.indexBuffer
          && bufs.instanceBuffer;

      bool passMatch = false;
      if (draw)
      {
        const double opacity = prim->primitiveOpacity();
        passMatch = opaque ? (opacity > 0.99999) : (opacity <= 0.99999);
      }

      if (passMatch)
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
        D3D12_VERTEX_BUFFER_VIEW views[2] = { bufs.vbv, bufs.instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&bufs.ibv);
        commandList->DrawIndexedInstanced(bufs.indexCount, bufs.instanceCount, 0, 0, 0);
      }
      ++index;
    }
  }
}

void DirectXObjectShader::paintOpaque(ID3D12GraphicsCommandList *commandList,
                                      D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                      UINT structureCBVStride)
{
  if (!_opaquePsoReady || !_opaquePso)
    return;
  for (size_t k = 0; k < static_cast<size_t>(Kind::count); ++k)
    paintKind(commandList, static_cast<Kind>(k), true, structureCBVBase, structureCBVStride);
}

void DirectXObjectShader::paintTransparent(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride)
{
  if (!_transparentPsoReady || !_transparentPso)
    return;
  for (size_t k = 0; k < static_cast<size_t>(Kind::count); ++k)
    paintKind(commandList, static_cast<Kind>(k), false, structureCBVBase, structureCBVStride);
}

void DirectXObjectShader::drawPickGeometry(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride) const
{
  if (!commandList)
    return;

  for (size_t k = 0; k < static_cast<size_t>(Kind::count); ++k)
  {
    const Kind kind = static_cast<Kind>(k);
    const bool strip = (kind == Kind::crystalEllipse || kind == Kind::ellipse);
    commandList->IASetPrimitiveTopology(
        strip ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    size_t index = 0;
    for (size_t i = 0; i < _renderStructures.size(); ++i)
    {
      for (size_t j = 0; j < _renderStructures[i].size(); ++j)
      {
        auto *prim = dynamic_cast<RKRenderPrimitiveObjectsSource *>(_renderStructures[i][j].get());
        const MeshBuffers &bufs = _buffers[k][i][j];
        if (prim && prim->drawAtoms() && _renderStructures[i][j]->isVisible()
            && bufs.indexCount > 0 && bufs.instanceCount > 0
            && bufs.vertexBuffer && bufs.indexBuffer && bufs.instanceBuffer)
        {
          commandList->SetGraphicsRootConstantBufferView(
              1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);
          D3D12_VERTEX_BUFFER_VIEW views[2] = { bufs.vbv, bufs.instanceVbv };
          commandList->IASetVertexBuffers(0, 2, views);
          commandList->IASetIndexBuffer(&bufs.ibv);
          commandList->DrawIndexedInstanced(bufs.indexCount, bufs.instanceCount, 0, 0, 0);
        }
        ++index;
      }
    }
  }
}

const std::string DirectXObjectShader::_vertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
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
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 pos = input.instancePosition
               + mul(structureUniforms.transformationMatrix, input.vertexPosition);

  output.ModelN = input.vertexNormal.xyz;
  output.N = mul(frameUniforms.normalMatrix,
                 mul(structureUniforms.modelMatrix,
                     mul(structureUniforms.transformationNormalMatrix, input.vertexNormal))).xyz;

  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXObjectShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 ModelN : NORMAL1;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  bool isFrontFace : SV_IsFrontFace;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float4 ambient, diffuse, specular, color;
  if (input.isFrontFace)
  {
    ambient = structureUniforms.primitiveAmbientFrontSide;
    diffuse = max(dot(N, L), 0.0) * structureUniforms.primitiveDiffuseFrontSide;
    specular = pow(max(dot(R, V), 0.0), structureUniforms.primitiveShininessFrontSide)
               * structureUniforms.primitiveSpecularFrontSide;
    color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
    if (structureUniforms.primitiveFrontSideHDR != 0)
    {
      float4 ldr = 1.0 - exp2(-color * structureUniforms.primitiveFrontSideHDRExposure);
      ldr.a = 1.0;
      color = ldr;
    }
  }
  else
  {
    ambient = structureUniforms.primitiveAmbientBackSide;
    diffuse = max(dot(-N, L), 0.0) * structureUniforms.primitiveDiffuseBackSide;
    specular = pow(max(dot(R, V), 0.0), structureUniforms.primitiveShininessBackSide)
               * structureUniforms.primitiveSpecularBackSide;
    color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);
    if (structureUniforms.primitiveBackSideHDR != 0)
    {
      float4 ldr = 1.0 - exp2(-color * structureUniforms.primitiveBackSideHDRExposure);
      ldr.a = 1.0;
      color = ldr;
    }
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.primitiveHue;
  hsv.y = hsv.y * structureUniforms.primitiveSaturation;
  hsv.z = hsv.z * structureUniforms.primitiveValue;
  return structureUniforms.primitiveOpacity * float4(hsv2rgb(hsv), 1.0);
}
)foo");
