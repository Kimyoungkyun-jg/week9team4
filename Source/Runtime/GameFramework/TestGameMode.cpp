#include "EnginePCH.h"
#include "GameFramework/TestGameMode.h"
#include "Collision/HitResult.h"
#include "Components/ScriptComponent.h"

ATestGameMode::ATestGameMode()
{
	ScriptComponent = CreateDefaultSubobject<UScriptComponent>("GameModeScript");
	ScriptComponent->SetScriptPath("Assets/Scripts/Gamemode.lua");
}

void ATestGameMode::NotifyProjectileHit(const FHitResult& Hit)
{
	ScriptComponent->CallEvent("OnProjectileHit", FHitResult(Hit));
}
