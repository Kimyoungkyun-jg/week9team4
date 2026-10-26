#include "EnginePCH.h"
#include "UI/WidgetRenderer.h"
#include "UI/Widget.h"
#include "GameFramework/Actor/MapActor.h"
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
	struct FWidgetParams { float ViewportAndMode[4]; float Tint[4]; float ClipCircle[4]; };
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
	ClipRadius = 0.0f;
	bMinimapTerrain = false;
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
	if (UMinimapWidget* Minimap = Cast<UMinimapWidget>(Widget))
		DrawMinimap(Minimap);
	else if (UImageWidget* Image = Cast<UImageWidget>(Widget))
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Image->GetTexturePath()))
			DrawQuad(Image->GetLayoutRect(), 0.0f, 0.0f, 1.0f, 1.0f, Texture, Image->GetTint());
	}
	else if (UTextWidget* Text = Cast<UTextWidget>(Widget))
		DrawTextWidget(Text);
	else if (UButtonWidget* Button = Cast<UButtonWidget>(Widget))
	{
		UTextInputWidget* Input = Cast<UTextInputWidget>(Button);
		FLinearColor Color = (Button->IsHovered() || (Input && Input->IsFocused())) ? Button->GetHoverBackgroundColor() : Button->GetBackgroundColor();
		if (Button->IsPressed()) { Color.R *= 0.75f; Color.G *= 0.75f; Color.B *= 0.75f; }
		DrawQuad(Button->GetLayoutRect(), 0, 0, 1, 1,
			UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture"), Color);
		DrawText(Input ? Input->GetDisplayText() : Button->GetText(), Button->GetFontPath(), Button->GetFontSize(),
			Button->IsHovered() ? Button->GetHoverTextColor() : Button->GetTextColor(), Button->GetLayoutRect());
	}
	if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
		for (UWidget* Child : Canvas->GetChildrenInPaintOrder())
			DrawWidget(Child);
}

void FWidgetRenderer::DrawMinimap(UMinimapWidget* Widget)
{
	FTexture2D* Terrain = Widget->GetTerrainTexture();
	if (!Terrain || !Widget->HasPlayer() || !Widget->GetMap()) return;
	const FRect Rect = Widget->GetMapRect();
	if (Rect.Width <= 0.0f) return;
	const AMapActor* Map = Widget->GetMap();
	const FVector2 PlayerUV = Widget->GetPlayerUV();
	const float RadiusTiles = Widget->GetRadiusTiles();
	const float RadiusPixels = Rect.Width * 0.5f;
	ClipCenter = {Rect.X + RadiusPixels, Rect.Y + RadiusPixels};
	ClipRadius = RadiusPixels;
	bMinimapTerrain = true;
	const float UExtent = RadiusTiles / Map->GetColumns();
	const float VExtent = RadiusTiles / Map->GetRows();
	const float TextureU = 1.0f - PlayerUV.X;
	const float TextureV = 1.0f - PlayerUV.Y;
	RenderCommand::BindSamplerState(0, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
	DrawQuad(Rect, TextureU - UExtent, TextureV - VExtent,
		TextureU + UExtent, TextureV + VExtent, Terrain, {1, 1, 1, 1});
	bMinimapTerrain = false;
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	UTexture2D* White = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
	if (!White) { ClipRadius = 0.0f; return; }
	for (const FVector2& UV : Widget->GetCoins())
	{
		const float DX = (UV.X - PlayerUV.X) * Map->GetColumns();
		const float DY = (UV.Y - PlayerUV.Y) * Map->GetRows();
		if (DX * DX + DY * DY > RadiusTiles * RadiusTiles) continue;
		const float X = ClipCenter.X - DX / RadiusTiles * RadiusPixels;
		const float Y = ClipCenter.Y - DY / RadiusTiles * RadiusPixels;
		DrawQuad({X - 1.5f, Y - 1.5f, 3.0f, 3.0f}, 0, 0, 1, 1, White, {1.0f, 0.85f, 0.04f, 1.0f});
	}
	{
		const FVector2 Direction = Widget->GetPlayerDirection();
		const float X = ClipCenter.X, Y = ClipCenter.Y;
		const FVector2 Forward{-Direction.X, -Direction.Y}, Side{-Forward.Y, Forward.X};
		DrawTriangle({X + Forward.X * 7.0f, Y + Forward.Y * 7.0f},
			{X - Forward.X * 4.0f + Side.X * 4.0f, Y - Forward.Y * 4.0f + Side.Y * 4.0f},
			{X - Forward.X * 4.0f - Side.X * 4.0f, Y - Forward.Y * 4.0f - Side.Y * 4.0f},
			{0.25f, 0.9f, 1.0f, 1.0f});
	}
	ClipRadius = 0.0f;
}

void FWidgetRenderer::DrawTriangle(const FVector2& A, const FVector2& B, const FVector2& C, const FLinearColor& Tint)
{
	UTexture2D* White = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
	if (!White || !White->GetResource()) return;
	const FWidgetVertex Vertices[3] = {{A.X, A.Y, 0, 0}, {B.X, B.Y, 0, 0}, {C.X, C.Y, 0, 0}};
	const FWidgetParams Params = {{static_cast<float>(TargetWidth), static_cast<float>(TargetHeight), 0, 0},
		{Tint.R, Tint.G, Tint.B, Tint.A}, {ClipCenter.X, ClipCenter.Y, ClipRadius, 0}};
	RenderCommand::UpdateBufferData(VertexBuffer.get(), Vertices, sizeof(Vertices));
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Params, sizeof(Params));
	RenderCommand::BindShaderResource(0, White, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
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
	DrawQuad(Rect, U0, V0, U1, V1, Texture ? Texture->GetResource() : nullptr, Tint, DistanceRange);
}

void FWidgetRenderer::DrawQuad(const FRect& Rect, float U0, float V0, float U1, float V1,
	FTexture2D* Texture, const FLinearColor& Tint, float DistanceRange)
{
	if (!Texture || !Texture->GetSRV() || Rect.Width <= 0.0f || Rect.Height <= 0.0f) return;
	const float X1 = Rect.X + Rect.Width;
	const float Y1 = Rect.Y + Rect.Height;
	const FWidgetVertex Vertices[6] = {
		{Rect.X, Rect.Y, U0, V0}, {X1, Rect.Y, U1, V0}, {Rect.X, Y1, U0, V1},
		{Rect.X, Y1, U0, V1}, {X1, Rect.Y, U1, V0}, {X1, Y1, U1, V1}
	};
	const FWidgetParams Params = {{static_cast<float>(TargetWidth), static_cast<float>(TargetHeight), DistanceRange, bMinimapTerrain ? 1.0f : 0.0f},
		{Tint.R, Tint.G, Tint.B, Tint.A}, {ClipCenter.X, ClipCenter.Y, ClipRadius, 0}};
	RenderCommand::UpdateBufferData(VertexBuffer.get(), Vertices, sizeof(Vertices));
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Params, sizeof(Params));
	RenderCommand::BindShaderResource(0, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
}
