#pragma once

#include "GameFramework/Character.h"
#include "Audio/FootstepCadence.h"
#include <random>

class AFlashlightActor;
class UCapsuleComponent;
class UHeadBobComponent;
class USpringArmComponent;

enum class EPlayerCameraMode : uint8 { FirstPerson, ThirdPerson };
class UFlashlightAttackComponent;

class APlayerPawn : public ACharacter
{
	DECLARE_CLASS(APlayerPawn, ACharacter)
	REFLECT_START(APlayerPawn)
		PROPERTY(Life)
	REFLECT_END()
	LUA_BIND_START(APlayerPawn)
		LUA_FUNC(GetSlomoGauge)
		LUA_FUNC(GetMaxSlomoGauge)
		LUA_FUNC(SetSlomoGauge)
		LUA_FUNC(AddSlomoGauge)
		LUA_FUNC(IsSlomoActive)
		LUA_FUNC(SetSlomoActive)
		LUA_FUNC(StartSlomo)
		LUA_FUNC(ConfigureFlashlight)
		LUA_FUNC(ConfigureFlashlightSwing)
		LUA_FUNC(ConfigureFlashlightAttack)
		LUA_FUNC(ConfigureFlashlightHitCameraShake)
		// LuaScriptComponent exposes Owner as AActor; resolve this game type explicitly.
		UserType["Cast"] = [](AActor* Actor) { return Cast<APlayerPawn>(Actor); };
	LUA_BIND_END()

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
	void PrimaryAction() override;
	bool IsInputSuppressed() const override;
	UFlashlightAttackComponent* GetFlashlightAttackComponent() const { return FlashlightAttack; }
	void SetFootstepInterval(float Seconds) { FootstepCadence.IntervalSeconds = Seconds; }
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	void ConfigureFlashlight(float DrainPerSecond, float AttackCost, float SwingSeconds, float AttackReach);
	void ConfigureFlashlightSwing(const FVector& Pivot, const FRotator& CoordinateRotation, float HalfAngle, float HandRadius);
	void ConfigureFlashlightAttack(float WindupSeconds, float RecoverySeconds, float CapsuleRadius, float HitStopSeconds, float GhostStaggerSeconds, float FlashMultiplier, float TrailAngleStep);
	void ConfigureFlashlightHitCameraShake(bool bEnabled, float Duration, float Scale, float Frequency, const FVector& LocationAmplitude, const FRotator& RotationAmplitude);
	int32 GetLife() const { return Life; }
	void SetLife(int32 InLife);
	void RespawnAtPlayerStart();
	// The hit sequence owns the camera until it explicitly returns control.
	void SetHeadBobSuspended(bool bSuspended);
	UHeadBobComponent* GetHeadBobComponent() const;

	float GetSlomoGauge() const { return SlomoGauge; }
	float GetMaxSlomoGauge() const { return MaxSlomoGauge; }
	void SetSlomoGauge(float NewGauge);
	void AddSlomoGauge(float Amount) { SetSlomoGauge(SlomoGauge + Amount); }
	bool IsSlomoActive() const { return bIsSlomoActive; }
	bool StartSlomo(float Dilation);
	void SetSlomoActive(bool bActive);

private:
	float SlomoGauge = 100.0f;
	float MaxSlomoGauge = 100.0f;
	float SlomoDrainRate = 25.0f;
	float SlomoRecoverRate = 12.5f;
	bool bIsSlomoActive = false;
	float WorldDilationBeforeSlomo = 1.0f;
	float PlayerDilationBeforeSlomo = 1.0f;
	void ResetWalkingEffects();
	FFootstepCadence FootstepCadence;
	std::mt19937 FootstepRandom{std::random_device{}()};
	AFlashlightActor* FlashlightActor = nullptr;
	UFlashlightAttackComponent* FlashlightAttack = nullptr;
	int32 Life = 3;
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation;
	FVector PlayerStartArmLocation = FVector::ZeroVector;
	float PlayerStartViewPitch = 0.0f;
	float ViewPitch = 0.0f;
	EPlayerCameraMode CameraMode = EPlayerCameraMode::FirstPerson;
	bool bCameraControlSuspended = false;
};
