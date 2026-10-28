#pragma once

#include "Math/Vector.h"
#include "Math/Rotator.h"
// 카메라 시점(위치, 회전, 투영 정보 등)을 전달하기 위한 최소 뷰 정보 구조체
// git add 안되어 있어서 임시로 추가
struct FMinimalViewInfo
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation;
	float FieldOfView = 60.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearClip = 0.1f;
	float FarClip = 10000.0f;
	float OrthoWidth = 16.0f;
	bool bIsPerspective = true;
};
