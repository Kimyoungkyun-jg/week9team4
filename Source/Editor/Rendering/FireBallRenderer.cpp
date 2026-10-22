#include "EnginePCH.h"
#include "Editor/Rendering/FireBallRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

// Shader와 상수 버퍼를 준비한다.
void FFireBallRenderer::Init(FRenderer* InRenderer)
{
	PipelineState = FRenderResourceManager::GetPSO(EPSOType::FireBall);

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FFireBallData));
}

bool FFireBallRenderer::OnRender(const FSceneView& View, FTexture2D* DepthTarget, FTexture2D* ColorTarget, const FFireBallData& InFireBallData)
{
	if (!DepthTarget || !ColorTarget || !PipelineState || !PipelineState->Shader ||
		!PipelineState->Shader->VertexShader || !PipelineState->Shader->PixelShader || !ConstantBuffer)
		return false;

	const uint32 Width = ColorTarget->GetWidth();
	const uint32 Height = ColorTarget->GetHeight();
	if (Width == 0 || Height == 0 || DepthTarget->GetWidth() != Width ||
		DepthTarget->GetHeight() != Height)
		return false;

	if (InFireBallData.Radius <= 0.0f || InFireBallData.Intensity <= 0.0f) return false;

	FFireBallData Data = InFireBallData;
	Data.InverseViewProjection = View.ViewProjectionMatrix.Inverse();
	Data.CameraPosition = View.ViewLocation;
	Data.Center = InFireBallData.Center;
	Data.Color = InFireBallData.Color;
	Data.Intensity = InFireBallData.Intensity;
	Data.Radius = InFireBallData.Radius;
	Data.RadiusFallOff = InFireBallData.RadiusFallOff;

	RenderCommand::BindPipelineState(*PipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}
