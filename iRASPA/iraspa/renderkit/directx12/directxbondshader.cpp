/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxbondshader.h"
#include <iostream>
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/cappedcylindersinglebondgeometry.h"
#include "geometry/cappedcylinderdoublebondgeometry.h"
#include "geometry/cappedcylinderpartialdoublebondgeometry.h"
#include "geometry/cappedcylindertriplebondgeometry.h"
#include "geometry/cubegeometry.h"
#include "skasymmetricbond.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace
{
void fillBondInputLayout(D3D12_INPUT_ELEMENT_DESC *out)
{
  out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
  out[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
  // HLSL INSTANCEPOSITION1 == semantic INSTANCEPOSITION, index 1.
  out[2] = { "INSTANCEPOSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
             static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position1)),
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  out[3] = { "INSTANCEPOSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
             static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, position2)),
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  out[4] = { "INSTANCECOLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
             static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color1)),
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  out[5] = { "INSTANCECOLOR", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
             static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, color2)),
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
  out[6] = { "INSTANCESCALE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
             static_cast<UINT>(offsetof(RKInPerInstanceAttributesBonds, scale)),
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
}

const char *kCylinderBody = R"foo(
  float4 scale = input.instanceScale;
  float4 pos = float4(input.vertexPosition.xyz, 1.0);

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  // Bond visibility is encoded in position.w (±1), same as atoms.
  if (pos1.w < 0.0 || pos2.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
    return output;
  }

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = max(length(dr), 1e-5);

  output.mixValue.x = clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.x;
  output.mixValue.y = input.vertexPosition.y;
  output.mixValue.z = 1.0 - clamp(structureUniforms.atomScaleFactor, 0.0, 0.7) * scale.z;
  output.mixValue.w = input.instanceScale.x / max(input.instanceScale.z, 1e-5);

  output.color1 = input.instanceColor1;
  output.color2 = input.instanceColor2;

  scale.x = max(structureUniforms.bondScaling, 0.02);
  scale.y = bondLength;
  scale.z = max(structureUniforms.bondScaling, 0.02);
  scale.w = 1.0;

  dr /= bondLength;
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));
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
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, worldPos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
)foo";
}

void DirectXBondShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBondShader::initializeInternalPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                              DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_vertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[7];
  ::fillBondInputLayout(inputLayout);

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
  psoDesc.InputLayout = { inputLayout, 7 };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso))))
  {
    std::cerr << "DirectXBondShader: failed to create internal PSO";
    return;
  }
  _psoReady = true;
}

void DirectXBondShader::initializeExternalPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                              DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_externalVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_pixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[7];
  ::fillBondInputLayout(inputLayout);

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
  psoDesc.InputLayout = { inputLayout, 7 };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_externalPso))))
  {
    std::cerr << "DirectXBondShader: failed to create external PSO";
    return;
  }
  _externalPsoReady = true;
}

void DirectXBondShader::initializeStencilPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                             DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_stencilVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_stencilPixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[7];
  ::fillBondInputLayout(inputLayout);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.DepthStencilState.StencilEnable = TRUE;
  // Match OpenGL (mask 0x1) / Metal (writeMask 0x1): INVERT must flip only bit 0
  // so the box pass EQUAL(ref=1) can see stencil value 1 (not 0xFF).
  psoDesc.DepthStencilState.StencilReadMask = 0x1;
  psoDesc.DepthStencilState.StencilWriteMask = 0x1;
  psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
  psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_INVERT;
  psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INVERT;
  psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace;
  psoDesc.InputLayout = { inputLayout, 7 };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_stencilPso))))
  {
    std::cerr << "DirectXBondShader: failed to create stencil PSO";
    return;
  }
  _stencilPsoReady = true;
}

