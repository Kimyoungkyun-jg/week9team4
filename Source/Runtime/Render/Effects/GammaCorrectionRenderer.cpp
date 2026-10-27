#include "EnginePCH.h"
#include "Render/Effects/GammaCorrectionRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

void FGammaCorrectionRenderer::Init()
{
	PipelineState.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/GammaCorrectionShader.hlsl");
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.RasterizerState = ERasterizerState::SolidNone;
	PipelineState.BlendState = EBlendState::Opaque;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
}

bool FGammaCorrectionRenderer::PrepareOutput(FTexture2D* ColorTarget)
{
	if (!ColorTarget || !PipelineState.Shader || !PipelineState.Shader->VertexShader ||
		!PipelineState.Shader->PixelShader) return false;
	const uint32 Width = ColorTarget->GetWidth();
	const uint32 Height = ColorTarget->GetHeight();
	if (!Width || !Height) return false;
	if (!OutputTarget || OutputTarget->GetWidth() != Width || OutputTarget->GetHeight() != Height ||
		OutputTarget->GetFormat() != ColorTarget->GetFormat())
	{
		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.Format = ColorTarget->GetFormat();
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		auto NewTarget = RenderCommand::CreateTexture2D(Desc);
		if (!NewTarget || !NewTarget->GetRTV() || !NewTarget->GetSRV()) return false;
		OutputTarget = std::move(NewTarget);
	}
	return true;
}

bool FGammaCorrectionRenderer::OnRender(FTexture2D* ColorTarget)
{
	if (!ColorTarget || !OutputTarget) return false;
	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindShaderResource(0, ColorTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}
