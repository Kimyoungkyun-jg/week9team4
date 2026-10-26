#pragma once

#include "Components/MovementComponent.h"

enum class ECharacterMovementMode : uint8 { None, Walking, Falling };

struct FCharacterFloorResult
{
	bool bWalkableFloor = false;
	float FloorDistance = 0.f;
	FHitResult Hit;
};

// Z-up capsule locomotion. Units are metres and seconds, matching this engine's Gravity.
class UCharacterMovementComponent : public UMovementComponent
{
	DECLARE_CLASS(UCharacterMovementComponent, UMovementComponent)
	REFLECT_START(UCharacterMovementComponent)
		PROPERTY(MaxWalkSpeed)
		PROPERTY(MaxAcceleration)
		PROPERTY(BrakingDecelerationWalking)
		PROPERTY(AirControl)
		PROPERTY(GravityScale)
		PROPERTY(JumpZVelocity)
		PROPERTY(MaxFallSpeed)
		PROPERTY(WalkableFloorAngle)
		PROPERTY(MaxStepHeight)
		PROPERTY(bJumpAllowed)
	REFLECT_END()
public:
	UCharacterMovementComponent();
	float MaxWalkSpeed = 5.f;
	float MaxAcceleration = 40.f;
	float BrakingDecelerationWalking = 40.f;
	float AirControl = 0.35f;
	float GravityScale = 1.f;
	float JumpZVelocity = 4.2f;
	float MaxFallSpeed = 55.f;
	float WalkableFloorAngle = 45.f;
	float MaxStepHeight = 0.45f;
	float GroundProbeDistance = 0.04f;
	float FloorGap = 0.002f;
	float MaxSimulationTimeStep = 1.f / 120.f;
	int32 MaxSimulationIterations = 32;

	void TickComponent(float DeltaTime) override;
	bool MoveUpdatedComponent(const FVector& Delta, const FRotator& Rotation, bool bSweep, FHitResult* Hit) override;
	void StopMovementImmediately() override;
	FVector ComputeSlideVector(const FVector& Delta, float Time, const FVector& Normal, const FHitResult& Hit) const override;
	virtual bool CanJump() const;
	void SetJumpAllowed(bool bAllowed) { bJumpAllowed = bAllowed; if (!bAllowed) bJumpRequested = false; }
	bool IsJumpAllowed() const { return bJumpAllowed; }
	void RequestJump() { if (IsJumpAllowed() && bMovementEnabled) bJumpRequested = true; }
	void StopJumping() { bJumpRequested = false; }
	void SetMovementEnabled(bool bEnabled);
	bool IsMovementEnabled() const { return bMovementEnabled; }
	bool IsMovingOnGround() const { return MovementMode == ECharacterMovementMode::Walking; }
	bool IsFalling() const { return MovementMode == ECharacterMovementMode::Falling; }
	ECharacterMovementMode GetMovementMode() const { return MovementMode; }
	const FCharacterFloorResult& GetCurrentFloor() const { return CurrentFloor; }
	FVector GetLastGroundedDisplacement() const { return LastGroundedDisplacement; }
	// Call after an external teleport/respawn: no momentum or old floor is retained.
	void ResetAfterTeleport();
	bool IsWalkable(const FHitResult& Hit) const;
	bool FindFloor(const FVector& Location, float Distance, FCharacterFloorResult& OutFloor) const;
protected:
	virtual void PhysWalking(float DeltaTime, const FVector& Input);
	virtual void PhysFalling(float DeltaTime, const FVector& Input);
	bool StepUp(const FVector& RemainingDelta, const FHitResult& Obstacle);
private:
	FVector SurfaceNormal(const FHitResult& Hit) const;
	bool SweepAt(const FVector& Start, const FVector& Delta, FHitResult& Hit) const;
	bool MoveSafely(const FVector& Delta, FHitResult& Hit);
	bool RefreshFloor(float Distance, bool bSnap);
	bool CanContinue() const;
	void Accelerate(const FVector& Input, float DeltaTime, bool bGrounded);
	FCharacterFloorResult CurrentFloor;
	FVector LastGroundedDisplacement = FVector::ZeroVector;
	ECharacterMovementMode MovementMode = ECharacterMovementMode::Falling;
	bool bJumpRequested = false;
	bool bJumpAllowed = true;
	bool bMovementEnabled = true;
	bool bSimulating = false;
	bool bInterrupted = false;
	USceneComponent* SimulationComponent = nullptr;
};
