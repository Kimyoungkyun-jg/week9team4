#pragma once

#include "GameFramework/GameHUD.h"

class APacmanHUD : public AGameHUD
{
	DECLARE_CLASS(APacmanHUD, AGameHUD)
public:
	APacmanHUD();
	void Tick(float DeltaTime) override;

protected:
	void OnWidgetShown(const FString& Path) override;

private:
	void ShowNameEntry();
	void LayoutNameEntry();
	void ShowScoreboard();
	void SaveScore();
	double ClearSeconds = 0.0;
	bool bNameEntryOpen = false;
	bool bScoreboardOpen = false;
	bool bScoreSaved = false;
};