void DirectXBondShader::initializeBoxPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  ComPtr<ID3DBlob> vs = compileShader(_boxVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(_boxPixelShaderSource, "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.DepthStencilState.StencilEnable = TRUE;
  // Match OpenGL glStencilFunc(EQUAL, 1, 0x1) / Metal readMask 0x1.
  psoDesc.DepthStencilState.StencilReadMask = 0x1;
  psoDesc.DepthStencilState.StencilWriteMask = 0x1;
  psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
  psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_ZERO;
  psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_ZERO;
  psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
  psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace;
  psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_boxPso))))
  {
    std::cerr << "DirectXBondShader: failed to create box PSO";
    return;
  }
  _boxPsoReady = true;
}

void DirectXBondShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                   DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializeInternalPSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeExternalPSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeStencilPSO(device, rootSignature, rtvFormat, dsvFormat);
  initializeBoxPSO(device, rootSignature, rtvFormat, dsvFormat);
  uploadBoxMesh(device);
}

void DirectXBondShader::uploadBoxMesh(ID3D12Device *device)
{
  if (!device)
    return;
  CubeGeometry cube;
  const auto vertices = cube.vertices();
  const auto indices = cube.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _boxMesh,
                                          vertices.data(), vertices.size() * sizeof(RKVertex),
                                          sizeof(RKVertex),
                                          indices.data(), indices.size() * sizeof(short));
}

void DirectXBondShader::setRenderStructures(std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXBondShader::deleteBuffers()
{
  _internalBuffers.clear();
  _externalBuffers.clear();
}

void DirectXBondShader::generateBuffers()
{
  _internalBuffers.resize(_renderStructures.size());
  _externalBuffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    _internalBuffers[i].resize(_renderStructures[i].size());
    _externalBuffers[i].resize(_renderStructures[i].size());
  }
}

void DirectXBondShader::uploadInstances(ID3D12Device *device, MeshBuffers &bufs,
                                        const std::vector<RKInPerInstanceAttributesBonds> &instances)
{
  bufs = MeshBuffers{};
  DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                             bufs.instanceCount, instances.data(), instances.size(),
                                             sizeof(RKInPerInstanceAttributesBonds));
}

void DirectXBondShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  if (!_boxMesh.vertexBuffer)
    uploadBoxMesh(device);

  CappedCylinderSingleBondGeometry singleBondCylinder(1.0, 41);
  CappedCylinderDoubleBondGeometry doubleBondCylinder(1.0, 41);
  CappedCylinderPartialDoubleBondGeometry partialDoubleBondCylinder(1.0, 41);
  CappedCylinderTripleBondGeometry tripleBondCylinder(1.0, 41);
  const auto singleVertices = singleBondCylinder.vertices();
  const auto singleIndices = singleBondCylinder.indices();
  const auto doubleVertices = doubleBondCylinder.vertices();
  const auto doubleIndices = doubleBondCylinder.indices();
  const auto partialVertices = partialDoubleBondCylinder.vertices();
  const auto partialIndices = partialDoubleBondCylinder.indices();
  const auto tripleVertices = tripleBondCylinder.vertices();
  const auto tripleIndices = tripleBondCylinder.indices();

  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshSingle,
                                          singleVertices.data(), singleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          singleIndices.data(), singleIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshDouble,
                                          doubleVertices.data(), doubleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          doubleIndices.data(), doubleIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshPartialDouble,
                                          partialVertices.data(), partialVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          partialIndices.data(), partialIndices.size() * sizeof(short));
  DirectXDeviceHelpers::uploadIndexedMesh(device, _meshTriple,
                                          tripleVertices.data(), tripleVertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          tripleIndices.data(), tripleIndices.size() * sizeof(short));

  const int32_t singleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::singleBond);
  const int32_t doubleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::doubleBond);
  const int32_t partialDoubleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::partialDoubleBond);
  const int32_t tripleBondType = static_cast<typename std::underlying_type<SKAsymmetricBond::SKBondType>::type>(
      SKAsymmetricBond::SKBondType::tripleBond);

  auto reloadSet = [&](bool internal) {
    auto &bufferSet = internal ? _internalBuffers : _externalBuffers;
    for (size_t i = 0; i < _renderStructures.size(); ++i)
    {
      for (size_t j = 0; j < _renderStructures[i].size(); ++j)
      {
        StructureBondBuffers &bufs = bufferSet[i][j];
        bufs = StructureBondBuffers{};

        auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
        if (!source)
          continue;

        std::vector<RKInPerInstanceAttributesBonds> bondInstanceData =
            internal ? source->renderInternalBonds() : source->renderExternalBonds();

        // Unity (VdW) draws the combined mesh; CPK draws per-type meshes. Do not
        // upload both — that doubled GPU memory and CPU time on large proteins.
        if (source->isUnity())
        {
          uploadInstances(device, bufs.all, bondInstanceData);
        }
        else
        {
          std::vector<RKInPerInstanceAttributesBonds> singleBondInstanceData;
          std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(singleBondInstanceData),
                       [singleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == singleBondType; });
          uploadInstances(device, bufs.single, singleBondInstanceData);

          std::vector<RKInPerInstanceAttributesBonds> doubleBondInstanceData;
          std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(doubleBondInstanceData),
                       [doubleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == doubleBondType; });
          uploadInstances(device, bufs.doubleBond, doubleBondInstanceData);

          std::vector<RKInPerInstanceAttributesBonds> partialDoubleBondInstanceData;
          std::copy_if(bondInstanceData.begin(), bondInstanceData.end(),
                       std::back_inserter(partialDoubleBondInstanceData),
                       [partialDoubleBondType](const RKInPerInstanceAttributesBonds &b) {
                         return b.type == partialDoubleBondType;
                       });
          uploadInstances(device, bufs.partialDouble, partialDoubleBondInstanceData);

          std::vector<RKInPerInstanceAttributesBonds> tripleBondInstanceData;
          std::copy_if(bondInstanceData.begin(), bondInstanceData.end(), std::back_inserter(tripleBondInstanceData),
                       [tripleBondType](const RKInPerInstanceAttributesBonds &b) { return b.type == tripleBondType; });
          uploadInstances(device, bufs.triple, tripleBondInstanceData);
        }
      }
    }
  };

  reloadSet(true);
  reloadSet(false);
}

