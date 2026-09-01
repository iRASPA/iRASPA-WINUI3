/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
   Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxbondshader.h"
#include <iostream>
#include "directxbondimposter.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "skasymmetricbond.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace
{
// Shading of the ray-traced surface point, shared by the internal and external pixel shaders.
const char *kImposterShading = R"foo(
  float3 V = normalize(-pos);

  LightingWeights lighting = accumulateLighting(N, V, float4(pos, 1.0),
                                                structureUniforms.bondShininess,
                                                shadowMaskAtFragment(input.position));

  float4 ambient = float4(lighting.ambient, 1.0) * input.ambient;
  float4 specular = float4(lighting.specular, 1.0) * input.specular;
  // The colours the vertex shader hands on are materials now, so the light rig arrives here
  // instead: mode 0 takes the structure's bond colour, the other two the per-atom colours.
  float4 diffuse = float4(lighting.diffuse, 1.0);

  if (structureUniforms.bondColorMode == 0)
    diffuse *= structureUniforms.bondDiffuseColor;
  else if (structureUniforms.bondColorMode == 1)
    diffuse *= (ct < 0.5 ? input.color1 : input.color2);
  else if (structureUniforms.bondColorMode == 2)
    diffuse *= lerp(input.color1, input.color2, smoothstep(0.0, 1.0, ct));

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
  output.color = float4(hsv2rgb(hsv), 1.0);
)foo";
}

void DirectXBondShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXBondShader::initializeImposterPSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                              DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                              bool external, bool perSample)
{
  ComPtr<ID3DBlob> vs = compileShader(_imposterVertexShaderSource, "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(imposterPixelShaderSource(external, perSample), "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[DirectXBondImposter::shadingInputLayoutSize];
  DirectXBondImposter::fillShadingInputLayout(inputLayout);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  // The hull is built in the vertex shader with view-dependent winding, so it must not be culled.
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  DirectXDeviceHelpers::recordEdgeCueingInStencil(psoDesc.DepthStencilState);
  psoDesc.InputLayout = { inputLayout, DirectXBondImposter::shadingInputLayoutSize };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  ComPtr<ID3D12PipelineState> &pso = external ? (perSample ? _externalImposterPso : _externalImposterPerPixelPso)
                                              : (perSample ? _imposterPso : _imposterPerPixelPso);
  bool &ready = external ? (perSample ? _externalImposterPsoReady : _externalImposterPerPixelPsoReady)
                         : (perSample ? _imposterPsoReady : _imposterPerPixelPsoReady);

  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "DirectXBondShader: failed to create " << (external ? "external " : "")
              << (perSample ? "per-sample" : "per-pixel") << " imposter PSO";
    return;
  }
  ready = true;
}

void DirectXBondShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                   DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializeImposterPSO(device, rootSignature, rtvFormat, dsvFormat, false, true);
  initializeImposterPSO(device, rootSignature, rtvFormat, dsvFormat, false, false);
  initializeImposterPSO(device, rootSignature, rtvFormat, dsvFormat, true, true);
  initializeImposterPSO(device, rootSignature, rtvFormat, dsvFormat, true, false);
  _hulls.upload(device);
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

  if (!_hulls.ready())
    _hulls.upload(device);

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

        // Unity (VdW) draws every bond as a single cylinder; CPK splits them per type. Do not
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

        // The bonds take the cues of the atoms they join, as in Cocoa: one setting reads as one
        // decision about the molecule, and a contour that stopped at every atom would look drawn
        // around the spheres rather than around the molecule.
        if (auto *atoms = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get()))
          commandList->OMSetStencilRef(RKEdgeCueingParameters::stencilValue(atoms->atomEdgeCueing()));

        if (source->isUnity())
        {
          drawMesh(commandList, _hulls.single, bufs.all);
        }
        else
        {
          const UINT typedCount = bufs.single.instanceCount + bufs.doubleBond.instanceCount
                                + bufs.partialDouble.instanceCount + bufs.triple.instanceCount;
          if (typedCount > 0)
          {
            drawMesh(commandList, _hulls.single, bufs.single);
            drawMesh(commandList, _hulls.doubleBond, bufs.doubleBond);
            drawMesh(commandList, _hulls.partialDouble, bufs.partialDouble);
            drawMesh(commandList, _hulls.triple, bufs.triple);
          }
          else
          {
            drawMesh(commandList, _hulls.single, bufs.all);
          }
        }
      }
      ++index;
    }
  }

  // Back to what is not a structure, for the passes that follow: the reference outlives this one.
  commandList->OMSetStencilRef(0);
}

void DirectXBondShader::drawPickGeometry(ID3D12GraphicsCommandList *commandList,
                                         D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                         UINT structureCBVStride,
                                         bool internal) const
{
  paintBondSet(commandList, structureCBVBase, structureCBVStride,
               internal ? _internalBuffers : _externalBuffers);
}

