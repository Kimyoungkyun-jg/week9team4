#pragma once

#include "GameFramework/GameMode.h"

struct FHitResult;
class ULuaScriptComponent;

class ATestGameMode : public AGameMode
{
	DECLARE_CLASS(ATestGameMode, AGameMode)

public:
	ATestGameMode();
	void NotifyProjectileHit(const FHitResult& Hit);

private:
	ULuaScriptComponent* ScriptComponent = nullptr;
};
