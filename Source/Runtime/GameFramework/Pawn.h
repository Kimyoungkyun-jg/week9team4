#pragma once

#include "GameFramework/Actor.h"

class APlayerController;
class UCameraComponent;

class APawn : public AActor
{
	DECLARE_CLASS(APawn, AActor)
	REFLECT_START(ClassName)
	REFLECT_END()
public:
	APawn();
	// ScaleValue is an input axis, not DeltaTime. Movement consumes it once per tick.
	virtual void AddMovementInput(const FVector& WorldDirection, float ScaleValue);
	FVector ConsumeMovementInputVector();
	virtual void Jump() {}
	virtual void StopJumping() {}
	virtual UCameraComponent* GetCameraComponent() const { return nullptr; }
	virtual void AddControllerYawInput(float Value) {}
	virtual void AddControllerPitchInput(float Value) {}
	void PossessedBy(APlayerController* NewController) { Controller = NewController; }
	void UnPossessed() { Controller = nullptr; }
	APlayerController* GetController() const { return Controller; }
private:
	APlayerController* Controller = nullptr;
	FVector PendingMovementInput = FVector::ZeroVector;
};
