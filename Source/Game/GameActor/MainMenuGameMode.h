#pragma once

#include "GameFramework/GameMode.h"

class AMainMenuGameMode : public AGameMode
{
	DECLARE_CLASS(AMainMenuGameMode, AGameMode)

public:
	AMainMenuGameMode();
	bool AllowsPlayerControllerInput() const override { return false; }
};
