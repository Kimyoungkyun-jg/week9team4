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

public:
	AGameMode();
	void SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController);

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
