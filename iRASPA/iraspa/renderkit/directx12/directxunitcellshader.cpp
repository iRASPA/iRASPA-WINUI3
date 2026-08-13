/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxunitcellshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"
#include "geometry/cappedcylindersinglebondgeometry.h"
#include <cstddef>

void DirectXUnitCellShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXUnitCellShader::initializeSpherePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_sphereVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_spherePixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, position)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesAtoms, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  // Thin cell edges + Y-flip: never cull (OpenGL CCW strips become CW after m22 flip).
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_spherePso))))
  {
    std::cerr << "DirectXUnitCellShader: failed to create sphere PSO";
    return;
  }
  _spherePsoReady = true;
}

void DirectXUnitCellShader::initializeCylinderPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                  DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_cylinderVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_cylinderPixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    // HLSL "INSTANCEPOSITION1" == semantic INSTANCEPOSITION, index 1.
    { "INSTANCEPOSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position1)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCEPOSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position2)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
      static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, scale)),
      D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
  };

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
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_cylinderPso))))
  {
    std::cerr << "DirectXUnitCellShader: failed to create cylinder PSO";
    return;
  }
  _cylinderPsoReady = true;
}

void DirectXUnitCellShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                       DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializeSpherePSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeCylinderPSO(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXUnitCellShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXUnitCellShader::deleteBuffers()
{
  _sphereBuffers.clear();
  _cylinderBuffers.clear();
}

void DirectXUnitCellShader::generateBuffers()
{
  _sphereBuffers.resize(_renderStructures.size());
  _cylinderBuffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    _sphereBuffers[i].resize(_renderStructures[i].size());
    _cylinderBuffers[i].resize(_renderStructures[i].size());
  }
}

void DirectXUnitCellShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  SphereGeometry sphere(1.0, 41);
  // Use triangle-list capped cylinder (same as bonds). CylinderGeometry is a strip mesh.
  CappedCylinderSingleBondGeometry cylinder(1.0, 41);
  const auto sphereVertices = sphere.vertices();
  const auto sphereIndices = sphere.indices();
  const auto cylinderVertices = cylinder.vertices();
  const auto cylinderIndices = cylinder.indices();

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      MeshBuffers &sphereBufs = _sphereBuffers[i][j];
      MeshBuffers &cylinderBufs = _cylinderBuffers[i][j];
      sphereBufs = MeshBuffers{};
      cylinderBufs = MeshBuffers{};

      auto *source = dynamic_cast<RKRenderUnitCellSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      {
        std::vector<RKInPerInstanceAttributesAtoms> instances = source->renderUnitCellSpheres();
        const size_t vbSize = sphereVertices.size() * sizeof(RKVertex);
        const size_t ibSize = sphereIndices.size() * sizeof(short);
        sphereBufs.indexCount = static_cast<UINT>(sphereIndices.size());
        sphereBufs.instanceCount = static_cast<UINT>(instances.size());
        sphereBufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbSize, 1));
        sphereBufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibSize, 1));
        DirectXDeviceHelpers::writeUploadBuffer(sphereBufs.vertexBuffer.Get(), sphereVertices.data(), vbSize);
        DirectXDeviceHelpers::writeUploadBuffer(sphereBufs.indexBuffer.Get(), sphereIndices.data(), ibSize);

        sphereBufs.vbv = { sphereBufs.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(vbSize, 1)), sizeof(RKVertex) };
        sphereBufs.ibv = { sphereBufs.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(ibSize, 1)), DXGI_FORMAT_R16_UINT };

        const size_t instanceBytes = std::max<size_t>(instances.size() * sizeof(RKInPerInstanceAttributesAtoms), 1);
        sphereBufs.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instanceBytes);
        if (!instances.empty())
          DirectXDeviceHelpers::writeUploadBuffer(sphereBufs.instanceBuffer.Get(), instances.data(),
                                                  instances.size() * sizeof(RKInPerInstanceAttributesAtoms));
        sphereBufs.instanceVbv = { sphereBufs.instanceBuffer->GetGPUVirtualAddress(), static_cast<UINT>(instanceBytes),
                                   sizeof(RKInPerInstanceAttributesAtoms) };
      }

      {
        std::vector<RKInPerInstanceAttributesBonds> instances = source->renderUnitCellCylinders();
        const size_t vbSize = cylinderVertices.size() * sizeof(RKVertex);
        const size_t ibSize = cylinderIndices.size() * sizeof(short);
        cylinderBufs.indexCount = static_cast<UINT>(cylinderIndices.size());
        cylinderBufs.instanceCount = static_cast<UINT>(instances.size());

        cylinderBufs.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbSize, 1));
        cylinderBufs.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibSize, 1));
        DirectXDeviceHelpers::writeUploadBuffer(cylinderBufs.vertexBuffer.Get(), cylinderVertices.data(), vbSize);
        DirectXDeviceHelpers::writeUploadBuffer(cylinderBufs.indexBuffer.Get(), cylinderIndices.data(), ibSize);

        cylinderBufs.vbv = { cylinderBufs.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(vbSize, 1)), sizeof(RKVertex) };
        cylinderBufs.ibv = { cylinderBufs.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(ibSize, 1)), DXGI_FORMAT_R16_UINT };

        const size_t instanceBytes = std::max<size_t>(instances.size() * sizeof(RKInPerInstanceAttributesBonds), 1);
        cylinderBufs.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instanceBytes);
        if (!instances.empty())
          DirectXDeviceHelpers::writeUploadBuffer(cylinderBufs.instanceBuffer.Get(), instances.data(),
                                                  instances.size() * sizeof(RKInPerInstanceAttributesBonds));
        cylinderBufs.instanceVbv = { cylinderBufs.instanceBuffer->GetGPUVirtualAddress(), static_cast<UINT>(instanceBytes),
                                     sizeof(RKInPerInstanceAttributesBonds) };
      }

      std::cerr << "DirectXUnitCellShader reload" << i << j
               << "draw" << source->drawUnitCell()
               << "spheres" << sphereBufs.instanceCount
               << "cylinders" << cylinderBufs.instanceCount
               << "cylIdx" << cylinderBufs.indexCount
               << "pso" << _spherePsoReady << _cylinderPsoReady;
    }
  }
}

