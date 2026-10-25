#pragma once

#include "GameFramework/Pawn.h"

class UCameraComponent;

class ADefaultPawn : public APawn
{
	DECLARE_CLASS(ADefaultPawn, APawn)
public:
	ADefaultPawn();
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
private:
	UCameraComponent* CameraComponent = nullptr;
	float MoveSpeed = 20.0f;
};
