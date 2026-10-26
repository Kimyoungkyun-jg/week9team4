#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "GameActor/PlayerPawn.h"
#include "GameActor/FlashlightActor.h"
#include "GameActor/CoinActor.h"
#include "GameActor/GhostActor.h"
#include "GameActor/MainMenuGameMode.h"
#include "GameActor/MainMenuHUD.h"
#include "GameActor/PacmanGameMode.h"
#include "GameActor/PacmanHUD.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"

#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor/LightActorBase.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/UIEditor/WidgetEditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

namespace
{
	DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
	DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
	DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
	DECLARE_CYCLE_STAT("Capture World", STAT_CaptureWorld);
	DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);
	DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);
}

#include "Core/SplashScreen.h"

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori Engine";
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = "Resources/Splash.png";
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
		return false;
	APlayerPawn::StaticClass();
	AFlashlightActor::StaticClass();
	ACoinActor::StaticClass();
	AGhostActor::StaticClass();
	AMainMenuGameMode::StaticClass();
	AMainMenuHUD::StaticClass();
	APacmanGameMode::StaticClass();
	APacmanHUD::StaticClass();

	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();

	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	EditorUI = MakeUnique<FEditorUI>();
	EditorUI->Init();

	EditorUI->SetNewSceneCallback([this]() { CreateNewScene(); });
	EditorUI->SetOpenSceneCallback([this]() { OpenScene(); });
	EditorUI->SetSaveSceneCallback([this]() { SaveCurrentScene(); });
	EditorUI->SetSaveSceneAsCallback([this]() { SaveSceneAs(); });

	OutputLogPanel = EditorUI->AddEditorPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);
	HTR_LOG(Info, "Editor Initialize...");

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	GridRenderer = MakeUnique<FGridRenderer>();
	GridRenderer->Init(Renderer);
	MapTileRenderer = MakeUnique<FMapTileRenderer>();
	MapTileRenderer->Init();

	GizmoRenderer = MakeUnique<FGizmoRenderer>();
	GizmoRenderer->Init(Renderer);

	Gizmo = MakeUnique<FGizmo>();

	DetailsPanel = EditorUI->AddEditorPanel<FDetailsPanel>();
	DetailsPanel->SetImGuiRenderer(ImGuiRenderer.get());
	DetailsPanel->SetShadowPreviewQuery([this](const ULightComponent* Light, FTexture2DArray*& Atlas,
		TArray<FLightShadowRenderData>& Shadows)
	{
		Atlas = nullptr;
		Shadows.Reset();
		if (!Light || !ViewportsPanel) return false;
		const int32 Index = ViewportsPanel->GetActiveViewIndex();
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client || !Client->IsActive() || Client->GetRect().Width <= 0 || Client->GetRect().Height <= 0)
			return false;
		if (PlayWorld && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport && Index == PIEStartEditorViewIndex)
		{
			FWorldContext* Context = GetPIEWorldContext();
			if (!Context || !Context->GameViewport || Light->GetWorld() != Context->World()) return false;
			return Context->GameViewport->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
		}
		if (Light->GetWorld() != (PlayWorld ? PlayWorld : EditorWorld)) return false;
		return Client->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
	});

	EditorControlsPanel = EditorUI->AddEditorPanel<FEditorControlsPanel>();
	ViewportsPanel = EditorUI->AddEditorPanel<FViewportsPanel>();
	ContentDrawerPanel = EditorUI->AddEditorPanel<FContentDrawerPanel>();
	WidgetEditorPanel = EditorUI->AddEditorPanel<FWidgetEditorPanel>();
	WidgetEditorPanel->SetViewportsPanel(ViewportsPanel);
	ContentDrawerPanel->SetOpenUIAssetCallback([this](const FString& Path)
	{
		if (!WidgetEditorPanel->RequestOpenAsset(Path))
			HTR_LOG(Error, "Failed to open UI asset: {}", Path);
	});

	// OutLine
	OutlineRenderer = MakeUnique<FOutlineRenderer>();
	OutlineRenderer->Init(Renderer);

	SceneDepthRenderer = MakeUnique<FSceneDepthRenderer>();
	SceneDepthRenderer->Init(Renderer);
	CascadeFloorRenderer = MakeUnique<FCascadeFloorRenderer>();
	CascadeFloorRenderer->Init();

	SettingsPanel = EditorUI->AddEditorPanel<FSettingsPanel>();
	DetailsPanel->SetEditorSettings(&SettingsPanel->GetSettings());
	SettingsPanel->SetEditScriptCallback([this](const FString& Path)
	{
		DetailsPanel->SetOpen(true);
		DetailsPanel->OpenScriptEditor(Path);
	});
	OutputLogPanel->SetEditorSettings(&SettingsPanel->GetMutableSettings());

	Outline = MakeUnique<FOutline>();

	SystemFont = UAssetManager::GetAssetByPath<UFont>("Assets/Fonts/Pretendard.json");

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	// 화면 나눔 비율 설정 가져오기
	ViewportSplitRatio = {
		SettingsPanel->GetSettings().MultipleViewportsHorizontal,
		SettingsPanel->GetSettings().MultipleViewportsVertical
	};
	// SingleView에 사용할 인덱스 설정
	SingleViewportIndex = SettingsPanel->GetSettings().MultipleViewportsSingleViewIndex;
	// 뷰포트 레이아웃 설정
	ViewportLayoutMode = SettingsPanel->GetSettings().bMultipleViewportsSingle
		? ELayoutMode::Single
		: ELayoutMode::QuadSplit;
	EditorWorld->GetMainCamera()->GetCameraComponent()->SetExternalInputManaged(true);

	OutlinerPanel = EditorUI->AddEditorPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
					if ((Primitive = Cast<UPrimitiveComponent>(C))) break;
			}
			Outline->SetTarget(Primitive);
		});

	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				AActor* Owner = SceneComponent->GetOwner();
				if (Owner && SceneComponent == Owner->GetRootComponent())
				{
					Gizmo->SetTarget(SceneComponent, true);
				}
				else
				{
					Gizmo->SetTarget(SceneComponent);
				}
			}
			else
			{
				Gizmo->SetTarget(nullptr);
				if (!Component) Outline->SetTarget(nullptr);
			}
		}
	);

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		}
	);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(Renderer, GetActiveWorld());

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(EditorWorld);
	EditorControlsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	EditorControlsPanel->SetDuplicateActorCallback([this]() { DuplicateSelectedActor(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportsPanel(ViewportsPanel);
	EditorControlsPanel->SetSettingsPanel(SettingsPanel);

	SettingsPanel->SetWorld(EditorWorld);
	SettingsPanel->SetViewportsPanel(ViewportsPanel);
	ViewportsPanel->SetLayoutMode(ViewportLayoutMode, SingleViewportIndex);

	for (int32 i = 0; i < 4; ++i)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i))
		{
			Client->SetWorld(EditorWorld);
			AllViewportClients.Add(Client);
		}
	}
	SettingsPanel->ApplyViewportSettings();

	ViewPipeline.Init(Renderer);

	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true } };

	Play = { "Play",   "Play World",        UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Play.png"),  Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	Pause = { "Pause",  "Pause",			UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Pause.png"), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; if (PlayWorld) PlayWorld->SetPaused(true); };

	Resume = { "Resume", "Resume",          UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Play.png"),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; if (PlayWorld) PlayWorld->SetPaused(false); };

	StepFrame = { "Step", "Step One Frame", UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Step.png"),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested= true; };

	Stop = { "Stop",   "Stop",				UAssetManager::GetAssetByPath<UTexture2D>("Assets/Icons/Toolbar/Stop.png"),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	EditorUI->GetToolbar()->SetCommands({ Play, Pause, Resume, StepFrame, Stop });
	EditorUI->GetToolbar()->SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });

	return true;
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창 닫기 요청 처리
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
		RequestEndPlayMap();
	if (bRequestEndPlayMapQueued) { bRequestEndPlayMapQueued = false; EndPlayMap(); }
	if (bPlaySessionRequested) { bPlaySessionRequested = false; if (!PlayWorld) StartPlayInEditorSession(); }
	if (FWorldContext* Context = GetPIEWorldContext(); Context && Context->GameViewport)
		Context->GameViewport->ProcessPendingSceneChange();

	BeginFrame(DeltaTime);
	UpdateViewportState(DeltaTime);
	TickWorldAndEditor(DeltaTime);
	RenderViewports();
	EndFrame();
}

// DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
void UEditorEngine::BeginFrame(const float DeltaTime)
{
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteActor(OutlinerPanel->GetSelectedActor());
}

// 패널 요청과 입력을 뷰포트 클라이언트에 전달해 레이아웃과 카메라 상태를 갱신한다.
void UEditorEngine::UpdateViewportState(const float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);

	const FVector2 ViewportSize = ViewportsPanel->GetContentSize();
	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();

	ELayoutMode RequestedLayout{};
	int32 RequestedSingleViewIndex = SingleViewportIndex;

	if (ViewportsPanel->ConsumeLayoutRequest(RequestedLayout, RequestedSingleViewIndex))
	{
		if (RequestedLayout == ELayoutMode::Single)
			SingleViewportIndex = RequestedSingleViewIndex;
		ViewportLayoutMode = RequestedLayout;

		FEditorSettings& Settings = SettingsPanel->GetMutableSettings();
		Settings.bMultipleViewportsSingle = RequestedLayout == ELayoutMode::Single;
		Settings.MultipleViewportsSingleViewIndex = RequestedSingleViewIndex;
	}

	const float HorizontalDrag = ViewportsPanel->ConsumeHorizontalDrag();
	const float VerticalDrag = ViewportsPanel->ConsumeVerticalDrag();
	if (HorizontalDrag != 0.0f)
		ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Horizontal, HorizontalDrag, ViewportSize, 0.1f);
	if (VerticalDrag != 0.0f)
		ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Vertical, VerticalDrag, ViewportSize, 0.1f);
	if (HorizontalDrag != 0.0f || VerticalDrag != 0.0f)
	{
		SettingsPanel->GetMutableSettings().MultipleViewportsHorizontal = ViewportSplitRatio.Horizontal;
		SettingsPanel->GetMutableSettings().MultipleViewportsVertical = ViewportSplitRatio.Vertical;
	}

	FRect ViewRects[4]{};
	if (ViewportLayoutMode == ELayoutMode::Single)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			ViewRects[i] = (i == SingleViewportIndex)
				? FRect{ 0.0f, 0.0f, ViewportSize.X, ViewportSize.Y }
				: FRect{};
		}
	}
	else
	{
		ComputeViewRects(ViewportSplitRatio, ViewportSize, ViewRects);
	}

	for (int32 i = 0; i < 4; ++i)
	{
		const bool bSlotActive = (ViewportLayoutMode == ELayoutMode::Single)
			? (i == SingleViewportIndex)
			: true;
		ViewportsPanel->SetView(i, ViewRects[i], bSlotActive);
	}

	// 마우스 호버 감지
	int32 HoveredViewIndex = -1;
	if (ViewportsPanel->IsHovered())
	{
		for (int32 i = 0; i < 4; ++i)
		{
			if (ViewRects[i].Width > 0.0f && ViewRects[i].Height > 0.0f &&
				LocalMousePosition.X >= ViewRects[i].X &&
				LocalMousePosition.X < ViewRects[i].X + ViewRects[i].Width &&
				LocalMousePosition.Y >= ViewRects[i].Y &&
				LocalMousePosition.Y < ViewRects[i].Y + ViewRects[i].Height)
			{
				HoveredViewIndex = i;
				break;
			}
		}
	}

	// 마우스 캡처 및 활성 뷰 결정
	static int32 CapturedViewportIndex = -1;
	if (FInputSystem::IsMouseReleased(EMouseButton::Right))
	{
		CapturedViewportIndex = -1;
	}
	if (FInputSystem::IsMousePressed(EMouseButton::Right) && HoveredViewIndex != -1)
	{
		CapturedViewportIndex = HoveredViewIndex;
		ImGui::SetWindowFocus("Viewports");
	}
	if (CapturedViewportIndex != -1)
	{
		ViewportsPanel->SetActiveViewIndex(CapturedViewportIndex);
	}
	else if (FInputSystem::IsMousePressed(EMouseButton::Left) && HoveredViewIndex != -1)
	{
		ViewportsPanel->SetActiveViewIndex(HoveredViewIndex);
	}

	const int32 WheelDelta = FInputSystem::GetWheelDelta();
	const float MoveSpeed = 10.0f * SettingsPanel->GetSettings().CameraSpeed;
	const float MouseSens = 0.2f * SettingsPanel->GetSettings().MouseSensitivity;

	for (int32 i = 0; i < 4; ++i)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i);
		if (!Client || !Client->IsActive() || (PlayWorld && i == PIEViewIndex))
			continue;

		Client->TickInput(DeltaTime, i == CapturedViewportIndex,
			i == HoveredViewIndex, WheelDelta, MoveSpeed, MouseSens);
	}

	// 활성 뷰포트 시점 동기화
	const int32 ActiveIndex = ViewportsPanel->GetActiveViewIndex();
	if (!PlayWorld)
	{
		if (FEditorViewportClient* ActiveClient = ViewportsPanel->GetViewportClient(ActiveIndex))
		{
			if (ACameraActor* MainCam = EditorWorld->GetMainCamera())
			{
				if (UCameraComponent* CamCom = MainCam->GetCameraComponent())
				{
					CamCom->SetRelativeLocation(ActiveClient->GetViewLocation());
					CamCom->SetRelativeRotation(ActiveClient->GetViewRotation());
					CamCom->SetFieldOfView(ActiveClient->GetViewFOV());
					CamCom->SetOrthoWidth(ActiveClient->GetOrthoWidth());
					CamCom->SetIsOrthogonal(!ActiveClient->IsPerspective());
				}
			}
		}
	}
}

