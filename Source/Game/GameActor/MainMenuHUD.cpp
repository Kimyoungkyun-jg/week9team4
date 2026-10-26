#include "EnginePCH.h"
#include "MainMenuHUD.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

AMainMenuHUD::AMainMenuHUD()
{
	SetWidgetAssetPath("Assets/UI/MainMenu.ui.json");
}

void AMainMenuHUD::OnWidgetShown(const FString& Path)
{
	if (Path != GetWidgetAssetPath()) return;
	BindButton("Button4", [this]
	{
		if (FGameViewportClient* Viewport = GetGameViewport())
		{
			if (UGameInstance* GameInstance = Viewport->GetGameInstance()) GameInstance->StartRunTimer();
			Viewport->RequestSceneChange("Scenes/Pacman.scene", "Scenes/MainMenu.scene");
		}
	});
	BindButton("Button4Copy", [this]
	{
		if (FGameViewportClient* Viewport = GetGameViewport())
			if (UGameInstance* GameInstance = Viewport->GetGameInstance())
				GameInstance->RequestEndSession();
	});
}
