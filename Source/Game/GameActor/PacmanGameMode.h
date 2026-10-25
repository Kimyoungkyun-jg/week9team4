#pragma once

#include "GameFramework/GameMode.h"

class APacmanGameMode : public AGameMode
{
	DECLARE_CLASS(APacmanGameMode, AGameMode)
	REFLECT_START(APacmanGameMode)
		PROPERTY(MaxCoinCount)
	REFLECT_END()

public:
	APacmanGameMode();
	void BeginPlay() override;
	int32 GetMaxCoinCount() const { return MaxCoinCount; }

private:
	int32 MaxCoinCount = 0;
};