// 월드를 한 번 Tick·Capture한 뒤 에디터와 피킹을 갱신한다.
void UEditorEngine::TickWorldAndEditor(const float DeltaTime)
{
	// 게임 입력 처리
	if (PlayWorld)
	{
		TickPIEInput(DeltaTime);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->SetPaused(false);
			GetActiveWorld()->Tick(1.0f / 60.0f);
			GetActiveWorld()->SetPaused(true);
			bStepRequested = false;
		}
		else if(!bIsPaused)
			GetActiveWorld()->Tick(DeltaTime);

	}
	UpdatePIEViewCamera();
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorUI->Tick(DeltaTime);
	}
	const auto InvalidComponent = [](const UActorComponent* Component)
	{ return Component && (!IsValid(Component) || Component->IsBeingDestroyed()); };
	if (InvalidComponent(Gizmo->GetTarget())) Gizmo->SetTarget(nullptr, false);
	if (InvalidComponent(Outline->GetTarget())) Outline->SetTarget(nullptr);
	UpdateGizmoAndPicking();
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld)
		PlayWorld->GetScene().UpdateAllTransforms();
}

// 활성 View별 렌더를 수행한다.
void UEditorEngine::RenderViewports()
{
	const bool bIsPIE = (PlayWorld != nullptr);
	const ULightComponent* PreviewLight = DetailsPanel ? DetailsPanel->GetSelectedLight() : nullptr;
	// Invalidate skipped views too: never expose a previous frame's atlas range.
	for (FEditorViewportClient* Client : AllViewportClients)
	{
		if (!Client) continue;
		Client->GetRenderData().PreviewLight = PreviewLight;
		Client->GetRenderData().PreviewShadowRange = {};
	}
	if (bIsPIE)
	{
		if (FWorldContext* Context = GetPIEWorldContext())
		{
			if (Context->GameViewport)
			{
				Context->GameViewport->GetRenderData().PreviewLight = PreviewLight;
				Context->GameViewport->GetRenderData().PreviewShadowRange = {};
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < AllViewportClients.Num(); ++ViewIndex) {
		FEditorViewportClient* ViewClient = AllViewportClients[ViewIndex];
		if (!ViewClient || !ViewClient->IsActive())
			continue;

		const FRect& ViewRect = ViewClient->GetRect();
		if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
			continue;

		bool bIsGameView = false;
		int32 TargetPlayerIndex = -1;

		if (bIsPIE && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport &&
			ViewIndex == PIEStartEditorViewIndex)
		{
			bIsGameView = true;
			TargetPlayerIndex = 0;
		}

		// 선택된 게임 뷰포트와 나머지 에디터 뷰포트는 같은 PIE 월드를 본다.
		UWorld* CurrentWorld = bIsPIE ? PlayWorld : EditorWorld;

		FSceneView SceneView;
		FSceneRenderOptions Options;
		if (bIsGameView && TargetPlayerIndex >= 0)
		{
			FGameViewportClient* GameClient = nullptr;
			for (int32 WorldIdx = 0; WorldIdx < WorldContextlist.Num(); ++WorldIdx)
			{
				if (WorldContextlist[WorldIdx].WorldType == EWorldType::PIE &&
					WorldContextlist[WorldIdx].PIEInstance == TargetPlayerIndex &&
					WorldContextlist[WorldIdx].GameViewport)
				{
					GameClient = WorldContextlist[WorldIdx].GameViewport.get();
					BuildSceneRenderOptions(GameClient, Options);

					if (WorldContextlist[WorldIdx].World())
					{
						CurrentWorld = WorldContextlist[WorldIdx].World();
					}

					SceneView = GameClient->CalcSceneView(ViewRect);

					FSceneRenderer SceneRenderer(CurrentWorld, SceneView, GameClient->GetRenderData());
					SceneRenderer.InitViews(Renderer, Options);

					RenderGameFrame(GameClient, SceneView, Options, SceneRenderer, GameClient->IsSIEMode());

					break;
				}
			}
		}
		else
		{
			BuildSceneRenderOptions(ViewClient, bIsGameView, Options);

			SceneView = ViewClient->CalcSceneView(ViewRect);

			FSceneRenderer SceneRenderer(CurrentWorld, SceneView, ViewClient->GetRenderData());
			SceneRenderer.InitViews(Renderer, Options);

			RenderFrame(ViewClient, SceneView, Options, SceneRenderer, CurrentWorld, bIsGameView);
		}
	}

	// 별도 창(FWindow) 모드인 경우 PIE 창 타깃에 렌더링하고 스왑체인 백버퍼로 복사한다.
	bPIEWindowRendered = false;
	if (bIsPIE && IsPIEInWindow() && UpdatePIEWindowTargets())
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (Context && Context->GameViewport && Context->World())
		{
			FGameViewportClient* GameClient = Context->GameViewport.get();
			const uint32 Width = GameClient->GetWidth();
			const uint32 Height = GameClient->GetHeight();
			if (Width > 0 && Height > 0)
			{
				FSceneRenderOptions Options;
				BuildSceneRenderOptions(GameClient, Options);

				FRect ViewRect{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) };
				FSceneView SceneView = GameClient->CalcSceneView(ViewRect);

				FSceneRenderer SceneRenderer(Context->World(), SceneView, GameClient->GetRenderData());
				SceneRenderer.InitViews(Renderer, Options);

				RenderGameFrame(GameClient, SceneView, Options, SceneRenderer, GameClient->IsSIEMode());

				CopyPIEWindowToBackbuffer();
			}
		}
	}
}

