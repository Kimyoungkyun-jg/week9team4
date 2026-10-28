#include "EnginePCH.h"
#include "GameFramework/WorldSettings.h"

AWorldSettings::AWorldSettings()
{
	SetActorEnableCollision(false);
}

float AWorldSettings::GetEffectiveTimeDilation() const
{
	return bAllowTimeDilation ? TimeDilation * CinematicTimeDilation * DemoPlayTimeDilation : 1.0f;
}

float AWorldSettings::FixupDeltaSeconds(float DeltaSeconds, float RealDeltaSeconds)
{
	// UE's base implementation does not use RealDeltaSeconds; overrides may.
	(void)RealDeltaSeconds;
	const float Dilation = GetEffectiveTimeDilation();
	const float MinFrameTime = MinUndilatedFrameTime * Dilation;
	const float MaxFrameTime = MaxUndilatedFrameTime * Dilation;
	return FMath::Clamp(DeltaSeconds, MinFrameTime, MaxFrameTime);
}

float AWorldSettings::SetTimeDilation(float NewTimeDilation)
{
	TimeDilation = FMath::Clamp(NewTimeDilation, MinGlobalTimeDilation, MaxGlobalTimeDilation);
	return TimeDilation;
}

void AWorldSettings::SetAllowTimeDilation(bool NewAllowTimeDilation)
{
	bAllowTimeDilation = NewAllowTimeDilation;
}