void DirectXBondShader::paint(ID3D12GraphicsCommandList *commandList,
                              D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                              UINT structureCBVStride)
{
  if (!_hulls.ready())
    return;

  const bool perSample = DirectXDeviceHelpers::perSampleImposterShading();

  ID3D12PipelineState *internalPso = perSample ? (_imposterPsoReady ? _imposterPso.Get() : nullptr)
                                               : (_imposterPerPixelPsoReady ? _imposterPerPixelPso.Get() : nullptr);
  if (internalPso)
  {
    commandList->SetPipelineState(internalPso);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _internalBuffers);
  }

  ID3D12PipelineState *externalPso =
      perSample ? (_externalImposterPsoReady ? _externalImposterPso.Get() : nullptr)
                : (_externalImposterPerPixelPsoReady ? _externalImposterPerPixelPso.Get() : nullptr);
  if (externalPso)
  {
    commandList->SetPipelineState(externalPso);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    paintBondSet(commandList, structureCBVBase, structureCBVStride, _externalBuffers);
  }
}

const std::string DirectXBondShader::_imposterVertexShaderSource =
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
std::string(R"foo(
struct VSInput
{
)foo") + DirectXBondImposter::HullVertexInputStringLiteral + std::string(R"foo(
  float4 instanceColor1 : INSTANCECOLOR1;
  float4 instanceColor2 : INSTANCECOLOR2;
  float4 instanceScale : INSTANCESCALE;
};

struct VSOutput
{
)foo") + DirectXBondImposter::shadingVaryings(false) + std::string(R"foo(
};
)foo") + DirectXBondImposter::HullStringLiteral + std::string(R"foo(
VSOutput VSMain(VSInput input)
{
  VSOutput output;

  float4 pos1 = input.instancePosition1;
  float4 pos2 = input.instancePosition2;

  // The material alone: the light rig is summed over in the pixel shader, since a sum over lights
  // that a shadow mask gates per pixel cannot be folded into one interpolated colour here.
  output.ambient = structureUniforms.bondAmbientColor;
  output.specular = structureUniforms.bondSpecularColor;
  float4 diffuseColor = (structureUniforms.bondColorMode == 0) ? structureUniforms.bondDiffuseColor
                                                               : structureUniforms.atomDiffuseColor;
  output.color1 = diffuseColor * input.instanceColor1;
  output.color2 = diffuseColor * input.instanceColor2;

  BondImposterHull hull = bondImposterHull(pos1, pos2, input.vertexPosition.xyz,
                                           input.vertexNormal.xy, input.vertexPosition.w, 1.0);
  output.frag_pos = hull.posEye;
  output.pointA = hull.a;
  output.pointB = hull.b;
  output.radius = hull.radius;

  float4 clip = mul(frameUniforms.projectionMatrix, float4(hull.posEye, 1.0));
  clip.z = clip.z * 0.5f + clip.w * 0.5f;
  output.position = clip;

  // Bond visibility is encoded in position.w (±1), same as atoms; collapse the whole hull.
  if (pos1.w < 0.0 || pos2.w < 0.0)
  {
    output.position = float4(0.0, 0.0, 0.0, 0.0);
  }
  return output;
}
)foo");

std::string DirectXBondShader::imposterPixelShaderSource(bool external, bool perSample)
{
  const std::string intersect = external ? DirectXBondImposter::ClippedIntersectStringLiteral
                                         : DirectXBondImposter::IntersectStringLiteral;

  // External bonds are clipped at the unit cell; the flat caps at the cell boundary come out of
  // the same intersection, so no stencil pass is needed to fill them.
  const std::string trace = external
      ? DirectXBondImposter::ToStructureStringLiteral +
        std::string("  float t = clippedCylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius,\n"
                    "                                     toStructure, N, ct);\n")
      : std::string("  float t = cylinderIntersect(ro, rd, input.pointA, input.pointB, input.radius, N, ct);\n");

  return DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
         DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
         DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
         DirectXUniformStringLiterals::LightingStringLiteral +
         DirectXUniformStringLiterals::RGBHSVStringLiteral +
         std::string("\nstruct PSInput\n{\n") + DirectXBondImposter::shadingVaryings(perSample) +
         std::string("};\n") +
         DirectXBondImposter::DepthOutputStringLiteral +
         intersect +
         std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXBondImposter::RayStringLiteral + std::string(R"foo(
  float3 N;
  float ct;
)foo") + trace + std::string(R"foo(
  if (t < 0.0) discard;

  float3 pos = ro + t * rd;
)foo") + DirectXBondImposter::WriteDepthStringLiteral + std::string(kImposterShading) + std::string(R"foo(
  return output;
}
)foo");
}
