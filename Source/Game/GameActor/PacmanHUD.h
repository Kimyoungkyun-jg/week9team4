#pragma once

#include "GameFramework/GameHUD.h"

class APacmanHUD : public AGameHUD
{
	DECLARE_CLASS(APacmanHUD, AGameHUD)
public:
	APacmanHUD();
	void Tick(float DeltaTime) override;
	void ShowNameEntry();

protected:
	void OnWidgetShown(const FString& Path) override;

private:
	void LayoutNameEntry();
	void LayoutResultScreen();
	void LayoutGameUI();
	void ShowScoreboard();
	void SaveScore();
	double ClearSeconds = 0.0;
	bool bNameEntryOpen = false;
	bool bResultScreenOpen = false;
	bool bShowingGameOver = false;
	bool bScoreboardOpen = false;
	bool bScoreSaved = false;
};
