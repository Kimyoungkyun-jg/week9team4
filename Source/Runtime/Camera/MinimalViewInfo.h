#pragma once

#include "Math/Vector.h"
#include "Math/Rotator.h"

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
