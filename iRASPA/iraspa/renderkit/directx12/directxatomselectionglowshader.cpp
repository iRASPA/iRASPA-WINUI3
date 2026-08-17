/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "directxatomselectionglowshader.h"
#include <iostream>
#include "directxatomimposter.h"
#include "directxatomselectionworleynoise3dshader.h"
#include "directxdevicehelpers.h"
#include "directxuniformstringliterals.h"
#include <cstddef>

void DirectXAtomSelectionGlowShader::loadShader(ID3D12Device * /*device*/)
{
}

void DirectXAtomSelectionGlowShader::initialize(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                                                DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, true);
  initializePSO(device, rootSignature, rtvFormat, dsvFormat, false);
}

void DirectXAtomSelectionGlowShader::initializePSO(ID3D12Device *device, ID3D12RootSignature *rootSignature,
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
  // The glow is drawn into the 1x glow target against the resolved scene depth, never into the
  // multisampled scene, so it stays single-sampled whatever the scene's sample count is.
  psoDesc.SampleDesc.Count = 1;

  ComPtr<ID3D12PipelineState> &pso = orthographic ? _orthographicPso : _perspectivePso;
  bool &ready = orthographic ? _orthographicPsoReady : _perspectivePsoReady;
  if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
  {
    std::cerr << "DirectXAtomSelectionGlowShader: failed to create "
              << (orthographic ? "orthographic" : "perspective") << " PSO";
    return;
  }
  ready = true;
}

void DirectXAtomSelectionGlowShader::setRenderStructures(
    std::vector<std::vector<std::shared_ptr<RKRenderObject>>> structures)
{
  _renderStructures = std::move(structures);
}

void DirectXAtomSelectionGlowShader::reloadData(ID3D12Device * /*device*/)
{
}

void DirectXAtomSelectionGlowShader::paint(ID3D12GraphicsCommandList *commandList,
                                           D3D12_GPU_VIRTUAL_ADDRESS structureCBVBase,
                                           UINT structureCBVStride,
                                           const DirectXAtomSelectionWorleyNoise3DShader &instanceSource,
                                           bool orthographic)
{
  ID3D12PipelineState *pso = orthographic ? (_orthographicPsoReady ? _orthographicPso.Get() : nullptr)
                                          : (_perspectivePsoReady ? _perspectivePso.Get() : nullptr);
  if (!pso || !instanceSource.isQuadReady())
    return;

  commandList->SetPipelineState(pso);
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

  const D3D12_VERTEX_BUFFER_VIEW quadVbv = instanceSource.quadVbv();
  const D3D12_INDEX_BUFFER_VIEW quadIbv = instanceSource.quadIbv();
  const UINT quadIndexCount = instanceSource.quadIndexCount();

  size_t index = 0;
  for (size_t i = 0; i < _renderStructures.size(); ++i)
  {
    for (size_t j = 0; j < _renderStructures[i].size(); ++j)
    {
      auto *source = dynamic_cast<RKRenderAtomSource *>(_renderStructures[i][j].get());
      if (source && source->atomSelectionStyle() == RKSelectionStyle::glow
          && source->drawAtoms() && _renderStructures[i][j]->isVisible()
          && instanceSource.isInstanceReady(i, j))
      {
        commandList->SetGraphicsRootConstantBufferView(
            1, structureCBVBase + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(index) * structureCBVStride);

        D3D12_VERTEX_BUFFER_VIEW views[2] = { quadVbv, instanceSource.instanceVbv(i, j) };
        commandList->IASetVertexBuffers(0, 2, views);
        commandList->IASetIndexBuffer(&quadIbv);
        commandList->DrawIndexedInstanced(quadIndexCount, instanceSource.instanceCount(i, j), 0, 0, 0);
      }
      ++index;
    }
  }
}

std::string DirectXAtomSelectionGlowShader::vertexShaderSource(bool orthographic)
{
  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
DirectXAtomImposter::SelectionVertexInputStringLiteral +
"struct VSOutput\n{" + DirectXAtomImposter::SelectionVaryingsStringLiteral + "};\n" +
DirectXAtomImposter::selectionVertexShaderBody(orthographic);
}

std::string DirectXAtomSelectionGlowShader::pixelShaderSource(bool orthographic)
{
  return
DirectXUniformStringLiterals::FrameUniformBlockStringLiteral +
DirectXUniformStringLiterals::StructureUniformBlockStringLiteral +
DirectXUniformStringLiterals::LightUniformBlockStringLiteral +
"struct PSInput\n{" + DirectXAtomImposter::SelectionVaryingsStringLiteral + "};\n" +
DirectXAtomImposter::DepthOutputStringLiteral +
std::string(R"foo(
PSOutput PSMain(PSInput input)
{
  PSOutput output;
)foo") + DirectXAtomImposter::hitStringLiteral(orthographic) + std::string(R"foo(
  // The glow is a flat silhouette that the blur pass turns into a halo, so the hit only
  // contributes its depth and the atom's own colour.
  output.color = float4(structureUniforms.atomSelectionIntensity * (input.ambient.xyz + input.diffuse.xyz), 1.0);
  return output;
}
)foo");
}
