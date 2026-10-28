#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameHUD.h"

class AGameMode : public AActor
{
	DECLARE_CLASS(AGameMode, AActor)
	REFLECT_START(ClassName)
		CLASS_PROPERTY(DefaultPawnClass, APawn)
		CLASS_PROPERTY(PlayerControllerClass, APlayerController)
		CLASS_PROPERTY(HUDClass, AGameHUD)
		PROPERTY(CoinCount)
	REFLECT_END()
	LUA_BIND_START(AGameMode)
		LUA_FUNC(AllowsPlayerControllerInput)
	LUA_BIND_END()

public:
	AGameMode();
	void SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController);
	virtual bool AllowsPlayerControllerInput() const { return true; }
	// Camera mode policy is independent of movement/look input permission.
	virtual bool AllowsCameraModeToggle() const { return true; }
	// UI/menu modes keep the cursor free; gameplay/test modes can opt in.
	virtual bool WantsMouseCapture() const { return false; }

	UClass* GetDefaultPawnClass() const { return DefaultPawnClass; }
	UClass* GetPlayerControllerClass() const { return PlayerControllerClass; }
	UClass* GetHUDClass() const { return HUDClass; }
	bool SetDefaultPawnClass(UClass* Class);
	bool SetPlayerControllerClass(UClass* Class);
	bool SetHUDClass(UClass* Class);

	int32 GetCoinCount() const { return CoinCount; }
	void SetCoinCount(int32 InCount) { CoinCount = InCount; }
	int32 AddCoin(int32 Amount = 1) { CoinCount += Amount; return CoinCount; }

private:
	UClass* DefaultPawnClass = nullptr;
	UClass* PlayerControllerClass = nullptr;
	UClass* HUDClass = nullptr;
	int32 CoinCount = 0;
};
