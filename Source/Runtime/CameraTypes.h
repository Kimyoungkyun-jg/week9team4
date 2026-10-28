#pragma once

#include "Math/Matrix.h"
#include "Math/EngineMath.h"
#include "Math/Rotator.h"
#include <cmath>

// 카메라가 어디서, 어떤 방향으로, 어떻게 보는지
// 카메라 컴포넌트와 카메라 매니저가 함께 사용하는 시점 데이터(예정)
struct FMinimalViewInfo
{
	FVector Location = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);

	float FOV = 60.0f; // 도 단위
	float AspectRatio = 16.0f / 9.0f;
	float NearClipPlane = 0.1f;
	float FarClipPlane = 10000.0f;

	bool bIsOrthogonal = false;
	float OrthoWidth = 16.0f;

	FMatrix GetViewMatrix() const
	{
		const FMatrix InverseTranslation = FMatrix::MakeTranslation(Location * -1.0f);

		const FMatrix InverseRotation =	Rotation.Quaternion().ToFMatrix().GetTransposed();

		return InverseTranslation * InverseRotation;
	}

    FMatrix GetProjectionMatrix() const
    {
        const float ZScale = FarClipPlane / (FarClipPlane - NearClipPlane);

        if (bIsOrthogonal)
        {
            const float Height = OrthoWidth / AspectRatio;
            const float XScale = 2.0f / OrthoWidth;
            const float YScale = 2.0f / Height;

            const float OrthoZScale = 1.0f / (FarClipPlane - NearClipPlane);

            const float ZOffset = -NearClipPlane / (FarClipPlane - NearClipPlane);

            return FMatrix(
                0.0f, 0.0f, OrthoZScale, 0.0f,
                XScale, 0.0f, 0.0f, 0.0f,
                0.0f, YScale, 0.0f, 0.0f,
                0.0f, 0.0f, ZOffset, 1.0f
            );
        }

        const float HalfFOV = FMath::DegreesToRadians(FOV) * 0.5f;
        const float YScale = 1.0f / std::tan(HalfFOV);
        const float XScale = YScale / AspectRatio;

        const float ZOffset = -NearClipPlane * FarClipPlane / (FarClipPlane - NearClipPlane);

        return FMatrix(
            0.0f, 0.0f, ZScale, 1.0f,
            XScale, 0.0f, 0.0f, 0.0f,
            0.0f, YScale, 0.0f, 0.0f,
            0.0f, 0.0f, ZOffset, 0.0f
        );
    }
};
