/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomselectionworleynoise3dshader.h"
#include <iostream>
#include "directxatomimposter.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include "geometry/quadgeometry.h"
#include <cstddef>

void DirectXAtomSelectionWorleyNoise3DShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomSelectionWorleyNoise3DShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                         DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, true);
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, false);
}

void DirectXAtomSelectionWorleyNoise3DShader::initializePSO(ID3D12Device *device,
                                                            ID3D12RootSignature *rootSignature,
                                                            DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                                                            bool orthographic)
{
  ComPtr<ID3DBlob> vs = compileShader(vertexShaderSource(orthographic), "VSMain", "vs_5_0");
  ComPtr<ID3DBlob> ps = compileShader(pixelShaderSource(orthographic), "PSMain", "ps_5_0");
  if (!vs || !ps)
    return;

  D3D12_INPUT_ELEMENT_DESC inputLayout[DirectXAtomImposter::selectionInputLayoutSize];
  DirectXAtomImposter::fillSelectionInputLayout(inputLayout);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature;
  psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
  psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  psoDesc.InputLayout = { inputLayout, DirectXAtomImposter::selectionInputLayoutSize };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = rtvFormat;
  psoDesc.DSVFormat = dsvFormat;
  psoDesc.SampleDesc = DirectXDeviceHelpers::sceneSampleDesc();

  ComPtr<ID3D12PipelineState> &pso = orthographic ? _orthographicPso : _perspectivePso;
  bool &ready = orthographic ? _orthographicPsoReady : _perspectivePsoReady;
  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "DirectXAtomSelectionWorleyNoise3DShader: failed to create "
              << (orthographic ? "orthographic" : "perspective") << " PSO";
    return;
  }
  ready = true;
}

void DirectXAtomSelectionWorleyNoise3DShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  deleteBuffers();
  _renderStructures = std::move(structures);
  generateBuffers();
}

void DirectXAtomSelectionWorleyNoise3DShader::deleteBuffers()
{
  _buffers.clear();
}

void DirectXAtomSelectionWorleyNoise3DShader::generateBuffers()
{
  _buffers.resize(_renderStructures.size());
  for (size_t i = 0; i < _renderStructures.size(); ++i)
    _buffers[i].resize(_renderStructures[i].size());
}

void DirectXAtomSelectionWorleyNoise3DShader::reloadData(ID3D12Device *device)
{
  if (!device)
    return;

  QuadGeometry quad;
  const auto vertices = quad.vertices();
  const auto indices = quad.indices();
  DirectXDeviceHelpers::uploadIndexedMesh(device, _quadMesh,
                                          vertices.data(), vertices.size() * sizeof(RKVertex), sizeof(RKVertex),
                                          indices.data(), indices.size() * sizeof(short));

  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      StructureBuffers &bufs = _buffers[i][j];
      bufs = StructureBuffers{};

      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (!source)
        continue;

      std::vector<RKInPerInstanceAttributesAtoms> atomData = source->renderSelectedAtoms();
      DirectXDeviceHelpers::uploadInstanceBuffer(device, bufs.instanceBuffer, bufs.instanceVbv,
                                                 bufs.instanceCount, atomData.data(), atomData.size(),
                                                 sizeof(RKInPerInstanceAttributesAtoms));
    }
  }
}

bool DirectXAtomSelectionWorleyNoise3DShader::isInstanceReady(size_t i, size_t j) const
{
  return i < _buffers.size() && j < _buffers[i].size()
      && _buffers[i][j].instanceBuffer != nullptr
      && _buffers[i][j].instanceCount > 0;
}

