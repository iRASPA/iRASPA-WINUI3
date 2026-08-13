/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxboundingboxshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/spheregeometry.h"
#include "geometry/cylindergeometry.h"
#include <cstddef>

void DirectXBoundingBoxShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBoundingBoxShader::initializeSpherePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
    std::cerr << "DirectXBoundingBoxShader: failed to create sphere PSO";
    return;
  }
  _spherePsoReady = true;
}

void DirectXBoundingBoxShader::initializeCylinderPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
    std::cerr << "DirectXBoundingBoxShader: failed to create cylinder PSO";
    return;
  }
  _cylinderPsoReady = true;
}

void DirectXBoundingBoxShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                          DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializeSpherePSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeCylinderPSO(device, rootSignature, rtvFormat, dsvFormat);
}

void DirectXBoundingBoxShader::setRenderDataSource(std::shared_ptr<RKRenderDataSource> source)
{
  _dataSource = std::move(source);
}

void DirectXBoundingBoxShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  _sphereBuffers = MeshBuffers{};
  _cylinderBuffers = MeshBuffers{};

  if (!_dataSource)
    return;

  SphereGeometry sphere(1.0, 41);
  CylinderGeometry cylinder(1.0, 41);

  {
    const auto vertices = sphere.vertices();
    const auto indices = sphere.indices();
    std::vector<RKInPerInstanceAttributesAtoms> instances = _dataSource->renderBoundingBoxSpheres();
    const size_t vbSize = vertices.size() * sizeof(RKVertex);
    const size_t ibSize = indices.size() * sizeof(short);
    _sphereBuffers.indexCount = static_cast<UINT>(indices.size());
    _sphereBuffers.instanceCount = static_cast<UINT>(instances.size());

    _sphereBuffers.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbSize, 1));
    _sphereBuffers.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibSize, 1));
    DirectXDeviceHelpers::writeUploadBuffer(_sphereBuffers.vertexBuffer.Get(), vertices.data(), vbSize);
    DirectXDeviceHelpers::writeUploadBuffer(_sphereBuffers.indexBuffer.Get(), indices.data(), ibSize);
    _sphereBuffers.vbv = { _sphereBuffers.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(vbSize, 1)), sizeof(RKVertex) };
    _sphereBuffers.ibv = { _sphereBuffers.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(ibSize, 1)), DXGI_FORMAT_R16_UINT };

    const size_t instanceBytes = std::max<size_t>(instances.size() * sizeof(RKInPerInstanceAttributesAtoms), 1);
    _sphereBuffers.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instanceBytes);
    if (!instances.empty())
      DirectXDeviceHelpers::writeUploadBuffer(_sphereBuffers.instanceBuffer.Get(), instances.data(),
                                              instances.size() * sizeof(RKInPerInstanceAttributesAtoms));
    _sphereBuffers.instanceVbv = { _sphereBuffers.instanceBuffer->GetGPUVirtualAddress(), static_cast<UINT>(instanceBytes),
                                   sizeof(RKInPerInstanceAttributesAtoms) };
  }

  {
    const auto vertices = cylinder.vertices();
    const auto indices = cylinder.indices();
    std::vector<RKInPerInstanceAttributesBonds> instances = _dataSource->renderBoundingBoxCylinders();
    const size_t vbSize = vertices.size() * sizeof(RKVertex);
    const size_t ibSize = indices.size() * sizeof(short);
    _cylinderBuffers.indexCount = static_cast<UINT>(indices.size());
    _cylinderBuffers.instanceCount = static_cast<UINT>(instances.size());

    _cylinderBuffers.vertexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(vbSize, 1));
    _cylinderBuffers.indexBuffer = DirectXDeviceHelpers::createUploadBuffer(device, std::max<size_t>(ibSize, 1));
    DirectXDeviceHelpers::writeUploadBuffer(_cylinderBuffers.vertexBuffer.Get(), vertices.data(), vbSize);
    DirectXDeviceHelpers::writeUploadBuffer(_cylinderBuffers.indexBuffer.Get(), indices.data(), ibSize);
    _cylinderBuffers.vbv = { _cylinderBuffers.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(vbSize, 1)), sizeof(RKVertex) };
    _cylinderBuffers.ibv = { _cylinderBuffers.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(std::max<size_t>(ibSize, 1)), DXGI_FORMAT_R16_UINT };

    const size_t instanceBytes = std::max<size_t>(instances.size() * sizeof(RKInPerInstanceAttributesBonds), 1);
    _cylinderBuffers.instanceBuffer = DirectXDeviceHelpers::createUploadBuffer(device, instanceBytes);
    if (!instances.empty())
      DirectXDeviceHelpers::writeUploadBuffer(_cylinderBuffers.instanceBuffer.Get(), instances.data(),
                                              instances.size() * sizeof(RKInPerInstanceAttributesBonds));
    _cylinderBuffers.instanceVbv = { _cylinderBuffers.instanceBuffer->GetGPUVirtualAddress(), static_cast<UINT>(instanceBytes),
                                     sizeof(RKInPerInstanceAttributesBonds) };
  }
}

