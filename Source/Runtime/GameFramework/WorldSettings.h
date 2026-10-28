#pragma once

#include "GameFramework/Actor.h"

// The time-related subset of UE's per-level AWorldSettings.
class AWorldSettings : public AActor
{
	DECLARE_CLASS(AWorldSettings, AActor)
	REFLECT_START(AWorldSettings)
		PROPERTY(MinGlobalTimeDilation)
		PROPERTY(MaxGlobalTimeDilation)
		PROPERTY(MinUndilatedFrameTime)
		PROPERTY(MaxUndilatedFrameTime)
	REFLECT_END()

public:
	AWorldSettings();
	virtual float GetEffectiveTimeDilation() const;
	virtual float FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds);
	virtual float SetTimeDilation(float NewTimeDilation);
	virtual void SetAllowTimeDilation(bool NewAllowTimeDilation);

	// Runtime values, deliberately not saved to scene files (transient in UE).
	float TimeDilation = 1.0f;
	float CinematicTimeDilation = 1.0f;
	float DemoPlayTimeDilation = 1.0f;
	bool bAllowTimeDilation = true;

	// Defaults from UE Engine/Config/BaseGame.ini, in seconds where applicable.
	float MinGlobalTimeDilation = 0.0001f;
	float MaxGlobalTimeDilation = 20.0f;
	float MinUndilatedFrameTime = 0.0005f;
	float MaxUndilatedFrameTime = 0.4f;
};
