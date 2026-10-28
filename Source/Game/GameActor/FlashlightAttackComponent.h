#pragma once

#include "Components/ActorComponent.h"
#include "Math/Transform.h"

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
	bool CanContinue() const;
	EFlashlightAttackPhase Phase = EFlashlightAttackPhase::Idle;
	float PhaseElapsed = 0.0f;
	FTransform RestPose;
	FTransform RecoveryStartPose;
	float HitStopRemaining = 0.0f;
	float PlayerTimeDilationBeforeStop = 1.0f;
	float FlashlightTimeDilationBeforeStop = 1.0f;
};