void DirectXBondShader::drawMesh(ID3D12GraphicsCommandList *commandList,
                                 const DirectXDeviceHelpers::IndexedMesh &mesh,
                                 const MeshBuffers &bufs)
{
  if (mesh.indexCount == 0 || bufs.instanceCount == 0
      || !mesh.vertexBuffer || !mesh.indexBuffer || !bufs.instanceBuffer)
    return;

  D3D12_VERTEX_BUFFER_VIEW views[2] = { mesh.vbv, bufs.instanceVbv };
  commandList->IASetVertexBuffers(0, 2, views);
  commandList->IASetIndexBuffer(&mesh.ibv);
  commandList->DrawIndexedInstanced(mesh.indexCount, bufs.instanceCount, 0, 0, 0);
}

void DirectXBondShader::paintBondSet(ID3D12GraphicsCommandList *commandList,
                                     D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                     UINT structureCBVStride,
                                     const std::vector<std::vector<StructureBondBuffers>> &buffers) const
{
  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
      const StructureBondBuffers &bufs = buffers[i][j];
      if (source && source->drawBonds() && _renderStructures[i][j]->isVisible())
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        if (source->isUnity())
        {
          drawMesh(commandList, _meshSingle, bufs.all);
        }
        else
        {
          const UINT typedCount = bufs.single.instanceCount + bufs.doubleBond.instanceCount
                                + bufs.partialDouble.instanceCount + bufs.triple.instanceCount;
          if (typedCount > 0)
          {
            drawMesh(commandList, _meshSingle, bufs.single);
            drawMesh(commandList, _meshDouble, bufs.doubleBond);
            drawMesh(commandList, _meshPartialDouble, bufs.partialDouble);
            drawMesh(commandList, _meshTriple, bufs.triple);
          }
          else
          {
            drawMesh(commandList, _meshSingle, bufs.all);
          }
        }
      }
      ++index;
    }
  }
}

void DirectXBondShader::drawPickGeometry(ID3D12GraphicsCommandList *commandList,
                                         D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                         UINT structureCBVStride,
                                         bool internal) const
{
  paintBondSet(commandList, structureCBVBase, structureCBVStride,
               internal ? _internalBuffers : _externalBuffers);
}