void DirectXBoundingBoxShader::paint(ID3D12GraphicsCommandList *commandList)
{
  if (!_dataSource || !_dataSource->showBoundingBox())
    return;

  if (_spherePsoReady && _spherePso && _sphereBuffers.indexCount > 0 && _sphereBuffers.instanceCount > 0
      && _sphereBuffers.vertexBuffer && _sphereBuffers.indexBuffer && _sphereBuffers.instanceBuffer)
  {
    commandList->SetPipelineState(_spherePso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_VERTEX_BUFFER_VIEW views[2] = { _sphereBuffers.vbv, _sphereBuffers.instanceVbv };
    commandList->IASetVertexBuffers(0, 2, views);
    commandList->IASetIndexBuffer(&_sphereBuffers.ibv);
    commandList->DrawIndexedInstanced(_sphereBuffers.indexCount, _sphereBuffers.instanceCount, 0, 0, 0);
  }

  if (_cylinderPsoReady && _cylinderPso && _cylinderBuffers.indexCount > 0 && _cylinderBuffers.instanceCount > 0
      && _cylinderBuffers.vertexBuffer && _cylinderBuffers.indexBuffer && _cylinderBuffers.instanceBuffer)
  {
    commandList->SetPipelineState(_cylinderPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_VERTEX_BUFFER_VIEW views[2] = { _cylinderBuffers.vbv, _cylinderBuffers.instanceVbv };
    commandList->IASetVertexBuffers(0, 2, views);
    commandList->IASetIndexBuffer(&_cylinderBuffers.ibv);
    commandList->DrawIndexedInstanced(_cylinderBuffers.indexCount, _cylinderBuffers.instanceCount, 0, 0, 0);
  }
}

const std::string DirectXBoundingBoxShader::_sphereVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
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
  float4 diffuse : COLOR0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  float4 scale = input.instanceScale;
  float4 pos = input.instancePosition + scale * input.vertexPosition;

  output.N = mul(frameUniforms.normalMatrix, input.vertexNormal).xyz;
  output.diffuse = float4(1.0, 1.0, 1.0, 1.0);

  float4 P = mul(frameUniforms.viewMatrix, pos);
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, pos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXBoundingBoxShader::_spherePixelShaderSource =
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float4 diffuse : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float4 color = max(dot(N, L), 0.0) * input.diffuse;
  return float4(float3(0.0, 0.75, 1.0) * color.xyz, 1.0);
}
)foo");

const std::string DirectXBoundingBoxShader::_cylinderVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
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
  float4 diffuse : COLOR0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 scale = input.instanceScale;
  float4 pos = scale * float4(input.vertexPosition.xyz, 1.0);

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = length(dr);

  output.diffuse = float4(1.0, 1.0, 1.0, 1.0);

  scale.x = 1.0;
  scale.y = bondLength;
  scale.z = 1.0;

  dr = normalize(dr);
  float3 v1;
  if ((dr.z != 0) && (-dr.x != dr.y))
    v1 = normalize(float3(-dr.y - dr.z, dr.x, dr.x));
  else
    v1 = normalize(float3(dr.z, dr.z, -dr.x - dr.y));
  float3 v2 = normalize(cross(dr, v1));

  float4x4 orientationMatrix = float4x4(
      -v1.x, -dr.x, -v2.x, 0.0,
      -v1.y, -dr.y, -v2.y, 0.0,
      -v1.z, -dr.z, -v2.z, 0.0,
       0.0,   0.0,   0.0,  1.0);

  output.N = mul(frameUniforms.normalMatrix, mul(orientationMatrix, input.vertexNormal)).xyz;

  float4 scaled = float4((scale * pos).xyz, 1.0);
  float4 worldPos = mul(orientationMatrix, scaled) + float4(pos1.xyz, 0.0);
  worldPos.w = 1.0;
  float4 P = mul(frameUniforms.viewMatrix, worldPos);
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, worldPos);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXBoundingBoxShader::_cylinderPixelShaderSource =
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float4 diffuse : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float4 color = max(dot(N, L), 0.0) * input.diffuse;
  return float4(float3(0.0, 0.75, 1.0) * color.xyz, 1.0);
}
)foo");
