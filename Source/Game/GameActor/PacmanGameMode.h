#pragma once

#include "GameFramework/GameMode.h"

class APacmanGameMode : public AGameMode
{
	DECLARE_CLASS(APacmanGameMode, AGameMode)
	REFLECT_START(APacmanGameMode)
		PROPERTY(MaxCoinCount)
	REFLECT_END()
	LUA_BIND_START(APacmanGameMode)
		LUA_FUNC(GetCollectedCoinCount)
		LUA_FUNC(GetMaxCoinCount)
		LUA_FUNC(GetPlayerLife)
		LUA_FUNC(ShowGameResult)
	LUA_BIND_END()

public:
	APacmanGameMode();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	bool AllowsPlayerControllerInput() const override { return !bResultShown; }
	int32 GetCollectedCoinCount() const { return GetCoinCount(); }
	int32 GetMaxCoinCount() const { return MaxCoinCount; }
	int32 GetPlayerLife() const;
	double GetClearSeconds() const { return ClearSeconds; }
	void ShowGameResult(bool bCleared);

private:
	int32 MaxCoinCount = 0;
	class ULuaScriptComponent* ScriptComponent = nullptr;
	class UImageWidget* FadeImage = nullptr;
	uint64 FadeOverlayHandle = 0;
	float FadeElapsed = 0.0f;
	bool bPendingClear = false;
	double ClearSeconds = 0.0;
	enum class EResultFadePhase { None, FadeOut, FadeIn, Complete };
	EResultFadePhase FadePhase = EResultFadePhase::None;
	bool bResultShown = false;
};
