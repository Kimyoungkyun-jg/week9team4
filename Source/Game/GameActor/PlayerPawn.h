#pragma once

#include "GameFramework/DefaultPawn.h"

class AFlashlightActor;
class UCapsuleComponent;

class APlayerPawn : public ADefaultPawn
{
	DECLARE_CLASS(APlayerPawn, ADefaultPawn)

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerPitchInput(float Value) override;
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	UCapsuleComponent* GetCollisionComponent() const { return CollisionComponent; }

private:
	AFlashlightActor* FlashlightActor = nullptr;
	UCapsuleComponent* CollisionComponent = nullptr;
};
