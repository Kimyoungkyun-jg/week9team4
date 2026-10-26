#pragma once

#include "Components/ActorComponent.h"
#include "Math/EngineMath.h"

class UCameraComponent;

// Optional camera effect. The caller supplies completed movement and its footstep pulse
// after movement simulation; this component never moves the pawn or plays game audio.
class UHeadBobComponent : public UActorComponent
{
	DECLARE_CLASS(UHeadBobComponent, UActorComponent)
	REFLECT_START(UHeadBobComponent)
		PROPERTY(bEnabled)
		PROPERTY(Amplitude)
		PROPERTY(ForwardMultiplier)
		PROPERTY(Duration)
	REFLECT_END()
public:
	bool bEnabled = true;
	float Amplitude = 0.04f;
	float ForwardMultiplier = 1.5f;
	float Duration = 0.24f;

	void BeginPlay() override;
	void EndPlay() override;
	void UpdateBob(float DeltaTime, const FVector& CompletedMovement, bool bStep);
	void Reset();
	// Remove the effect before a cinematic takes ownership of the camera.
	// Resume after the caller restores the camera; its current position becomes the base.
	void SetSuspended(bool bValue);
	bool IsSuspended() const { return bSuspended; }
	// Call after explicitly restoring/teleporting the camera, including while suspended.
	void Rebase();

private:
	UCameraComponent* GetCamera() const;
	void ClearPulse();
	FVector BaseLocation = FVector::ZeroVector;
	float Offset = 0.f;
	float StartOffset = 0.f;
	float StepAmplitude = 0.f;
	float Elapsed = 0.f;
	bool bActivePulse = false;
	bool bReturning = false;
	bool bSuspended = false;
};
