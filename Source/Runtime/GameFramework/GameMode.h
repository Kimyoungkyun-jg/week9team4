#pragma once

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

class AGameMode : public AActor
{
	DECLARE_CLASS(AGameMode, AActor)
	REFLECT_START(ClassName)
		CLASS_PROPERTY(DefaultPawnClass, APawn)
		CLASS_PROPERTY(PlayerControllerClass, APlayerController)
	REFLECT_END()

public:
	AGameMode();
	void SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController);

	UClass* GetDefaultPawnClass() const { return DefaultPawnClass; }
	UClass* GetPlayerControllerClass() const { return PlayerControllerClass; }
	bool SetDefaultPawnClass(UClass* Class);
	bool SetPlayerControllerClass(UClass* Class);

private:
	UClass* DefaultPawnClass = nullptr;
	UClass* PlayerControllerClass = nullptr;
};