void UEditorEngine::UpdatePIEViewCamera()
{
	if (!PlayWorld || PIEViewIndex == InvalidViewIndex)
		return;
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport)
		return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	if (PIEViewIndex >= 0 && PIEViewIndex < AllViewportClients.Num())
	{
		if (FEditorViewportClient* View = AllViewportClients[PIEViewIndex])
		{
			const FRect& GameRect = View->GetRect();
			if (GameRect.Width <= 0.0f || GameRect.Height <= 0.0f) return;
			const FSceneView GameView = GameViewport.CalcSceneView(GameRect);
			View->SetViewLocation(GameView.ViewLocation);
			View->SetViewRotation(GameView.ViewRotation);
			View->SetViewFOV(GameView.FieldOfView);
			View->SetOrthoWidth(GameView.OrthoWidth);
			View->SetIsPerspective(GameView.bIsPerspective);
		}
	}
}

bool UEditorEngine::IsSimulatingInEditor() const
{
	for (const FWorldContext& Context : WorldContextlist)
		if (Context.WorldType == EWorldType::PIE && Context.GameViewport)
			return Context.GameViewport->IsSIEMode();
	return false;
}

void UEditorEngine::TickPIEInput(float DeltaTime)
{
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport) return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	const FVector2 Mouse = ViewportsPanel->GetLocalMousePosition();
	const int32 InputViewIndex = IsPIEInWindow() && GameViewport.IsSIEMode()
		? PIEStartEditorViewIndex : PIEViewIndex;
	const FRect PIEViewRect = (InputViewIndex >= 0 && InputViewIndex < AllViewportClients.Num() && AllViewportClients[InputViewIndex])
		? AllViewportClients[InputViewIndex]->GetRect() : FRect{};
	const bool bMouseOverPIE = Mouse.X >= PIEViewRect.X && Mouse.Y >= PIEViewRect.Y &&
		Mouse.X < PIEViewRect.X + PIEViewRect.Width && Mouse.Y < PIEViewRect.Y + PIEViewRect.Height;
	const int32 ActiveViewIdx = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	const bool bEditorSIEFocus = IsPIEInWindow() && GameViewport.IsSIEMode() &&
		::GetForegroundWindow() == MainWindow->GetHandle() &&
		ActiveViewIdx == PIEStartEditorViewIndex &&
		(ViewportsPanel->IsHovered() || FInputSystem::IsMouseDown(EMouseButton::Right)) &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
	const bool bFocused = IsPIEInWindow()
		? ::GetForegroundWindow() == PIEWindow->GetHandle() || bEditorSIEFocus
		: ::GetForegroundWindow() == MainWindow->GetHandle() &&
			ActiveViewIdx == InputViewIndex &&
			(ViewportsPanel->IsHovered() || (GameViewport.IsSIEMode() && FInputSystem::IsMouseDown(EMouseButton::Right))) &&
			!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
	if (!bFocused) { GameViewport.LostFocus(); return; }
	if (!IsPIEInWindow() &&
		(FInputSystem::IsMousePressed(EMouseButton::Left) || FInputSystem::IsMousePressed(EMouseButton::Right)) &&
		!bMouseOverPIE)
	{
		GameViewport.LostFocus();
		return;
	}
	if (IsPIEInWindow() && !bEditorSIEFocus)
	{
		POINT Cursor{};
		::GetCursorPos(&Cursor);
		::ScreenToClient(PIEWindow->GetHandle(), &Cursor);
		GameViewport.MouseMove(nullptr, Cursor.x, Cursor.y);
	}
	else
		GameViewport.MouseMove(nullptr, static_cast<int32>(Mouse.X - PIEViewRect.X),
			static_cast<int32>(Mouse.Y - PIEViewRect.Y));

	if (FInputSystem::IsKeyPressed(EKeyCode::F8))
		GameViewport.InputKey(nullptr, static_cast<int32>(EKeyCode::F8), true);
	if (GameViewport.ConsumeSIERequest())
	{
		if (GameViewport.IsSIEMode()) GameViewport.EndSIEMode();
		else GameViewport.BeginSIEMode();
		OnActiveWorldChanged();
	}
	if (FInputSystem::IsKeyPressed(EKeyCode::Escape))
		GameViewport.InputKey(nullptr, static_cast<int32>(EKeyCode::Escape), true);
	if (GameViewport.ConsumeExitRequest())
	{
		RequestEndPlayMap();
		return;
	}
	constexpr EKeyCode Keys[] = { EKeyCode::W, EKeyCode::A, EKeyCode::S, EKeyCode::D,
		EKeyCode::E, EKeyCode::Q, EKeyCode::Space, EKeyCode::Shift,
		EKeyCode::Backspace, EKeyCode::Enter };
	for (EKeyCode Key : Keys)
	{
		const bool bTextEditKey = Key == EKeyCode::Backspace || Key == EKeyCode::Enter;
		if (FInputSystem::IsKeyPressed(Key))
		{
			if (GameViewport.IsSIEMode() && !bTextEditKey) GameViewport.GetKeyInputBySIEMode(static_cast<int32>(Key), true);
			else GameViewport.InputKey(nullptr, static_cast<int32>(Key), true);
		}
		if (FInputSystem::IsKeyReleased(Key))
		{
			if (GameViewport.IsSIEMode() && !bTextEditKey) GameViewport.GetKeyInputBySIEMode(static_cast<int32>(Key), false);
			else GameViewport.InputKey(nullptr, static_cast<int32>(Key), false);
		}
	}
	constexpr EMouseButton MouseButtons[] = { EMouseButton::Left, EMouseButton::Right };
	for (EMouseButton Button : MouseButtons)
	{
		const int32 Key = Button == EMouseButton::Left ? static_cast<int32>(EKeyCode::LButton) : static_cast<int32>(EKeyCode::RButton);
		if (FInputSystem::IsMousePressed(Button))
		{
			if (GameViewport.IsSIEMode())
			{
				if (!GameViewport.ProcessUIKey(Key, true)) GameViewport.GetKeyInputBySIEMode(Key, true);
			}
			else GameViewport.InputKey(nullptr, Key, true);
		}
		if (FInputSystem::IsMouseReleased(Button))
		{
			if (GameViewport.IsSIEMode())
			{
				if (!GameViewport.ProcessUIKey(Key, false)) GameViewport.GetKeyInputBySIEMode(Key, false);
			}
			else GameViewport.InputKey(nullptr, Key, false);
		}
	}
	const float MouseX = static_cast<float>(FInputSystem::GetMouseDeltaX());
	const float MouseY = static_cast<float>(FInputSystem::GetMouseDeltaY());
	const float Wheel = static_cast<float>(FInputSystem::GetWheelDelta());
	if (GameViewport.IsSIEMode())
	{
		GameViewport.GetAxisInputBySIEMode(EGameInputAxis::MouseX, MouseX);
		GameViewport.GetAxisInputBySIEMode(EGameInputAxis::MouseY, MouseY);
		GameViewport.GetAxisInputBySIEMode(EGameInputAxis::MouseWheel, Wheel);
		GameViewport.TickSIEInput(DeltaTime, 10.0f * SettingsPanel->GetSettings().CameraSpeed,
			0.2f * SettingsPanel->GetSettings().MouseSensitivity);
	}
	else
	{
		GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseX, MouseX, DeltaTime);
		GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseY, MouseY, DeltaTime);
	}
}

