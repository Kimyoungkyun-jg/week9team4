#pragma once

#include "Components/ActorComponent.h"
#include "Math/Transform.h"
#include "Camera/CameraShake.h"

class UPrimitiveComponent;

enum class EFlashlightAttackPhase : uint8 { Idle, Windup, Swing, Recovery };

class UFlashlightAttackComponent : public UActorComponent
{
	DECLARE_CLASS(UFlashlightAttackComponent, UActorComponent)
	REFLECT_START(UFlashlightAttackComponent)
		PROPERTY(BatteryCost)
		PROPERTY(WindupDuration)
		PROPERTY(SwingDuration)
		PROPERTY(RecoveryDuration)
		PROPERTY(AttackReach)
		PROPERTY(SwingPivot)
		PROPERTY(SwingCoordinateRotation)
		PROPERTY(SwingHalfAngle)
		PROPERTY(SwingHandRadius)
		PROPERTY(CapsuleRadius)
		PROPERTY(HitStopDuration)
		PROPERTY(bHitCameraShakeEnabled)
		PROPERTY(HitCameraShakeDuration)
		PROPERTY(HitCameraShakeScale)
		PROPERTY(HitCameraShakeFrequency)
		PROPERTY(HitCameraShakeLocationAmplitude)
		PROPERTY(HitCameraShakeRotationAmplitude)
		PROPERTY(GhostStaggerDuration)
		PROPERTY(FlashMultiplier)
		PROPERTY(TrailAngleStep)
	REFLECT_END()
public:
	UFlashlightAttackComponent();
	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;
	bool TryStartAttack();
	void CancelAttack();
	// Preserve the active hitstop while changing the dilation restored at its end.
	void SetPlayerTimeDilation(float Dilation);
	bool IsAttacking() const { return Phase != EFlashlightAttackPhase::Idle; }
	bool IsHitStopped() const { return HitStopRemaining > 0.0f; }
	EFlashlightAttackPhase GetPhase() const { return Phase; }
	float GetPhaseElapsed() const { return PhaseElapsed; }

	float BatteryCost = 5.0f;
	float WindupDuration = 0.12f;
	float SwingDuration = 0.18f;
	float RecoveryDuration = 0.25f;
	float AttackReach = 20.0f; // Maximum beam length from the flashlight tip, in metres.
	FVector SwingPivot = FVector(0.9f, 0.0f, -0.35f); // Camera-local fan centre, in metres.
	FRotator SwingCoordinateRotation = FRotator(-25.0f, 0.0f, 25.0f); // Orientation of the fan's local XY plane.
	float SwingHalfAngle = 90.0f; // Angular range within the fan's coordinates, +angle to -angle.
	float SwingHandRadius = 0.35f; // Distance from the fan centre to the flashlight root, in metres.
	float CapsuleRadius = 0.18f;
	float HitStopDuration = 0.12f;
	// Perlin impact shake; duration includes its short attack and fade-out.
	bool bHitCameraShakeEnabled = true;
	float HitCameraShakeDuration = 0.2f;
	float HitCameraShakeScale = 1.0f;
	float HitCameraShakeFrequency = 28.0f; // Noise lattice units per second.
	FVector HitCameraShakeLocationAmplitude = FVector(0.025f, 0.04f, 0.025f); // Camera-local metres.
	FRotator HitCameraShakeRotationAmplitude = FRotator(1.5f, 1.0f, 0.6f); // Degrees.
	float GhostStaggerDuration = 0.8f;
	float FlashMultiplier = 3.0f;
	float TrailAngleStep = 2.0f; // Successive visual-only capsules lag by this many fan degrees.
private:
	void StartSwing();
	void StartRecovery();
	void ApplyVisuals();
	void HandleAttackOverlap(UPrimitiveComponent* Self, UPrimitiveComponent* Other);
	void BeginHitStop();
	void EndHitStop();
	void StartHitCameraShake();
	void StopHitCameraShake();
	bool CanContinue() const;
	EFlashlightAttackPhase Phase = EFlashlightAttackPhase::Idle;
	float PhaseElapsed = 0.0f;
	FTransform RestPose;
	FTransform RecoveryStartPose;
	float HitStopRemaining = 0.0f;
	float PlayerTimeDilationBeforeStop = 1.0f;
	float FlashlightTimeDilationBeforeStop = 1.0f;
	FCameraShakeHandle HitCameraShakeHandle;
	uint32 HitCameraShakeSeed = 0;
};
