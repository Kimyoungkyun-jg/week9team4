#include "EnginePCH.h"
#include "MainMenuHUD.h"
#include "ScoreboardUI.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "UI/Widget.h"
#include <algorithm>

AMainMenuHUD::AMainMenuHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	SetWidgetAssetPath("Assets/UI/MainMenu.ui.json");
}

void AMainMenuHUD::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bScoreboardOpen) ScoreboardUI::Layout(*this);
	else LayoutMainMenu();
}

void AMainMenuHUD::LayoutMainMenu()
{
	FGameViewportClient* Viewport = GetGameViewport();
	if (!Viewport) return;
	const float Scale = std::min(Viewport->GetWidth() / 1280.0f, Viewport->GetHeight() / 720.0f);
	if (Scale <= 0.0f) return;
	if (UImageWidget* Title = Cast<UImageWidget>(FindWidget("Image1")))
	{
		Title->SetPosition({52.0f * Scale, -165.0f * Scale});
		Title->SetSize({1085.0f * Scale, 362.0f * Scale});
	}
	const auto LayoutButton = [this, Scale](const char* Name, float X, float Y, float Width, float Height, float FontSize)
	{
		if (UButtonWidget* Button = Cast<UButtonWidget>(FindWidget(Name)))
		{
			Button->SetPosition({X * Scale, Y * Scale});
			Button->SetSize({Width * Scale, Height * Scale});
			Button->SetFontSize(FontSize * Scale);
		}
	};
	LayoutButton("Button4", 38.0f, 85.0f, 237.0f, 104.0f, 90.0f);
	LayoutButton("ScoreButton", 40.0f, 195.0f, 237.0f, 104.0f, 90.0f);
	LayoutButton("Button4Copy", 54.0f, 305.0f, 231.0f, 103.0f, 92.0f);
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
	LayoutMainMenu();
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