UINT DirectXAtomSelectionWorleyNoise3DShader::instanceCount(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return 0;
  return _buffers[i][j].instanceCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::instanceVbv(size_t i, size_t j) const
{
  if (i >= _buffers.size() || j >= _buffers[i].size())
    return {};
  return _buffers[i][j].instanceVbv;
}

bool DirectXAtomSelectionWorleyNoise3DShader::isQuadReady() const
{
  return _quadMesh.vertexBuffer && _quadMesh.indexBuffer && _quadMesh.indexCount > 0;
}

UINT DirectXAtomSelectionWorleyNoise3DShader::quadIndexCount() const
{
  return _quadMesh.indexCount;
}

D3D12_VERTEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::quadVbv() const
{
  return _quadMesh.vbv;
}

D3D12_INDEX_BUFFER_VIEW DirectXAtomSelectionWorleyNoise3DShader::quadIbv() const
{
  return _quadMesh.ibv;
}

bool DirectXAtomSelectionWorleyNoise3DShader::hasGlowWork() const
{
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->atomSelectionStyle() == RKSelectionStyle::glow
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && isInstanceReady(i, j))
        return true;
    }
  }
  return false;
}

void DirectXAtomSelectionWorleyNoise3DShader::paint(ID3D12GraphicsCommandList *commandList,
                                                    D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                                    UINT structureCBVStride,
                                                    bool orthographic)
{
  ID3D12PipelineState *pso = orthographic ? (_orthographicPsoReady ? _orthographicPso.Get() : nullptr)
                                          : (_perspectivePsoReady ? _perspectivePso.Get() : nullptr);
  if (!pso || !isQuadReady())
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->atomSelectionStyle() == RKSelectionStyle::WorleyNoise3D
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && isInstanceReady(i, j))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        D3D12_VERTEX_BUFFER_VIEW views[2] = { _quadMesh.vbv, _buffers[i][j].instanceVbv };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&_quadMesh.ibv);
        commandList->DrawIndexedInstanced(_quadMesh.indexCount, _buffers[i][j].instanceCount, 0, 0, 0);
      }
      ++index;
    }
  }
}

std::string DirectXAtomSelectionWorleyNoise3DShader::vertexShaderSource(bool orthographic)
{
  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXAtomImposter::SelectionVertexInputStringLiteral +
"struct VSOutput\n{" + DirectXAtomImposter::selectionVaryings(false) + "};\n" +
DirectXAtomImposter::selectionVertexShaderBody(orthographic);
}

std::string DirectXAtomSelectionWorleyNoise3DShader::pixelShaderSource(bool orthographic)
{
  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightingStringLiteral +
DirectXUniformStringLiterals::RGBHSVStringLiteral +
DirectXUniformStringLiterals::WorleyNoise3DStringLiteral +
"struct PSInput\n{" + DirectXAtomImposter::selectionVaryings(false) + "};\n" +
DirectXAtomImposter::DepthOutputStringLiteral +
std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXAtomImposter::hitStringLiteral(orthographic) + std::string(R"foo(
  // Unshadowed, as the overlay marks a selection rather than describing the scene's light.
  LightingWeights lighting = accumulateLighting(N, normalize(input.V), surfaceEyePosition,
                                                structureUniforms.atomShininess);
  float3 ambient = lighting.ambient * input.ambient.xyz;
  float3 diffuse = lighting.diffuse * input.diffuse.xyz;
  float3 specular = lighting.specular * input.specular.xyz;
)foo") + DirectXAtomImposter::ModelNormalStringLiteral + std::string(R"foo(
  float frequency = structureUniforms.atomSelectionWorleyNoise3DFrequency;
  float jitter = structureUniforms.atomSelectionWorleyNoise3DJitter;
  float2 F = cellular3D(frequency * float3(t1.x, t1.z, t1.y), jitter);
  float n = F.y - F.x;

  float4 color = n * float4(ambient + diffuse + specular, 1.0);

  if (structureUniforms.atomHDR != 0)
  {
    float4 vLdrColor = 1.0 - exp2(-color * structureUniforms.atomHDRExposure);
    color = vLdrColor;
  }

  float3 hsv = rgb2hsv(color.xyz);
  hsv.x = hsv.x * structureUniforms.atomHue;
  hsv.y = hsv.y * structureUniforms.atomSaturation;
  hsv.z = hsv.z * structureUniforms.atomValue;
  float bloomLevel = frameUniforms.bloomLevel * structureUniforms.atomSelectionIntensity;
  output.color = float4(hsv2rgb(hsv) * bloomLevel, bloomLevel);
  return output;
}
)foo");
}