// 화면을 표시하고 UI 변경 후 View 설정을 보관한다.
void UEditorEngine::EndFrame()
{
	PresentFrame();
	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldContextlist.Num(); ++i)
	{
		if (WorldContextlist[i].WorldType == EWorldType::Editor)
		{
			return WorldContextlist[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldContextlist)
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.PIEInstance == WorldPIEInstance)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}

void UEditorEngine::UpdateGizmoAndPicking()
{
	const int32 ViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	if (ViewIndex < 0 || ViewIndex >= 4 || !ViewportsPanel || !ViewportsPanel->IsHovered())
		return;
	if (IsPlayingView(ViewIndex))
		return;

	FEditorViewportClient* ViewClient = ViewportsPanel->GetViewportClient(ViewIndex);
	if (!ViewClient || !ViewClient->IsActive())
		return;

	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	ViewClient->UpdateGizmoAndPicking(Gizmo.get(), LocalMousePosition, [this](AActor* SelectedActor) {
		OutlinerPanel->SelectActor(SelectedActor);
	});
}

void UEditorEngine::BuildSceneRenderOptions(FGameViewportClient* ViewClient, FSceneRenderOptions& Options)
{
	if (!SettingsPanel)
	{
		return;
	}

	Options.bEnableFXAA = SettingsPanel->GetSettings().bEnableFXAA;
	Options.bCacheShadows = SettingsPanel->GetSettings().bCacheShadows;
	Options.PcfMode = SettingsPanel->GetSettings().PcfMode;
	Options.RenderingPath = SettingsPanel->GetSettings().RenderingPath;
	Options.LightingModel = SettingsPanel->GetSettings().LightingModel;
	Options.LightCullingMode = SettingsPanel->GetSettings().LightCullingMode;
	Options.DirectionalShadowMapSize = SettingsPanel->GetSettings().GetDirectionalShadowMapSize();
	Options.PointShadowMapSize = SettingsPanel->GetSettings().GetPointShadowMapSize();
	Options.SpotShadowMapSize = SettingsPanel->GetSettings().GetSpotShadowMapSize();
}

