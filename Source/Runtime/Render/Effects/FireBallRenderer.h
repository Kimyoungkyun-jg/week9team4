#pragma once

#include "Render/SceneView.h"
#include "Math/Vector4.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"

class FRenderer;
class FTexture2D;

struct alignas(16) FFireBallData
{
	FMatrix InverseViewProjection;
	FVector Center;
	float Radius;
	FVector4 Color;
	FVector CameraPosition;
	float RadiusFallOff;
	float Intensity;
	float Padding[3]; // 16바이트 정렬을 위해 패딩 추가
};

static_assert(sizeof(FFireBallData) == 128);

class FFireBallRenderer
{
public:
	void Init(FRenderer* InRenderer);

	// 깊이에서 표면 위치를 복원해 반경 내의 FireBall 색상을 더한다.
	bool OnRender(const FSceneView& View, FTexture2D* DepthTarget, FTexture2D* ColorTarget, const FFireBallData& InFireBallData);

private:
	FPipelineState* PipelineState = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
};
