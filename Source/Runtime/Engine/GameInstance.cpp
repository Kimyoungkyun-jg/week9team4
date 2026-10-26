#include "EnginePCH.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

UGameInstance::UGameInstance() = default;
UGameInstance::~UGameInstance() = default;

void UGameInstance::Init()
{
	bEndSessionRequested = false;
	bRunTimerRunning = false;
	LastRunSeconds = 0.0;
	LuaManager = MakeUnique<FLuaManager>();
	LuaManager->Init(*this);
}

void UGameInstance::StartRunTimer()
{
	RunStartedAt = std::chrono::steady_clock::now();
	LastRunSeconds = 0.0;
	bRunTimerRunning = true;
}

double UGameInstance::GetRunElapsedSeconds() const
{
	return bRunTimerRunning
		? std::chrono::duration<double>(std::chrono::steady_clock::now() - RunStartedAt).count()
		: LastRunSeconds;
}

double UGameInstance::StopRunTimer()
{
	if (bRunTimerRunning) LastRunSeconds = GetRunElapsedSeconds();
	bRunTimerRunning = false;
	return LastRunSeconds;
}

void UGameInstance::RequestEndSession()
{
	if (bEndSessionRequested || !EndSessionHandler) return;
	bEndSessionRequested = true;
	EndSessionHandler();
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
