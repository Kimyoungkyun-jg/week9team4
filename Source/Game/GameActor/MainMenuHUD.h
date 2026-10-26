#pragma once

#include "GameFramework/GameHUD.h"

class AMainMenuHUD : public AGameHUD
{
	DECLARE_CLASS(AMainMenuHUD, AGameHUD)

public:
	AMainMenuHUD();
	void Tick(float DeltaTime) override;

protected:
	void OnWidgetShown(const FString& Path) override;

private:
	bool bScoreboardOpen = false;
};
