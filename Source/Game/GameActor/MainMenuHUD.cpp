#include "EnginePCH.h"
#include "MainMenuHUD.h"
#include "ScoreboardUI.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

AMainMenuHUD::AMainMenuHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	SetWidgetAssetPath("Assets/UI/MainMenu.ui.json");
}

void AMainMenuHUD::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bScoreboardOpen) ScoreboardUI::Layout(*this);
}

void AMainMenuHUD::OnWidgetShown(const FString& Path)
{
	if (Path == "Assets/UI/Scoreboard.ui.json")
	{
		bScoreboardOpen = true;
		ScoreboardUI::Layout(*this);
		ScoreboardUI::Populate(*this, std::nullopt);
		BindButton("BackToMenu", [this] { ShowWidget(GetWidgetAssetPath()); });
		return;
	}
	if (Path != GetWidgetAssetPath()) return;
	bScoreboardOpen = false;
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
	BindButton("ScoreButton", [this] { ShowWidget("Assets/UI/Scoreboard.ui.json"); });
}
