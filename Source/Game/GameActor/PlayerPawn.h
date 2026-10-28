#pragma once

#include "GameFramework/Character.h"
#include "Audio/FootstepCadence.h"
#include <random>

class AFlashlightActor;
class UCapsuleComponent;
class UHeadBobComponent;
class USpringArmComponent;

enum class EPlayerCameraMode : uint8 { FirstPerson, ThirdPerson };

class APlayerPawn : public ACharacter
{
	DECLARE_CLASS(APlayerPawn, ACharacter)
	REFLECT_START(APlayerPawn)
		PROPERTY(Life)
	REFLECT_END()

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	void AddControllerPitchInput(float Value) override;
	void ToggleCameraMode() override;
	void SetCameraMode(EPlayerCameraMode NewMode);
	bool IsThirdPersonMode() const { return CameraMode == EPlayerCameraMode::ThirdPerson; }
	USpringArmComponent* GetSpringArmComponent() const;
	USceneComponent* GetFlashlightPivot() const;
	void SetFootstepInterval(float Seconds) { FootstepCadence.IntervalSeconds = Seconds; }
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	int32 GetLife() const { return Life; }
	void SetLife(int32 InLife);
	void RespawnAtPlayerStart();
	// The hit sequence owns the camera until it explicitly returns control.
	void SetHeadBobSuspended(bool bSuspended);
	UHeadBobComponent* GetHeadBobComponent() const;

private:
	void ResetWalkingEffects();
	FFootstepCadence FootstepCadence;
	std::mt19937 FootstepRandom{std::random_device{}()};
	AFlashlightActor* FlashlightActor = nullptr;
	int32 Life = 3;
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation;
	FVector PlayerStartArmLocation = FVector::ZeroVector;
	float PlayerStartViewPitch = 0.0f;
	float ViewPitch = 0.0f;
	EPlayerCameraMode CameraMode = EPlayerCameraMode::FirstPerson;
	bool bCameraControlSuspended = false;
};
