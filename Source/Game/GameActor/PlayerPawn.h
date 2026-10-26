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

private:
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
