#pragma once

#include "GameFramework/GameMode.h"

// Isolated camera test rules: no Pacman coins, ghosts, result UI, or scripts.
class ASpringArmTestGameMode : public AGameMode
{
	DECLARE_CLASS(ASpringArmTestGameMode, AGameMode)

public:
	ASpringArmTestGameMode();
	void BeginPlay() override;
	bool AllowsCameraModeToggle() const override { return true; }
	bool WantsMouseCapture() const override { return true; }
};
