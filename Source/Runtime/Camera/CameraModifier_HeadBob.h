#pragma once

#include "Camera/CameraModifier.h"

// Idle breathing and footstep-driven Walk bob share one state blend.
// Only the final POV is changed; attached objects keep their base transforms.
class UCameraModifier_HeadBob : public UCameraModifier
{
	DECLARE_CLASS(UCameraModifier_HeadBob, UCameraModifier)

public:
	UCameraModifier_HeadBob() { Priority = 100; } // Before CameraShake (127).
	bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	void ResetAnimation();
	uint32 SourceUUID = 0;
	uint64 ResetSerial = 0;
	uint64 LastInputSerial = 0;
	double IdlePhase = 0.0;
	float IdleWeight = 0.0f;
	float WalkWeight = 0.0f;
	float WalkOffset = 0.0f;
	float StartOffset = 0.0f;
	float StepAmplitude = 0.0f;
	float Elapsed = 0.0f;
	bool bActivePulse = false;
	bool bReturning = false;
};