void UEditorEngine::BuildSceneRenderOptions(FEditorViewportClient* ViewClient, bool IsPIE, FSceneRenderOptions& Options)
{
	if (!SettingsPanel)
	{
		return;
	}

	Options.bEnableFXAA = SettingsPanel->GetSettings().bEnableFXAA;
	Options.bCacheShadows = SettingsPanel->GetSettings().bCacheShadows;
	Options.PcfMode = SettingsPanel->GetSettings().PcfMode;
	Options.RenderingPath = SettingsPanel->GetSettings().RenderingPath;
	Options.LightingModel = SettingsPanel->GetSettings().LightingModel;
	Options.LightCullingMode = SettingsPanel->GetSettings().LightCullingMode;
	Options.DirectionalShadowMapSize = SettingsPanel->GetSettings().GetDirectionalShadowMapSize();
	Options.PointShadowMapSize = SettingsPanel->GetSettings().GetPointShadowMapSize();
	Options.SpotShadowMapSize = SettingsPanel->GetSettings().GetSpotShadowMapSize();

	Options.bDrawPrimitives = IsPIE || SettingsPanel->GetSettings().bDrawPrimitives;
	Options.bEnableFog = ViewClient->GetViewportMode() == EViewportMode::Solid;
	Options.bShowSceneDepth = !IsPIE && ViewClient->IsSceneDepth();
	Options.bShowCascade = !IsPIE && ViewClient->IsCascade();
	Options.bShowWorldNormal = !IsPIE && ViewClient->IsWorldNormal();
	Options.bShowLightCulling = !IsPIE && ViewClient->IsLightCulling();
	Options.SceneDepthRange = ViewClient->GetMaxRange();
}

// 게임 뷰포트 화면을 렌더링한다
void UEditorEngine::RenderGameFrame(FGameViewportClient* GameClient, const FSceneView& SceneView, const FSceneRenderOptions& RenderOptions, FSceneRenderer& SceneRenderer, bool bIsSIEMode)
{
	if (!GameClient) return;

	FSceneViewTargets& Targets = GameClient->GetViewTargets();
	if (!Targets.SceneColor || !Targets.Depth) return;
	const bool bSceneRendered = bIsSIEMode
		? RenderScene(Targets, SceneView, SceneRenderer, GameClient->GetWorld(), RenderOptions, nullptr, false, true)
		: ViewPipeline.Render(SceneRenderer, Renderer, Targets, GameClient->GetWorld(), SceneView, RenderOptions);
	if (bSceneRendered) GameClient->RenderUI();
}

