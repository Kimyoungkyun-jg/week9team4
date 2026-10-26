#pragma once

#include "Core/Types.h"
#include "Render/PipelineState.h"
#include "Math/Rect.h"
#include "Math/Vector2.h"
#include "Math/Color.h"

class UWidget;
class UCanvasWidget;
class UTextWidget;
class UMinimapWidget;
class UTexture2D;
class FTexture2D;
class FVertexBuffer;
class FConstantBuffer;

class FWidgetRenderer
{
public:
	bool Init();
	void Render(UCanvasWidget* Root, FTexture2D* Target);

private:
	void DrawWidget(UWidget* Widget);
	void DrawTextWidget(const UTextWidget* Widget);
	void DrawMinimap(UMinimapWidget* Widget);
	void DrawTriangle(const FVector2& A, const FVector2& B, const FVector2& C, const FLinearColor& Tint);
	void DrawText(const FString& Text, const FString& FontPath, float FontSize,
		const FLinearColor& Color, const FRect& Rect);
	void DrawQuad(const FRect& Rect, float U0, float V0, float U1, float V1,
		UTexture2D* Texture, const FLinearColor& Tint, float DistanceRange = 0.0f);
	void DrawQuad(const FRect& Rect, float U0, float V0, float U1, float V1,
		FTexture2D* Texture, const FLinearColor& Tint, float DistanceRange = 0.0f);

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	FPipelineState PipelineState;
	uint32 TargetWidth = 0;
	uint32 TargetHeight = 0;
	FVector2 ClipCenter;
	float ClipRadius = 0.0f;
	bool bMinimapTerrain = false;
};