void DirectXBondShader::paintExternalStencilAndBox(ID3D12GraphicsCommandList *commandList,
                                                   D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                   UINT structureCBVStride)
{
  if (!_stencilPsoReady || !_stencilPso || !_boxPsoReady || !_boxPso
      || !_boxMesh.vertexBuffer || _boxMesh.indexCount == 0)
    return;

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderBondSource *>(_renderStructures[i][j].get());
      const StructureBondBuffers &bufs = _externalBuffers[i][j];
      if (source && source->drawBonds() && _renderStructures[i][j]->isVisible()
          && (bufs.all.instanceCount > 0
              || bufs.single.instanceCount + bufs.doubleBond.instanceCount
               + bufs.partialDouble.instanceCount + bufs.triple.instanceCount > 0))
      {
        const D3D12_GPU_VIRTUAL_ADDRESS structureCBV =
            structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride;
        commandList->SetGraphicsRootConstantBufferView(1, structureCBV);

        // Pass B: invert stencil on clipped external bonds (front + back).
        commandList->SetPipelineState(_stencilPso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->OMSetStencilRef(1);

        if (source->isUnity())
        {
          drawMesh(commandList, _meshSingle, bufs.all);
        }
        else
        {
          const UINT typedCount = bufs.single.instanceCount + bufs.doubleBond.instanceCount
                                + bufs.partialDouble.instanceCount + bufs.triple.instanceCount;
          if (typedCount > 0)
          {
            drawMesh(commandList, _meshSingle, bufs.single);
            drawMesh(commandList, _meshDouble, bufs.doubleBond);
            drawMesh(commandList, _meshPartialDouble, bufs.partialDouble);
            drawMesh(commandList, _meshTriple, bufs.triple);
          }
          else
          {
            drawMesh(commandList, _meshSingle, bufs.all);
          }
        }

        // Pass C: fill caps where stencil == 1.
        commandList->SetPipelineState(_boxPso.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->OMSetStencilRef(1);
        commandList->IASetVertexBuffers(0, 1, &_boxMesh.vbv);
        commandList->IASetIndexBuffer(&_boxMesh.ibv);
        commandList->DrawIndexedInstanced(_boxMesh.indexCount, 1, 0, 0, 0);
      }
      ++index;
    }
  }
}

void DirectXBondShader::paint(ID3D12GraphicsCommandList *commandList,
                              D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                              UINT structureCBVStride)
{
  if (_psoReady && _pso)
  {
    commandList->SetPipelineState(_pso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _internalBuffers);
  }

  if (_externalPsoReady && _externalPso)
  {
    commandList->SetPipelineState(_externalPso.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _externalBuffers);
  }

  paintExternalStencilAndBox(commandList, structureCBVBase, structureCBVStride);
}

const std::string DirectXBondShader::_vertexShaderSource =
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
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
)foo") + std::string(kCylinderBody) + std::string(R"foo(
  return output;
}
)foo");

const std::string DirectXBondShader::_externalVertexShaderSource =
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
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
  // D3D allows only SV_ClipDistance0/1; pack 6 planes into float4 + float2.
  float4 clip0123 : SV_ClipDistance0;
  float2 clip45 : SV_ClipDistance1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
)foo") + std::string(kCylinderBody) + std::string(R"foo(
  float4 objectPos = float4(world, 1.0);
  output.clip0123 = float4(
      dot(structureUniforms.clipPlaneBack, objectPos),
      dot(structureUniforms.clipPlaneBottom, objectPos),
      dot(structureUniforms.clipPlaneLeft, objectPos),
      dot(structureUniforms.clipPlaneFront, objectPos));
  output.clip45 = float2(
      dot(structureUniforms.clipPlaneTop, objectPos),
      dot(structureUniforms.clipPlaneRight, objectPos));
  return output;
}
)foo");

const std::string DirectXBondShader::_stencilVertexShaderSource =
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
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  float4 clip0123 : SV_ClipDistance0;
  float2 clip45 : SV_ClipDistance1;
};