void DirectXUnitCellShader::paint(ID3D12GraphicsCommandList *commandList,
                                  D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                  UINT structureCBVStride)
{
  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderUnitCellSource *>(_renderStructures[i][j].get());
      if (source && source->drawUnitCell() && _renderStructures[i][j]->isVisible())
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        const MeshBuffers &sphereBufs = _sphereBuffers[i][j];
        if (_spherePsoReady && _spherePso && sphereBufs.indexCount > 0 && sphereBufs.instanceCount > 0
            && sphereBufs.vertexBuffer && sphereBufs.indexBuffer && sphereBufs.instanceBuffer)
        {
          commandList->SetPipelineState(_spherePso.Get());
          commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
          D3D12_VERTEX_BUFFER_VIEW views[2] = { sphereBufs.vbv, sphereBufs.instanceVbv };
          commandList->IASetVertexBuffers(0, 2, views);
          commandList->IASetIndexBuffer(&sphereBufs.ibv);
          commandList->DrawIndexedInstanced(sphereBufs.indexCount, sphereBufs.instanceCount, 0, 0, 0);
        }

        const MeshBuffers &cylinderBufs = _cylinderBuffers[i][j];
        if (_cylinderPsoReady && _cylinderPso && cylinderBufs.indexCount > 0 && cylinderBufs.instanceCount > 0
            && cylinderBufs.vertexBuffer && cylinderBufs.indexBuffer && cylinderBufs.instanceBuffer)
        {
          commandList->SetPipelineState(_cylinderPso.Get());
          commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
          D3D12_VERTEX_BUFFER_VIEW views[2] = { cylinderBufs.vbv, cylinderBufs.instanceVbv };
          commandList->IASetVertexBuffers(0, 2, views);
          commandList->IASetIndexBuffer(&cylinderBufs.ibv);
          commandList->DrawIndexedInstanced(cylinderBufs.indexCount, cylinderBufs.instanceCount, 0, 0, 0);
        }
      }
      ++index;
    }
  }
}

const std::string DirectXUnitCellShader::_sphereVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition : INSTANCEPOSITION;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  // Match OpenGL: radius = unitCellScaling * instanceScale (instanceScale already holds 0.0025*bbox).
  float4 scale = structureUniforms.unitCellScaling * input.instanceScale;
  float4 pos = input.instancePosition + scale * float4(input.vertexPosition.xyz, 0.0);

  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;
  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, pos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, pos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXUnitCellShader::_spherePixelShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float ndotl = max(dot(N, L), 0.0);
  float4 color = (0.25 + 0.75 * ndotl) * structureUniforms.unitCellColor;

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");

const std::string DirectXUnitCellShader::_cylinderVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
  float4 instancePosition1 : INSTANCEPOSITION1;
  float4 instancePosition2 : INSTANCEPOSITION2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;

  // Match OpenGL unit-cell cylinder exactly (including instanceScale pre-multiply).
  float4 scale = input.instanceScale;
  float4 pos = scale * float4(input.vertexPosition.xyz, 1.0);

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = max(length(dr), 1e-5);

  float us = structureUniforms.unitCellScaling;
  if (us < 1e-6)
    us = 1.0;
  // Match OpenGL unit-cell cylinder (no extra thickness multiplier).
  scale.x = us;
  scale.y = bondLength;
  scale.z = us;
  scale.w = 1.0;

  dr /= bondLength;
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));
  // GLSL mat4 columns = (-v1, -dr, -v2). Apply as column vectors (avoid float4x4 ctor ambiguity).
  float3 c0 = -v1;
  float3 c1 = -dr;
  float3 c2 = -v2;

  float3 local = (scale * pos).xyz;
  float3 world = pos1.xyz + c0 * local.x + c1 * local.y + c2 * local.z;

  float3 nLocal = input.vertexNormal.xyz;
  float3 nWorld = c0 * nLocal.x + c1 * nLocal.y + c2 * nLocal.z;
  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, float4(nWorld, 0.0))).xyz;

  float4 worldPos = float4(world, 1.0);
  float4 P = mul(frameUniforms.viewMatrix, mul(structureUniforms.modelMatrix, worldPos));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, worldPos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXUnitCellShader::_cylinderPixelShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float ndotl = max(dot(N, L), 0.0);
  float4 color = (0.35 + 0.65 * ndotl) * structureUniforms.unitCellColor;

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");
