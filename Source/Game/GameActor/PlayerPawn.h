#pragma once

#include "GameFramework/DefaultPawn.h"
#include "Audio/FootstepCadence.h"
#include <random>

class AFlashlightActor;
class UCapsuleComponent;

class APlayerPawn : public ADefaultPawn
{
	DECLARE_CLASS(APlayerPawn, ADefaultPawn)
	REFLECT_START(APlayerPawn)
		PROPERTY(Life)
		PROPERTY(bHeadBobEnabled)
		PROPERTY(HeadBobAmplitude)
		PROPERTY(HeadBobForwardMultiplier)
		PROPERTY(HeadBobDuration)
	REFLECT_END()

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	void SetFootstepInterval(float Seconds) { FootstepCadence.IntervalSeconds = Seconds; }
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerPitchInput(float Value) override;
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	UCapsuleComponent* GetCollisionComponent() const { return CollisionComponent; }
	int32 GetLife() const { return Life; }
	void SetLife(int32 InLife);
	void RespawnAtPlayerStart();
	// The hit sequence owns the camera until it explicitly returns control.
	void SetHeadBobSuspended(bool bSuspended);

private:
	void ResetWalkingEffects();
	void ResetHeadBob();
	void UpdateHeadBob(float DeltaTime, const FVector& Movement, bool bStep);
	bool bHeadBobEnabled = true;
	float HeadBobAmplitude = 0.04f;
	float HeadBobForwardMultiplier = 1.5f;
	float HeadBobDuration = 0.24f;
	FVector HeadBobBaseLocation = FVector::ZeroVector;
	float HeadBobOffset = 0.0f;
	float HeadBobStartOffset = 0.0f;
	float HeadBobStepAmplitude = 0.0f;
	float HeadBobElapsed = 0.0f;
	bool bHeadBobActive = false;
	bool bHeadBobReturning = false;
	bool bHeadBobSuspended = false;
	uint32 WalkStateRevision = 0;
	FFootstepCadence FootstepCadence;
	std::mt19937 FootstepRandom{std::random_device{}()};
	FVector WalkDisplacement = FVector::ZeroVector;
	AFlashlightActor* FlashlightActor = nullptr;
	UCapsuleComponent* CollisionComponent = nullptr;
	int32 Life = 3;
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation;
	FVector PlayerStartCameraLocation = FVector::ZeroVector;
	FRotator PlayerStartCameraRotation;
};
