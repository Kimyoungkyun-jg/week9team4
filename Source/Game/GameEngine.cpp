#include "EnginePCH.h"
#include "GameEngine.h"
#include "GameActor/PlayerPawn.h"
#include "GameActor/FlashlightActor.h"
#include "GameActor/GhostActor.h"
#include "GameActor/GhostAIComponent.h"
#include "GameActor/MainMenuGameMode.h"
#include "GameActor/MainMenuHUD.h"
#include "GameActor/PacmanGameMode.h"
#include "GameActor/PacmanHUD.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Input/InputSystem.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/SceneRenderer.h"
#include "Render/Swapchain.h"
#include "Serialization/JsonArchive.h"
#include "GameFramework/TestGameMode.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "Components/BillboardComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

#include <utility>

namespace
{
	// 정적 라이브러리에서 씬이 이름으로 생성하는 클래스의 등록 코드를 링크한다.
	void RegisterGameSceneClasses()
	{
		(void)ATestGameMode::StaticClass();
		(void)APlayerPawn::StaticClass();
		(void)AFlashlightActor::StaticClass();
		(void)AGhostActor::StaticClass();
		(void)UGhostAIComponent::StaticClass();
		(void)AMainMenuGameMode::StaticClass();
		(void)AMainMenuHUD::StaticClass();
		(void)APacmanGameMode::StaticClass();
		(void)APacmanHUD::StaticClass();
		(void)ASampleHUD::StaticClass();
		(void)ADirectionalLightActor::StaticClass();
		(void)AParticleActor::StaticClass();
		(void)ASkyLightActor::StaticClass();
		(void)AStaticMeshActor::StaticClass();
		(void)ATextRenderActor::StaticClass();
		(void)UBillboardComponent::StaticClass();
		(void)UDirectionalLightComponent::StaticClass();
		(void)UParticleSubUVComponent::StaticClass();
		(void)USceneComponent::StaticClass();
		(void)USkyLightComponent::StaticClass();
		(void)UStaticMeshComponent::StaticClass();
		(void)UTextRenderComponent::StaticClass();
	}
}

FEngineConfig UGameEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori";
	Desc.Width = 0;
	Desc.Height = 0;
	Desc.bBorderless = true;
	Desc.SyncInterval = 0;   // 0 = VSync 끔 (FPS 측정용)
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = true;        
	return Desc;
}

bool UGameEngine::Init()
{
	if (!Super::Init()) return false;
	FRenderer* Renderer = GetEngineLoop().GetRenderer();
	if (!Renderer) return false;
	ViewPipeline.Init(Renderer);
	RegisterGameSceneClasses();

	GameContext = &CreateNewWorldContext(EWorldType::Game);
	GameContext->SetCurrentWorld(UWorld::CreateWorld(EWorldType::Game, false));
	UWorld* World = GameContext->World();
	if (!World || !FJsonArchive::LoadWorld(World, "Scenes/MainMenu.scene")) return false;

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	GameInstance->SetWorldContext(GameContext);
	GameInstance->SetEndSessionHandler([this] { GetEngineLoop().RequestExit(); });
	GameInstance->Init();
	GameContext->GameViewport = MakeShared<FGameViewportClient>();
	GameContext->GameViewport->Init(*GameContext, this);
	World->BeginPlay();
	return true;
}

void UGameEngine::Tick(float DeltaTime)
{
	if (!GameContext || !GameContext->World() || !GameContext->GameViewport) return;
	FGameViewportClient& Viewport = *GameContext->GameViewport;
	Viewport.MouseMove(nullptr, FInputSystem::GetMouseX(), FInputSystem::GetMouseY());
	for (int32 Key = 0; Key < 256; ++Key)
	{
		const EKeyCode Code = static_cast<EKeyCode>(Key);
		if (FInputSystem::IsKeyPressed(Code)) Viewport.InputKey(nullptr, Key, true);
		if (FInputSystem::IsKeyReleased(Code)) Viewport.InputKey(nullptr, Key, false);
	}
	for (const auto [Button, Code] : { std::pair{ EMouseButton::Left, EKeyCode::LButton },
		std::pair{ EMouseButton::Right, EKeyCode::RButton },
		std::pair{ EMouseButton::Middle, EKeyCode::MButton } })
	{
		if (FInputSystem::IsMousePressed(Button)) Viewport.InputKey(nullptr, static_cast<int32>(Code), true);
		if (FInputSystem::IsMouseReleased(Button)) Viewport.InputKey(nullptr, static_cast<int32>(Code), false);
	}
	Viewport.InputAxis(nullptr, 0, EGameInputAxis::MouseX, static_cast<float>(FInputSystem::GetMouseDeltaX()), DeltaTime);
	Viewport.InputAxis(nullptr, 0, EGameInputAxis::MouseY, static_cast<float>(FInputSystem::GetMouseDeltaY()), DeltaTime);
	Viewport.InputAxis(nullptr, 0, EGameInputAxis::MouseWheel, static_cast<float>(FInputSystem::GetWheelDelta()), DeltaTime);
	GameContext->World()->Tick(DeltaTime);
}

void UGameEngine::Render()
{
	if (!GameContext || !GameContext->World() || !GameContext->GameViewport) return;
	FGameViewportClient& Viewport = *GameContext->GameViewport;
	const uint32 Width = GetEngineLoop().GetViewportWidth();
	const uint32 Height = GetEngineLoop().GetViewportHeight();
	const FRect Rect{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) };
	const FSceneView View = Viewport.CalcSceneView(Rect);

	FSceneRenderOptions Options;
	FRenderer* Renderer = GetEngineLoop().GetRenderer();
	FSceneRenderer SceneRenderer(GameContext->World(), View, Viewport.GetRenderData());
	SceneRenderer.InitViews(Renderer, Options);
	if (!ViewPipeline.Render(SceneRenderer, Renderer, Viewport.GetViewTargets(), GameContext->World(), View, Options)) return;
	Viewport.RenderUI();

	FTexture2D* Backbuffer = GetEngineLoop().GetSwapchain()->GetBackbuffer();
	FTexture2D* SceneColor = Viewport.GetColorTarget();
	if (Backbuffer && SceneColor)
		RenderCommand::GetContext()->CopyResource(Backbuffer->GetRawPtr(), SceneColor->GetRawPtr());
}

void UGameEngine::PreExit()
{
	if (GameContext)
	{
		DestroyWorldContext(GameContext->World());
		GameContext = nullptr;
	}
}

