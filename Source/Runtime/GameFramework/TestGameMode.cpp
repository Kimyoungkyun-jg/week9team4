#include "EnginePCH.h"
#include "GameFramework/TestGameMode.h"
#include "Collision/HitResult.h"
#include "Components/LuaScriptComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Lua/LuaManager.h"

ATestGameMode::ATestGameMode()
{
	SetHUDClass(ASampleHUD::StaticClass());
	ScriptComponent = CreateDefaultSubobject<ULuaScriptComponent>("GameModeScript");
	ScriptComponent->SetScriptPath("Assets/Scripts/Gamemode.lua");
}

void ATestGameMode::NotifyProjectileHit(const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	FLuaManager* LuaManager = GameInstance ? GameInstance->GetLuaManager() : nullptr;
	if (LuaManager && ScriptComponent)
		LuaManager->Call(ScriptComponent->GetScriptInstance(), "OnProjectileHit", FHitResult(Hit));
}
