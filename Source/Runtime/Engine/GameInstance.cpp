#include "EnginePCH.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor.h"
#include "Collision/HitResult.h"
#include <sol/sol.hpp>

UGameInstance::UGameInstance() = default;
UGameInstance::~UGameInstance() = default;

void UGameInstance::Init()
{
	bEndSessionRequested = false;
	LuaState = MakeUnique<sol::state>();
	LuaState->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::coroutine);
	LuaState->set_function("Log", [](const FString& Message) { HTR_LOG(Info, "[Lua] {}", Message); });
	LuaState->set_function("EndGame", [this]() { RequestEndSession(); });
	LuaState->new_usertype<UObject>("UObject", sol::no_constructor,
		"UUID", &UObject::GetUUID,
		"GetName", &UObject::GetName);
	LuaState->new_usertype<AActor>("AActor", sol::no_constructor,
		sol::base_classes, sol::bases<UObject>());
	LuaState->new_usertype<FVector>("FVector", sol::no_constructor,
		"X", &FVector::X,
		"Y", &FVector::Y,
		"Z", &FVector::Z);
	LuaState->new_usertype<FHitResult>("FHitResult", sol::no_constructor,
		"bBlockingHit", &FHitResult::bBlockingHit,
		"Distance", &FHitResult::Distance,
		"ImpactPoint", &FHitResult::ImpactPoint,
		"ImpactNormal", &FHitResult::ImpactNormal);
}

void UGameInstance::RequestEndSession()
{
	if (bEndSessionRequested || !EndSessionHandler) return;
	bEndSessionRequested = true;
	EndSessionHandler();
}

void UGameInstance::Init()
{
	LuaManager = MakeUnique<FLuaManager>();
	LuaManager->Init();
}

void UGameInstance::Shutdown()
{
	if (LuaManager)
	{
		LuaManager->Shutdown();
		LuaManager.reset();
	}

	if (!WorldContext) return;
	if (WorldContext->GameViewport)
	{
		WorldContext->GameViewport->Reset();
		WorldContext->GameViewport.reset();
	}
	WorldContext->OwningGameInstance = nullptr;
	WorldContext = nullptr;
}

UWorld* UGameInstance::GetWorld() const
{
	return WorldContext ? WorldContext->World() : nullptr;
}

void UGameInstance::SetWorldContext(FWorldContext* Context)
{
	WorldContext = Context;
	if (Context) Context->OwningGameInstance = this;
}

bool UGameInstance::InitializeForPlayInEditor(int32 PIEInstance)
{
	UEngine* Engine = GetEngine();
	if (!Engine) return false;
	FWorldContext* Context = Engine->GetWorldContextFromPIEInstance(PIEInstance);
	if (!Context || !Engine->GetEditorWorld()) return false;
	SetWorldContext(Context);
	if (!Engine->CreatePIEWorldByDuplication(*Context, Engine->GetEditorWorld())) return false;
	Init();
	return true;
}

bool UGameInstance::StartPlayInEditorGameInstance()
{
	UWorld* World = GetWorld();
	if (!World || !WorldContext) return false;
	WorldContext->GameViewport = MakeShared<FGameViewportClient>();
	WorldContext->GameViewport->Init(*WorldContext, GetEngine());
	World->BeginPlay();
	return true;
}

UEngine* UGameInstance::GetEngine() const
{
	return GEngine;
}
