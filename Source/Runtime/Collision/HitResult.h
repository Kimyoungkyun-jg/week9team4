#pragma once
#include "Core/Types.h"
#include "Math/Vector.h"

class UPrimitiveComponent;

struct FHitResult
{
	int32 FaceIndex = -1; // Hitted face index

	float Time = 1.0f; // Fraction of requested translation
	float Distance = FLT_MAX;

	FVector Location = FVector(); // For swept
	FVector ImpactPoint = FVector();
	FVector Normal = FVector::ZeroVector;
	FVector ImpactNormal = FVector::ZeroVector;

	FVector TraceStart;
	FVector TraceEnd;

	float PenetrationDepth = 0.0f;

	bool bBlockingHit = false;
	bool bStartPenetrating = false;

	UPrimitiveComponent* HitComponent = nullptr;

	FHitResult()
	{
		Init();
	}

	explicit FHitResult(float InTime)
	{
		Init();
		Time = InTime;
	}

	// Initailize empty hit result
	inline void Init()
	{
		FaceIndex = -1;
		Time = 1.0f;
		Distance = 0.0f;
		Location = ImpactPoint = Normal = ImpactNormal = FVector::ZeroVector;
		TraceStart = TraceEnd = FVector::ZeroVector;
		PenetrationDepth = 0.0f;
		bBlockingHit = bStartPenetrating = false;
		HitComponent = nullptr;
	}

	inline void Init(FVector Start, FVector End)
	{
		Init();
		TraceStart = Start;
		TraceEnd = End;
	}
};
