#pragma once

#include "Components/ActorComponent.h"
#include "Math/EngineMath.h"

struct FHeadBobMovementState
{
	bool bGrounded = false;
	bool bWalking = false;
	bool bStep = false;
	float ForwardWeight = 0.0f;
	uint64 UpdateSerial = 0;
	uint64 ResetSerial = 0;
};

// Pawn supplies completed movement and footsteps. The Modifier reads these settings
// and inputs; this component never changes the CameraComponent transform.
class UHeadBobComponent : public UActorComponent
{
	DECLARE_CLASS(UHeadBobComponent, UActorComponent)
	REFLECT_START(UHeadBobComponent)
		PROPERTY(bEnabled)
		PROPERTY(Amplitude)
		PROPERTY(ForwardMultiplier)
		PROPERTY(Duration)
		PROPERTY(IdleAmplitude)
		PROPERTY(IdleFrequency)
		PROPERTY(StateBlendTime)
	REFLECT_END()
public:
	bool bEnabled = true;
	float Amplitude = 0.04f;
	float ForwardMultiplier = 1.5f;
	float Duration = 0.24f;
	// Idle breathing: distance units and cycles per second (Hz).
	float IdleAmplitude = 0.006f;
	float IdleFrequency = 0.25f;
	float StateBlendTime = 0.3f;

	void BeginPlay() override;
	void EndPlay() override;
	void UpdateBob(float DeltaTime, const FVector& CompletedMovement, bool bStep, bool bGrounded = true);
	void Reset();
	// Suppress both Idle and Walk effects while a cinematic owns the camera.
	void SetSuspended(bool bValue);
	bool IsSuspended() const { return bSuspended; }
	const FHeadBobMovementState& GetMovementState() const { return MovementState; }

private:
	FHeadBobMovementState MovementState;
	bool bSuspended = false;
};
