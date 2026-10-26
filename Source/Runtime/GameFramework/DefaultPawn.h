#pragma once

#include "GameFramework/Pawn.h"

class UCameraComponent;

class ADefaultPawn : public APawn
{
	DECLARE_CLASS(ADefaultPawn, APawn)
public:
	ADefaultPawn();
	void Tick(float DeltaTime) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	UCameraComponent* GetCameraComponent() const override { return CameraComponent; }
protected:
	float GetMoveSpeed() const { return MoveSpeed; }
private:
	UCameraComponent* CameraComponent = nullptr;
	float MoveSpeed = 5.0f;
};