float frontFacing(float4 pos0, float4 pos1, float4 pos2)
{
  return pos0.x * pos1.y - pos1.x * pos0.y + pos1.x * pos2.y - pos2.x * pos1.y + pos2.x * pos0.y - pos0.x * pos2.y;
}

VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 scale = input.instanceScale;
  float4 pos = float4(input.vertexPosition.xyz, 1.0);
  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  if (pos1.w < 0.0 || pos2.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
    return output;
  }

  float3 dr = (pos1 - pos2).xyz;
  float bondLength = max(length(dr), 1e-5);
  scale.x = max(structureUniforms.bondScaling, 0.02);
  scale.y = bondLength;
  scale.z = max(structureUniforms.bondScaling, 0.02);
  scale.w = 1.0;

  dr /= bondLength;
  float3 v1 = normalize(abs(dr.x) > abs(dr.z) ? float3(-dr.y, dr.x, 0.0) : float3(0.0, -dr.z, dr.y));
  float3 v2 = normalize(cross(dr, v1));
  float3 c0 = -v1;
  float3 c1 = -dr;
  float3 c2 = -v2;

  float3 local = (scale * pos).xyz;
  float3 world = pos1.xyz + c0 * local.x + c1 * local.y + c2 * local.z;
  float4 worldPos = float4(world, 1.0);

  float4 clip = mul(frameUniforms.mvpMatrix, mul(structureUniforms.modelMatrix, worldPos));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;

  float4 objectPos = worldPos;
  float v_clipDistLeft = dot(structureUniforms.clipPlaneLeft, objectPos);
  float v_clipDistRight = dot(structureUniforms.clipPlaneRight, objectPos);
  float v_clipDistTop = dot(structureUniforms.clipPlaneTop, objectPos);
  float v_clipDistBottom = dot(structureUniforms.clipPlaneBottom, objectPos);
  float v_clipDistFront = dot(structureUniforms.clipPlaneFront, objectPos);
  float v_clipDistBack = dot(structureUniforms.clipPlaneBack, objectPos);

  float4x4 mvpMatrix = mul(frameUniforms.mvpMatrix, structureUniforms.modelMatrix);
  float4 boxPosition0 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(0.0, 0.0, 0.0, 1.0)));
  float4 boxPosition1 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(1.0, 0.0, 0.0, 1.0)));
  float4 boxPosition2 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(1.0, 1.0, 0.0, 1.0)));
  float4 boxPosition3 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(0.0, 1.0, 0.0, 1.0)));
  float4 boxPosition4 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(0.0, 0.0, 1.0, 1.0)));
  float4 boxPosition5 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(1.0, 0.0, 1.0, 1.0)));
  float4 boxPosition6 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(1.0, 1.0, 1.0, 1.0)));
  float4 boxPosition7 = mul(mvpMatrix, mul(structureUniforms.boxMatrix, float4(0.0, 1.0, 1.0, 1.0)));

  boxPosition0 /= boxPosition0.w;
  boxPosition1 /= boxPosition1.w;
  boxPosition2 /= boxPosition2.w;
  boxPosition3 /= boxPosition3.w;
  boxPosition4 /= boxPosition4.w;
  boxPosition5 /= boxPosition5.w;
  boxPosition6 /= boxPosition6.w;
  boxPosition7 /= boxPosition7.w;

  float leftFrontfacing = frontFacing(boxPosition0, boxPosition3, boxPosition7);
  float rightFrontfacing = frontFacing(boxPosition1, boxPosition5, boxPosition2);
  float topFrontFacing = frontFacing(boxPosition3, boxPosition2, boxPosition7);
  float bottomFrontFacing = frontFacing(boxPosition0, boxPosition4, boxPosition1);
  float frontFrontFacing = frontFacing(boxPosition4, boxPosition6, boxPosition5);
  float backFrontFacing = frontFacing(boxPosition0, boxPosition1, boxPosition2);

  // OpenGL/Metal use (frontFacing < 0) to enable clip on camera-facing cell planes.
  // Direct3D flips projection m22, which flips screen-space winding, so the test is inverted.
  output.clip0123 = float4(
      (leftFrontfacing < 0.0) ? v_clipDistLeft : 0.0,
      (rightFrontfacing < 0.0) ? v_clipDistRight : 0.0,
      (topFrontFacing < 0.0) ? v_clipDistTop : 0.0,
      (bottomFrontFacing < 0.0) ? v_clipDistBottom : 0.0);
  output.clip45 = float2(
      (frontFrontFacing < 0.0) ? v_clipDistFront : 0.0,
      (backFrontFacing < 0.0) ? v_clipDistBack : 0.0);
  return output;
}
)foo");

