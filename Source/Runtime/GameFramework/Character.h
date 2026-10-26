#pragma once

#include "GameFramework/Pawn.h"

class UCapsuleComponent;
class UCharacterMovementComponent;

// Reusable upright, capsule-based first-person pawn. Game rules belong to subclasses.
class ACharacter : public APawn
{
	DECLARE_CLASS(ACharacter, APawn)
public:
	ACharacter();
	void BeginPlay() override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	void Jump() override;
	void StopJumping() override;
	virtual bool CanJump() const;
	UCapsuleComponent* GetCapsuleComponent() const;
	UCapsuleComponent* GetCollisionComponent() const { return GetCapsuleComponent(); }
	UCameraComponent* GetCameraComponent() const override;
	UCharacterMovementComponent* GetCharacterMovement() const;
};
