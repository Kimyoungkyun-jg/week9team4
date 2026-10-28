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
		UserType["Cast"] = [](AGameMode* Mode) { return Cast<APacmanGameMode>(Mode); };
		LUA_FUNC(HasGameplayStarted)
		LUA_FUNC(HasResultCinematicStarted)
		LUA_FUNC(HasClearCinematicStarted)
		LUA_DELEGATE(OnGameplayStarted)
		LUA_DELEGATE(OnResultCinematicStarted)
		LUA_DELEGATE(OnClearCinematicStarted)
		LUA_FUNC(GetCollectedCoinCount)
		LUA_FUNC(GetMaxCoinCount)
		LUA_FUNC(GetPlayerLife)
		LUA_FUNC(IsResolvingGhostHit)
		LUA_FUNC(ShowGameResult)
	LUA_BIND_END()

public:
	APacmanGameMode();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	bool AllowsPlayerControllerInput() const override { return !bResultShown && !bGhostHitPlaying && !bIntroPlaying; }
	bool AllowsCameraModeToggle() const override { return false; }
	bool WantsMouseCapture() const override { return !bResultShown && !bIntroPlaying; }
	int32 GetCollectedCoinCount() const { return GetCoinCount(); }
	int32 GetMaxCoinCount() const { return MaxCoinCount; }
	int32 GetPlayerLife() const;
	bool IsResolvingGhostHit() const { return bGhostHitPlaying; }
	void StartGhostHit(class AGhostActor* Ghost, class APlayerPawn* Player);
	double GetClearSeconds() const { return ClearSeconds; }
	void ShowGameResult(bool bCleared);
	bool HasGameplayStarted() const { return bGameplayStarted; }
	bool HasResultCinematicStarted() const { return bResultCinematicStarted; }
	bool HasClearCinematicStarted() const { return bClearCinematicStarted; }
	TMulticastDelegate<> OnGameplayStarted;
	TMulticastDelegate<> OnResultCinematicStarted;
	TMulticastDelegate<> OnClearCinematicStarted;

private:
	int32 MaxCoinCount = 0;
	int32 LifeCount = 3;
	class ULuaScriptComponent* ScriptComponent = nullptr;
	class UImageWidget* FadeImage = nullptr;
	class ALevelSequenceActor* ResultSequenceActor = nullptr;
	class ALevelSequenceActor* IntroSequenceActor = nullptr;
	bool bIntroPlaying = false;
	bool bGameplayStarted = false;
	bool bResultCinematicStarted = false;
	bool bClearCinematicStarted = false;
	uint64 FadeOverlayHandle = 0;
	float FadeElapsed = 0.0f;
	bool bPendingClear = false;
	double ClearSeconds = 0.0;
	enum class EResultFadePhase { None, FadeOut, AwaitingBlackFrame, FadeIn, Complete };
	EResultFadePhase FadePhase = EResultFadePhase::None;
	bool bResultShown = false;
	enum class EGhostHitPhase { None, CloseUp, FadeToBlack, AwaitingBlackFrame, FadeFromBlack };
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
	void CommitGameResult(bool bCleared);
	void BeginResultFade(bool bCleared);
	void OnResultFadeRendered();
	void OnResultSequenceFinished();
	void PlayIntroSequence();
	void OnIntroSequenceFinished();
	void TickGhostHit(float DeltaTime);
	void ResolveGhostHit();
	void ReleaseGhostHitCamera();
	class APlayerCameraManager* GetPlayerCameraManager() const;
};
