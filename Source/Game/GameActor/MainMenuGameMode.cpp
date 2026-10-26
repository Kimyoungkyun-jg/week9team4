#include "EnginePCH.h"
#include "MainMenuGameMode.h"
#include "MainMenuHUD.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	SetDefaultPawnClass(APawn::StaticClass());
	SetHUDClass(AMainMenuHUD::StaticClass());
}
