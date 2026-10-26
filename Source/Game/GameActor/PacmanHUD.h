#pragma once

#include "GameFramework/GameHUD.h"

class APacmanHUD : public AGameHUD
{
	DECLARE_CLASS(APacmanHUD, AGameHUD)
public:
	APacmanHUD();
	void Tick(float DeltaTime) override;
};
