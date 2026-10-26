#pragma once

#include "GameFramework/GameHUD.h"

class AMainMenuHUD : public AGameHUD
{
	DECLARE_CLASS(AMainMenuHUD, AGameHUD)

public:
	AMainMenuHUD();

protected:
	void OnWidgetShown(const FString& Path) override;
};