bool UEditorEngine::RenderScene(FSceneViewTargets& Targets, const FSceneView& SceneView,
	FSceneRenderer& SceneRenderer, UWorld* TargetWorld, const FSceneRenderOptions& Options,
	FEditorViewportClient* ViewClient, bool bIsPIE, bool bIsSIE)
{
	FEditorViewPasses EditorPasses(Options, SceneView, Targets.SceneColor.get(), Targets.Depth.get(), SceneDepthRenderer.get(), CascadeFloorRenderer.get());
	const FEditorOverlayResources OverlayResources{ GridRenderer.get(), GizmoRenderer.get(), LineBatcher.get(),
		Outline.get(), OutlineRenderer.get(), Gizmo.get(), TextRenderer.get(), SystemFont,
		EditorWorld, GetActiveWorld(), SettingsPanel ? &SettingsPanel->GetSettings() : nullptr,
		MapTileRenderer.get(), DetailsPanel ? &DetailsPanel->GetMapTileEditor() : nullptr,
		!PlayWorld || IsSimulatingInEditor() };
	if (ViewClient)
		EditorPasses.ConfigureViewportOverlays(OverlayResources, ViewClient, bIsPIE,
			ViewportsPanel && SceneView.ViewIndex == ViewportsPanel->GetActiveViewIndex());
	else if (bIsSIE)
		EditorPasses.ConfigureSIEOverlays(OverlayResources);
	FRenderGraphBuilder Graph;
	FSceneViewPipeline::FPreparedView Prepared;
	return ViewPipeline.Prepare(Graph, Prepared, SceneRenderer, Renderer, Targets, TargetWorld, SceneView, Options) &&
		EditorPasses.AddPasses(Graph, SceneRenderer, Renderer, Prepared.FinalColor, Prepared.Depth) &&
		ViewPipeline.Execute(Graph, Prepared, SceneRenderer, Targets);
}

// 뷰포트 하나의 씬과 에디터 요소를 렌더링한다
void UEditorEngine::RenderFrame(FEditorViewportClient* ViewClient,
	const FSceneView& SceneView,
	const FSceneRenderOptions& Options,
	FSceneRenderer& SceneRenderer,
	UWorld* TargetWorld,
	const bool bIsPIE)
{
	if (!ViewClient || !SettingsPanel) return;

	FSceneViewTargets& Targets = ViewClient->GetViewTargets();
	if (!Targets.SceneColor || !Targets.Depth) return;
	const bool bRendered = RenderScene(Targets, SceneView, SceneRenderer, TargetWorld, Options,
		ViewClient, bIsPIE, false);
	if (!bRendered) return;
}

// View Texture가 포함된 UI를 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
void UEditorEngine::PresentFrame()
{
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetBackbuffer(), nullptr,
		MainWindow ? MainWindow->GetWidth() : 0,
		MainWindow ? MainWindow->GetHeight() : 0);

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorUI->OnRender();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass();
}

void UEditorEngine::DrawPIEWindows()
{
}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	EndPlayMap();
	if (WidgetEditorPanel) WidgetEditorPanel->ReleaseAsset();
	if (SettingsPanel) SettingsPanel->SaveSettings();
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();
	ImGuiRenderer->Shutdown();
}

void UEditorEngine::DuplicateSelectedActor()
{
	AActor* Source = OutlinerPanel ? OutlinerPanel->GetSelectedActor() : nullptr;
	if (!Source || Source->GetWorld() != GetActiveWorld() ||
		(IsPlaying() && !bIsPaused && !IsSimulatingInEditor())) return;

	ULevel* Level = Source->GetLevel();
	if (!Level) return;
	AActor* Copy = Cast<AActor>(StaticDuplicateObject(Source, Level));
	if (!Copy) return;
	Level->AddActor(Copy);
	Copy->RegisterAllComponents();
	OutlinerPanel->SelectActor(Copy);
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
		return;
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
		return;
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;
	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Component->GetOwner()->DestroyComponent(Component);
}

void UEditorEngine::StartPlayInEditorSession()
{
	// 별도 창 모드면 패널 View는 모두 에디터 월드로 두고 창 하나에만 PIE 월드를 그린다.
	// 창을 만들지 못하면 선택한 View에서 실행한다.
	const bool bNewWindow = (PlayMode == EPlayModeType::InEditorFloating);

	PIEStartEditorViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : 0;
	if (PIEStartEditorViewIndex >= 0 && PIEStartEditorViewIndex < AllViewportClients.Num())
	{
		if (FEditorViewportClient* View = AllViewportClients[PIEStartEditorViewIndex])
		{
			SavedEditorCameraLocation = View->GetViewLocation();
			SavedEditorCameraRotation = View->GetViewRotation();
		}
	}
	if (bNewWindow && CreatePIEWindow())
		PIEViewIndex = -2;
	else
		// 지금 편집 중인 View 하나만 PIE 월드를 보여준다.
		PIEViewIndex = PIEStartEditorViewIndex;

	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PIEContext.PIEInstance = 0;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	GameInstance->SetWorldContext(&PIEContext);
	GameInstance->SetEndSessionHandler([this]() { RequestEndPlayMap(); });
	const bool bInitialized = GameInstance->InitializeForPlayInEditor(0);
	if (!bInitialized ||
		!GameInstance->StartPlayInEditorGameInstance())
	{
		UWorld* FailedWorld = PIEContext.World();
		DestroyWorldContext(FailedWorld);
		PIEViewIndex = InvalidViewIndex;
		PIEStartEditorViewIndex = InvalidViewIndex;
		DestroyPIEWindow();
		return;
	}
	PlayWorld = GameInstance->GetWorld();
	if (!bNewWindow && PIEContext.GameViewport && ViewportsPanel)
	{
		ViewportsPanel->SetGameViewportClient(PIEStartEditorViewIndex, PIEContext.GameViewport.get());
	}

	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	if (!NewPIEWorld) return nullptr;
	const int32 StartView = PIEStartEditorViewIndex;
	if (StartView >= 0 && StartView < AllViewportClients.Num() && AllViewportClients[StartView] && NewPIEWorld->GetMainCamera())
	{
		FEditorViewportClient* Source = AllViewportClients[StartView];
		UCameraComponent* Camera = NewPIEWorld->GetMainCamera()->GetCameraComponent();
		Camera->SetWorldLocationAndRotation(Source->GetViewLocation(), Source->GetViewRotation(), false);
		Camera->SetFieldOfView(Source->GetViewFOV());
		Camera->SetOrthoWidth(Source->GetOrthoWidth());
		Camera->SetIsOrthogonal(!Source->IsPerspective());
		Camera->SetExternalInputManaged(true);
	}
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
}

