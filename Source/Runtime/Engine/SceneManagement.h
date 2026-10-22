#pragma once

#include "Math/EngineMath.h"
#include "Math/Color.h"
#include <cstddef>

// 모든 광원(방향광·포인트·스포트·렉트)이 공유하는 GPU 원소.
// LightData.hlsli의 FLightRenderParameters와 바이트 단위로 같아야 한다.
struct FLightRenderParameters
{
	FVector Position;
	float Radius;
	FVector Color;
	float Intensity;
	FVector Direction;
	float InnerConeCos;
	FVector Right;
	float FalloffExponent;
	float OuterConeCos;
	float HalfWidth;
	float HalfHeight;
	uint32 Type;
	// Rect 전용: 발광면 가장자리의 문(Barn Door)
	float BarnDoorCos;
	float BarnDoorSin;
	float BarnDoorLength;
	// LightShadows에서 이 광원의 첫 그림자 뷰 위치. 그림자가 없으면 InvalidShadowIndex.
	uint32 ShadowIndex;
	float SourceSize; // Directional: tan(angular radius), Point/Spot: world radius

	static constexpr uint32 InvalidShadowIndex = 0xFFFFFFFFu;
};

static_assert(sizeof(FLightRenderParameters) == 100, "sizeof(LightData.hlsli FLightRenderParameters) == sizeof(FLightRenderParameters)");
static_assert(offsetof(FLightRenderParameters, ShadowIndex) == 92);
static_assert(offsetof(FLightRenderParameters, SourceSize) == 96);
