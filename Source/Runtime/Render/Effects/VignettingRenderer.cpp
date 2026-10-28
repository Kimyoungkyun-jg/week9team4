#include "EnginePCH.h"
#include "Render/Effects/VignettingRenderer.h"
#include "Render/Buffer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include <algorithm>

namespace
{
	struct alignas(16) FVignettingConstants
	{
		FVector4 Color;
		float InnerRadius;
		float Softness;
		float Opacity;
		float AspectRatio;
	};
	static_assert(sizeof(FVignettingConstants) == 32);
}

FVignettingRenderer::FVignettingRenderer() = default;
FVignettingRenderer::~FVignettingRenderer() = default;

void FVignettingRenderer::Init()
{
	PipelineState.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/VignettingShader.hlsl");
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.RasterizerState = ERasterizerState::SolidNone;
	PipelineState.BlendState = EBlendState::Opaque;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FVignettingConstants));
}

bool FVignettingRenderer::PrepareOutput(FTexture2D* ColorTarget)
{
	if (!ColorTarget || !PipelineState.Shader || !PipelineState.Shader->VertexShader ||
		!PipelineState.Shader->PixelShader || !ConstantBuffer) return false;
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

bool FVignettingRenderer::OnRender(FTexture2D* ColorTarget, const FVignettingSettings& Settings)
{
	if (!ColorTarget || !OutputTarget || !ConstantBuffer) return false;
	FVignettingConstants Data{};
	Data.Color = Settings.Color;
	Data.InnerRadius = std::max(Settings.InnerRadius, 0.0f);
	Data.Softness = std::max(Settings.Softness, 0.001f);
	Data.Opacity = std::clamp(Settings.Opacity, 0.0f, 1.0f);
	Data.AspectRatio = static_cast<float>(ColorTarget->GetWidth()) / ColorTarget->GetHeight();
	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, ColorTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}