void UEditorEngine::OnActiveWorldChanged()
{
	ResetSceneSelection();
	UWorld* UIWorld = PlayWorld ? PlayWorld : EditorWorld;
	for (FEditorViewportClient* Client : AllViewportClients)
		if (Client) Client->SetWorld(UIWorld);
	if (OutlinerPanel) OutlinerPanel->SetWorld(UIWorld);
	if (DetailsPanel) DetailsPanel->SetWorld(UIWorld);
	if (EditorControlsPanel) EditorControlsPanel->SetWorld(UIWorld);
	if (SettingsPanel) SettingsPanel->SetWorld(UIWorld);
	for (FEditorViewportClient* Client : AllViewportClients)
		if (Client) Client->SetWorld(UIWorld);
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
		return;
	bIsPaused = false;
	bStepRequested = false;

	ResetSceneSelection();

	// PIE 종료 시 에디터 카메라 복원
	if (ViewportsPanel && ViewportsPanel->IsRestoreCameraOnPIEExit())
	{
		const int32 RestoreIndex = (PIEStartEditorViewIndex >= 0) ? PIEStartEditorViewIndex : (ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : 0);
		if (RestoreIndex >= 0 && RestoreIndex < AllViewportClients.Num())
		{
			if (FEditorViewportClient* View = AllViewportClients[RestoreIndex])
			{
				View->SetViewLocation(SavedEditorCameraLocation);
				View->SetViewRotation(SavedEditorCameraRotation);
			}
		}
	}

	if (ViewportsPanel)
		ViewportsPanel->ClearGameViewportClient();
	DestroyWorldContext(PlayWorld);
	PlayWorld = nullptr;
	PIEViewIndex = InvalidViewIndex;
	PIEStartEditorViewIndex = InvalidViewIndex;
	DestroyPIEWindow();
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
		return;

	PIETargetWidth = 0;
	PIETargetHeight = 0;
	bPIEWindowRendered = false;

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
	const uint32 Width = PIEWindow->GetWidth();
	const uint32 Height = PIEWindow->GetHeight();
	// 최소화 중에는 그릴 대상이 없다.
	if (Width == 0 || Height == 0 || ::IsIconic(PIEWindow->GetHandle()))
	{
		return false;
	}

	if (PIEWindow->CheckResized())
		PIESwapchain->Resize(Width, Height);

	if (Width != PIETargetWidth || Height != PIETargetHeight)
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (!Context || !Context->GameViewport) return false;
		Context->GameViewport->Resize(Width, Height);
		if (!Context->GameViewport->GetViewTargets().IsValidFor(Width, Height))
		{
			return false;
		}

		PIETargetWidth = Width;
		PIETargetHeight = Height;
	}

	return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	if (!PIESwapchain)
		return;

	FTexture2D* Backbuffer = PIESwapchain->GetBackbuffer();
	FWorldContext* Context = GetPIEWorldContext();
	FTexture2D* PIEColorTarget = Context && Context->GameViewport
		? Context->GameViewport->GetColorTarget() : nullptr;
	if (!Backbuffer || !PIEColorTarget ||
		Backbuffer->GetWidth() != PIETargetWidth ||
		Backbuffer->GetHeight() != PIETargetHeight)
		return;

	RenderCommand::GetContext()->CopyResource(Backbuffer->GetRawPtr(), PIEColorTarget->GetRawPtr());
	bPIEWindowRendered = true;
}

// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::ResetSceneSelection()
{
	Gizmo->SetTarget(nullptr, false);
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	DetailsPanel->SelectComponent(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::NewScene(EditorWorld))
		return;
	if (SettingsPanel) SettingsPanel->MarkSceneSaved();

	ResetSceneSelection();
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld))
		return;
	if (SettingsPanel) SettingsPanel->MarkSceneSaved();

	ResetSceneSelection();
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	if (FEditorFileUtils::SaveScene(EditorWorld) && SettingsPanel) SettingsPanel->MarkSceneSaved();
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	if (FEditorFileUtils::SaveSceneAs(EditorWorld) && SettingsPanel) SettingsPanel->MarkSceneSaved();
}