const std::string DirectXBondShader::_pixelShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 color1 : COLOR0;
  nointerpolation float4 color2 : COLOR1;
  float4 mixValue : COLOR2;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float3 V = normalize(input.V);
  float3 R = reflect(-L, N);

  float4 ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
  float4 specular = pow(max(dot(R, V), 0.0), lightUniforms.lights[0].shininess + structureUniforms.bondShininess)
                    * lightUniforms.lights[0].specular * structureUniforms.bondSpecularColor;
  float d = max(dot(N, L), 0.0);
  float4 diffuse = float4(d, d, d, 1.0);
  float t = clamp((input.mixValue.y - input.mixValue.x) / (input.mixValue.z - input.mixValue.x), 0.0, 1.0);

  if (structureUniforms.bondColorMode == 0)
    diffuse *= structureUniforms.bondDiffuseColor;
  else if (structureUniforms.bondColorMode == 1)
    diffuse *= (t < 0.5 ? input.color1 : input.color2);
  else if (structureUniforms.bondColorMode == 2)
    diffuse *= lerp(input.color1, input.color2, smoothstep(0.0, 1.0, t));

  float4 color = float4(ambient.xyz + diffuse.xyz + specular.xyz, 1.0);

  if (structureUniforms.bondHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.bondHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.bondHue;
  hsv.y = hsv.y * structureUniforms.bondSaturation;
  hsv.z = hsv.z * structureUniforms.bondValue;
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");

const std::string DirectXBondShader::_stencilPixelShaderSource =
std::string(R"foo(
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
  return float4(0.0, 0.0, 0.0, 0.0);
}
)foo");

const std::string DirectXBondShader::_boxVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
  float4 vertexPosition : POSITION;
  float4 vertexNormal : NORMAL;
};

struct VSOutput
{
  float4 position : SV_POSITION;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  output.ambient = lightUniforms.lights[0].ambient * structureUniforms.bondAmbientColor;
  output.diffuse = lightUniforms.lights[0].diffuse * structureUniforms.bondDiffuseColor;

  output.N = mul(frameUniforms.normalMatrix, mul(structureUniforms.modelMatrix, input.vertexNormal)).xyz;

  float4 P = mul(frameUniforms.viewMatrix,
                 mul(structureUniforms.modelMatrix, mul(structureUniforms.boxMatrix, input.vertexPosition)));
  output.L = (lightUniforms.lights[0].position - P * lightUniforms.lights[0].position.w).xyz;
  output.V = -P.xyz;

  float4 clip = mul(frameUniforms.projectionMatrix, P);
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;
  return output;
}
)foo");

const std::string DirectXBondShader::_boxPixelShaderSource =
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
std::string(R"foo(
struct PSInput
{
  float4 position : SV_POSITION;
  nointerpolation float4 ambient : COLOR0;
  nointerpolation float4 diffuse : COLOR1;
  float3 N : NORMAL0;
  float3 L : TEXCOORD0;
  float3 V : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET
{
  float3 N = normalize(input.N);
  float3 L = normalize(input.L);
  float4 ambient = input.ambient;
  float4 diffuse = max(dot(N, L), 0.0) * input.diffuse;
  float4 color = float4(ambient.xyz + diffuse.xyz, 1.0);

  if (structureUniforms.atomHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.atomHDRExposure);
    vLdrColor.a = 1.0;
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.bondHue;
  hsv.y = hsv.y * structureUniforms.bondSaturation;
  hsv.z = hsv.z * structureUniforms.bondValue;
  return float4(hsv2rgb(hsv), 1.0);
}
)foo");
