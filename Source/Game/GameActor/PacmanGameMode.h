#pragma once

#include "GameFramework/GameMode.h"

class APacmanGameMode : public AGameMode
{
	DECLARE_CLASS(APacmanGameMode, AGameMode)
	REFLECT_START(APacmanGameMode)
		PROPERTY(MaxCoinCount)
		PROPERTY(LifeCount)
	REFLECT_END()
	LUA_BIND_START(APacmanGameMode)
		LUA_FUNC(GetCollectedCoinCount)
		LUA_FUNC(GetMaxCoinCount)
		LUA_FUNC(GetPlayerLife)
		LUA_FUNC(IsResolvingGhostHit)
		LUA_FUNC(ShowGameResult)
	LUA_BIND_END()

public:
	APacmanGameMode();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	bool AllowsPlayerControllerInput() const override { return !bResultShown && !bGhostHitPlaying; }
	bool WantsMouseCapture() const { return !bResultShown; }
	int32 GetCollectedCoinCount() const { return GetCoinCount(); }
	int32 GetMaxCoinCount() const { return MaxCoinCount; }
	int32 GetPlayerLife() const;
	bool IsResolvingGhostHit() const { return bGhostHitPlaying; }
	void StartGhostHit(class AGhostActor* Ghost, class APlayerPawn* Player);
	double GetClearSeconds() const { return ClearSeconds; }
	void ShowGameResult(bool bCleared);

private:
	int32 MaxCoinCount = 0;
	int32 LifeCount = 3;
	class ULuaScriptComponent* ScriptComponent = nullptr;
	class UImageWidget* FadeImage = nullptr;
	uint64 FadeOverlayHandle = 0;
	float FadeElapsed = 0.0f;
	bool bPendingClear = false;
	double ClearSeconds = 0.0;
	enum class EResultFadePhase { None, FadeOut, FadeIn, Complete };
	EResultFadePhase FadePhase = EResultFadePhase::None;
	bool bResultShown = false;
	enum class EGhostHitPhase { None, CloseUp, FadeToBlack, FadeFromBlack };
	EGhostHitPhase GhostHitPhase = EGhostHitPhase::None;
	float GhostHitElapsed = 0.0f;
	float GhostHitCooldown = 0.0f;
	class AGhostActor* HitGhost = nullptr;
	class APlayerPawn* HitPlayer = nullptr;
	class UCameraComponent* HitCamera = nullptr;
	class APointLightActor* HitLight = nullptr;
	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation;
	FRotator SavedGhostRotation;
	float SavedCameraFov = 60.0f;
	class UImageWidget* HitImage = nullptr;
	uint64 HitOverlayHandle = 0;
	bool bGhostHitPlaying = false;
	void TickGhostHit(float DeltaTime);
	void ResolveGhostHit();
};
