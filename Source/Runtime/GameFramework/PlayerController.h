#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"

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
	AActor* GetViewTarget() const;
	void UpdateCameraManager(float DeltaTime);
	void SetHUD(AGameHUD* InHUD) { HUD = InHUD; }
	void InputKey(int32 Key, bool bDown);
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();
private:
	APawn* PossessedPawn = nullptr;
	AGameHUD* HUD = nullptr;
	APlayerCameraManager* PlayerCameraManager = nullptr;
	bool bKeyDown[256]{};
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;
};
