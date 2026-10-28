#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"

#include "Camera/PlayerCameraManager.h"

class APawn;
class AGameHUD;
class APlayerCameraManager;

class APlayerController : public AActor
{
	DECLARE_CLASS(APlayerController, AActor)
	REFLECT_START(ClassName)
	REFLECT_END()
public:
	APlayerController();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	void ProcessPlayerInput(float DeltaTime);
	void GetPlayerViewCamera(FVector& OutLocation, FRotator& OutRotation) const;
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	AGameHUD* GetHUD() const { return HUD; }
	APlayerCameraManager* GetPlayerCameraManager() const;
	void SetViewTarget(AActor* InTarget);
	void SetViewTargetWithBlend(AActor* NewTarget, float BlendTime = 0.0f, EViewTargetBlendFunction BlendFunc = VTBlend_Cubic, float BlendExp = 2.0f, bool bLockOutgoing = false);
	AActor* GetViewTarget() const;
	void UpdateCameraManager(float DeltaTime);
	void SetHUD(AGameHUD* InHUD) { HUD = InHUD; }
	void InputKey(int32 Key, bool bDown);
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	// 입력 잠금 기능 (언리얼 규격)
	void SetIgnoreMoveInput(bool bNewMoveInput);
	bool IsMoveInputIgnored() const { return IgnoreMoveInputCount > 0; }
	void ResetIgnoreMoveInput() { IgnoreMoveInputCount = 0; }

	void SetIgnoreLookInput(bool bNewLookInput);
	bool IsLookInputIgnored() const { return IgnoreLookInputCount > 0; }
	void ResetIgnoreLookInput() { IgnoreLookInputCount = 0; }

	void SetCinematicMode(bool bInCinematicMode, bool bHidePlayer = false, bool bAffectsMovement = true, bool bAffectsTurning = true);
	bool IsInCinematicMode() const { return bCinematicMode; }

private:
	APawn* PossessedPawn = nullptr;
	AGameHUD* HUD = nullptr;
	APlayerCameraManager* PlayerCameraManager = nullptr;
	bool bKeyDown[256]{};
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;

	int32 IgnoreMoveInputCount = 0;
	int32 IgnoreLookInputCount = 0;
	bool bCinematicMode = false;
};
