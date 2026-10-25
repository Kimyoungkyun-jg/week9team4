#include "EnginePCH.h"
#include "UI/WidgetRenderer.h"
#include "UI/Widget.h"
#include "Text/Font.h"
#include "Text/Utf8.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "Render/Buffer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture.h"

namespace
{
	struct FWidgetVertex { float X, Y, U, V; };
	struct FWidgetParams { float ViewportAndMode[4]; float Tint[4]; };
}

bool FWidgetRenderer::Init()
{
	PipelineState.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/WidgetShader.hlsl");
	PipelineState.RasterizerState = ERasterizerState::SolidNone;
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
	if (!PipelineState.Shader) return false;
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FWidgetVertex) * 6, sizeof(FWidgetVertex));
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FWidgetParams));
	return VertexBuffer && ConstantBuffer;
}

void FWidgetRenderer::Render(UCanvasWidget* Root, FTexture2D* Target)
{
	if (!Root || !Target || !Target->GetRTV() || !VertexBuffer || !ConstantBuffer ||
		!Target->GetWidth() || !Target->GetHeight()) return;
	TargetWidth = Target->GetWidth();
	TargetHeight = Target->GetHeight();
	Root->Layout({0.0f, 0.0f, static_cast<float>(TargetWidth), static_cast<float>(TargetHeight)});
	RenderCommand::BindRenderPassNoClear(Target, nullptr, TargetWidth, TargetHeight);
	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	DrawWidget(Root);
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	RenderCommand::EndRenderPass();
}

void FWidgetRenderer::DrawWidget(UWidget* Widget)
{
	if (!Widget || !Widget->IsVisible()) return;
	if (UImageWidget* Image = Cast<UImageWidget>(Widget))
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Image->GetTexturePath()))
			DrawQuad(Image->GetLayoutRect(), 0.0f, 0.0f, 1.0f, 1.0f, Texture, Image->GetTint());
	}
	else if (UTextWidget* Text = Cast<UTextWidget>(Widget))
		DrawTextWidget(Text);
	else if (UButtonWidget* Button = Cast<UButtonWidget>(Widget))
	{
		FLinearColor Color = Button->GetBackgroundColor();
		if (Button->IsPressed()) { Color.R *= 0.75f; Color.G *= 0.75f; Color.B *= 0.75f; }
		DrawQuad(Button->GetLayoutRect(), 0, 0, 1, 1,
			UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture"), Color);
		DrawText(Button->GetText(), Button->GetFontPath(), Button->GetFontSize(),
			Button->GetTextColor(), Button->GetLayoutRect());
	}
	if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
		for (UWidget* Child : Canvas->GetChildrenInPaintOrder())
			DrawWidget(Child);
}

void FWidgetRenderer::DrawTextWidget(const UTextWidget* Widget)
{
	DrawText(Widget->GetDisplayText(), Widget->GetFontPath(), Widget->GetFontSize(),
		Widget->GetColor(), Widget->GetLayoutRect());
}

void FWidgetRenderer::DrawText(const FString& Text, const FString& FontPath, float FontSize,
	const FLinearColor& Color, const FRect& Rect)
{
	const UFont* Font = UAssetManager::GetAssetByPath<UFont>(FontPath);
	if (!Font || !Font->AtlasTexture || Font->Width <= 0.0f || Font->Height <= 0.0f ||
		FontSize <= 0.0f) return;
	float PenX = Rect.X;
	float Baseline = Rect.Y + FontSize;
	uint32 Previous = 0;
	for (uint32 CodePoint : DecodeUTF8(Text))
	{
		if (CodePoint == '\n') { PenX = Rect.X; Baseline += Font->LineHeight * FontSize; Previous = 0; continue; }
		const FGlyphInfo* Glyph = Font->GlyphMap.Find(CodePoint);
		if (!Glyph) continue;
		if (Previous)
			if (const float* Kerning = Font->KerningMap.Find((static_cast<uint64>(Previous) << 32) | CodePoint))
				PenX += *Kerning * FontSize;
		if (Glyph->bHasBounds)
		{
			const float Size = FontSize;
			DrawQuad({PenX + Glyph->PlaneLeft * Size, Baseline - Glyph->PlaneTop * Size,
				(Glyph->PlaneRight - Glyph->PlaneLeft) * Size, (Glyph->PlaneTop - Glyph->PlaneBottom) * Size},
				Glyph->AtlasLeft / Font->Width, 1.0f - Glyph->AtlasTop / Font->Height,
				Glyph->AtlasRight / Font->Width, 1.0f - Glyph->AtlasBottom / Font->Height,
				Font->AtlasTexture, Color, Font->DistanceRange);
		}
		PenX += Glyph->Advance * FontSize;
		Previous = CodePoint;
	}
}

void FWidgetRenderer::DrawQuad(const FRect& Rect, float U0, float V0, float U1, float V1,
	UTexture2D* Texture, const FLinearColor& Tint, float DistanceRange)
{
	if (!Texture || !Texture->GetResource() || Rect.Width <= 0.0f || Rect.Height <= 0.0f) return;
	const float X1 = Rect.X + Rect.Width;
	const float Y1 = Rect.Y + Rect.Height;
	const FWidgetVertex Vertices[6] = {
		{Rect.X, Rect.Y, U0, V0}, {X1, Rect.Y, U1, V0}, {Rect.X, Y1, U0, V1},
		{Rect.X, Y1, U0, V1}, {X1, Rect.Y, U1, V0}, {X1, Y1, U1, V1}
	};
	const FWidgetParams Params = {{static_cast<float>(TargetWidth), static_cast<float>(TargetHeight), DistanceRange, 0.0f},
		{Tint.R, Tint.G, Tint.B, Tint.A}};
	RenderCommand::UpdateBufferData(VertexBuffer.get(), Vertices, sizeof(Vertices));
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Params, sizeof(Params));
	RenderCommand::BindShaderResource(0, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
}
