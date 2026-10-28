#include "EnginePCH.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/MinimalViewInfo.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/ViewportCameraMovement.h"
#include "Sequencer/LevelSequenceActor.h"
#include <cmath>
#include "Math/Frustum.h"
#include "Render/RenderCommand.h"
#include "UI/Widget.h"
#include "UI/WidgetAsset.h"
#include "UI/WidgetRenderer.h"
#include "Serialization/JsonArchive.h"
#include "Input/InputSystem.h"

namespace
{
	void StartAutoTimers(UWidget* Widget)
	{
		if (UTimerWidget* Timer = Cast<UTimerWidget>(Widget))
			if (Timer->GetAutoStart()) Timer->Start();
		if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
			for (UWidget* Child : Canvas->GetChildren()) StartAutoTimers(Child);
	}
}

FGameViewportClient::FGameViewportClient()
{
}

FGameViewportClient::~FGameViewportClient() = default;

bool FGameViewportClient::LoadWidgetRoot(const FString& AssetPath)
{
	TUniquePtr<UCanvasWidget> LoadedRoot = FWidgetAsset::Load(AssetPath);
	if (!LoadedRoot) return false;
	SetWidgetRoot(std::move(LoadedRoot));
	return true;
}

void FGameViewportClient::SetWidgetRoot(TUniquePtr<UCanvasWidget> InRoot)
{
	if (PressedButton) { PressedButton->SetPressed(false); bUIBlockingClick = true; }
	PressedButton = nullptr;
	ClearHoveredButton();
	SetFocusedTextInput(nullptr);
	WidgetRoot = std::move(InRoot);
	if (WidgetRoot) StartAutoTimers(WidgetRoot.get());
}

void FGameViewportClient::ClearWidgetRoot()
{
	if (PressedButton) { PressedButton->SetPressed(false); bUIBlockingClick = true; }
	PressedButton = nullptr;
	ClearHoveredButton();
	SetFocusedTextInput(nullptr);
	WidgetRoot.reset();
}

uint64 FGameViewportClient::AddWidgetOverlay(const FString& AssetPath, bool bModal)
{
	TUniquePtr<UCanvasWidget> Overlay = FWidgetAsset::Load(AssetPath);
	if (!Overlay) return 0;
	ClearHoveredButton();
	StartAutoTimers(Overlay.get());
	const uint64 Handle = NextWidgetOverlayHandle++;
	WidgetOverlays.Add({Handle, std::move(Overlay), bModal});
	return Handle;
}

bool FGameViewportClient::RemoveWidgetOverlay(uint64 Handle)
{
	for (uint32 Index = 0; Index < WidgetOverlays.Num(); ++Index)
	{
		if (WidgetOverlays[Index].Handle != Handle) continue;
		if (PressedButton) { PressedButton->SetPressed(false); PressedButton = nullptr; bUIBlockingClick = true; }
		ClearHoveredButton();
		SetFocusedTextInput(nullptr);
		WidgetOverlays.RemoveAt(Index);
		return true;
	}
	return false;
}

void FGameViewportClient::ClearWidgetOverlays()
{
	if (PressedButton) { PressedButton->SetPressed(false); PressedButton = nullptr; }
	ClearHoveredButton();
	SetFocusedTextInput(nullptr);
	WidgetOverlays.Empty();
	bUIBlockingClick = false;
}

void FGameViewportClient::RenderUI(const FSceneView& View)
{
	if (FocusedTextInput) FocusedTextInput->AppendText(FInputSystem::ConsumeTextInput());

	const bool bHasFade = View.bCameraFade && View.FadeAmount > 0.0f;
	const bool bHasLetterbox = View.LetterboxRatio > 0.0f;
	if ((!WidgetRoot && WidgetOverlays.IsEmpty() && !bHasFade && !bHasLetterbox) || !ViewTargets.SceneColor) return;

	if (!WidgetRenderer)
	{
		WidgetRenderer = MakeUnique<FWidgetRenderer>();
		if (!WidgetRenderer->Init()) { WidgetRenderer.reset(); return; }
	}

	// 1. [bFadeHUD == false] 3D 월드만 페이드: UI 그리기 전에 페이드 적용 (UI는 정상 표시)
	if (bHasFade && !View.bFadeHUD)
	{
		WidgetRenderer->DrawFadeOverlay(
			View.FadeColor,
			View.FadeAmount,
			ViewTargets.SceneColor.get()
		);
	}

	// 2. UI 위젯 렌더링
	if (WidgetRoot)
		WidgetRenderer->Render(WidgetRoot.get(), ViewTargets.SceneColor.get());
	for (const FWidgetOverlay& Overlay : WidgetOverlays)
		WidgetRenderer->Render(Overlay.Root.get(), ViewTargets.SceneColor.get());
	if (View.LetterboxRatio > 0.0f)
	{
		WidgetRenderer->DrawLetterboxOverlay(View.LetterboxRatio, ViewTargets.SceneColor.get());
	}

	// 3. [bFadeHUD == true] UI까지 전체 페이드: UI를 모두 덮어씌움
	if (bHasFade && View.bFadeHUD)
	{
		WidgetRenderer->DrawFadeOverlay(
			View.FadeColor,
			View.FadeAmount,
			ViewTargets.SceneColor.get()
		);
	}

	OnUIRendered.Broadcast();
}

void FGameViewportClient::Init(FWorldContext& InWorldContext, UEngine* InEngine)
{
	SetWorld(InWorldContext.World());
	GameInstance = InWorldContext.OwningGameInstance;
	Engine = InEngine;
}

UWorld* FGameViewportClient::GetWorld() const
{
	return World;
}

void FGameViewportClient::MouseMove(FViewport* Viewport, int32 X, int32 Y)
{
	UIMouseX = X;
	UIMouseY = Y;
	HandleUIMouseMove(X, Y);
	UButtonWidget* Button = HitTestButton();
	if (Button != HoveredButton)
	{
		ClearHoveredButton();
		HoveredButton = Button;
		if (HoveredButton) HoveredButton->SetHovered(true);
	}
}

namespace
{
	UButtonWidget* HitButton(UWidget* Widget, float X, float Y)
	{
		if (!Widget || !Widget->IsVisible()) return nullptr;
		if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
		{
			TArray<UWidget*> Ordered = Canvas->GetChildrenInPaintOrder();
			for (int32 Index = static_cast<int32>(Ordered.Num()) - 1; Index >= 0; --Index)
				if (UButtonWidget* Hit = HitButton(Ordered[Index], X, Y)) return Hit;
		}
		const FRect& Rect = Widget->GetLayoutRect();
		return X >= Rect.X && Y >= Rect.Y && X < Rect.X + Rect.Width && Y < Rect.Y + Rect.Height
			? Cast<UButtonWidget>(Widget) : nullptr;
	}

	UWidget* FindWidgetInTree(UWidget* Widget, const FString& Name)
	{
		if (!Widget) return nullptr;
		if (Widget->GetWidgetName() == Name) return Widget;
		if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
			for (UWidget* Child : Canvas->GetChildren())
				if (UWidget* Found = FindWidgetInTree(Child, Name)) return Found;
		return nullptr;
	}
}

UWidget* FGameViewportClient::FindWidget(const FString& WidgetName) const
{
	for (int32 Index = static_cast<int32>(WidgetOverlays.Num()) - 1; Index >= 0; --Index)
		if (UWidget* Found = FindWidgetInTree(WidgetOverlays[Index].Root.get(), WidgetName)) return Found;
	return FindWidgetInTree(WidgetRoot.get(), WidgetName);
}

bool FGameViewportClient::BindButtonAction(const FString& WidgetName, std::function<void()> Callback)
{
	if (UButtonWidget* Button = Cast<UButtonWidget>(FindWidget(WidgetName)))
	{
		Button->SetOnClicked(std::move(Callback));
		return true;
	}
	return false;
}

void FGameViewportClient::RequestSceneChange(const FString& ScenePath, const FString& FallbackPath)
{
	if (ScenePath.empty() || !PendingScenePath.empty()) return;
	PendingScenePath = ScenePath;
	FallbackScenePath = FallbackPath;
}

void FGameViewportClient::ProcessPendingSceneChange()
{
	if (!World || PendingScenePath.empty()) return;
	FString ScenePath = std::move(PendingScenePath);
	FString FallbackPath = std::move(FallbackScenePath);
	PendingScenePath.clear();
	FallbackScenePath.clear();
	if (!FJsonArchive::LoadWorld(World, ScenePath))
	{
		HTR_LOG(Error, "Failed to load scene: {}", ScenePath);
		if (FallbackPath.empty() || !FJsonArchive::LoadWorld(World, FallbackPath)) return;
	}
	UWorld* LoadedWorld = World;
	SetWorld(nullptr);
	SetWorld(LoadedWorld);
	LoadedWorld->BeginPlay();
}

void FGameViewportClient::ClearHoveredButton()
{
	if (HoveredButton) HoveredButton->SetHovered(false);
	HoveredButton = nullptr;
}

void FGameViewportClient::SetFocusedTextInput(UTextInputWidget* Input)
{
	if (FocusedTextInput == Input) return;
	if (FocusedTextInput) FocusedTextInput->SetFocused(false);
	FocusedTextInput = Input;
	if (FocusedTextInput) FocusedTextInput->SetFocused(true);
}

bool FGameViewportClient::TryGetViewInfo(FMinimalViewInfo& OutView) const
{
	if (!World) return false;
	const auto IsUsableTarget = [](AActor* Target)
	{
		return Target && GUObjectArray.Find(Target) != INDEX_NONE &&
			IsValid(Target) && !Target->IsActorBeingDestroyed();
	};
	if (!bSIEMode)
	{
		APlayerController* Controller = World->GetPlayerController();
		if (IsUsableTarget(Controller))
			if (APlayerCameraManager* Manager = Controller->GetPlayerCameraManager())
				if (const FMinimalViewInfo* CachedPOV = Manager->GetCameraCachePOV())
				{
					OutView = *CachedPOV;
					return true;
				}
	}
	else if (CameraComponent && GUObjectArray.Find(CameraComponent) != INDEX_NONE &&
		IsValid(CameraComponent) && !CameraComponent->IsBeingDestroyed())
	{
		CameraComponent->GetCameraView(OutView);
		return true;
	}

	AActor* Fallback = bSIEMode ? static_cast<AActor*>(World->GetMainCamera()) : World->GetPlayerPawn();
	if (!IsUsableTarget(Fallback)) Fallback = World->GetMainCamera();
	if (!IsUsableTarget(Fallback)) return false;
	Fallback->CalcCamera(0.0f, OutView);
	return IsUsableTarget(Fallback);
}

UButtonWidget* FGameViewportClient::HitTestButton(bool* bBlocked) const
{
	if (bBlocked) *bBlocked = false;
	if (!Width || !Height || UIMouseX < 0 || UIMouseY < 0 ||
		static_cast<uint32>(UIMouseX) >= Width || static_cast<uint32>(UIMouseY) >= Height) return nullptr;
	const FRect Rect{0, 0, static_cast<float>(Width), static_cast<float>(Height)};
	for (int32 Index = static_cast<int32>(WidgetOverlays.Num()) - 1; Index >= 0; --Index)
	{
		const FWidgetOverlay& Overlay = WidgetOverlays[Index];
		UCanvasWidget* Root = Overlay.Root.get();
		if (!Root->IsVisible()) continue;
		Root->Layout(Rect);
		if (UButtonWidget* Button = HitButton(Root, static_cast<float>(UIMouseX), static_cast<float>(UIMouseY)))
			return Button;
		if (Overlay.bModal) { if (bBlocked) *bBlocked = true; return nullptr; }
	}
	if (!WidgetRoot) return nullptr;
	WidgetRoot->Layout(Rect);
	return HitButton(WidgetRoot.get(), static_cast<float>(UIMouseX), static_cast<float>(UIMouseY));
}

bool FGameViewportClient::ProcessUIKey(int32 Key, bool bDown)
{
	if (Key != static_cast<int32>(EKeyCode::LButton)) return false;
	bool bBlocked = false;
	if (!bDown && PressedButton)
	{
		UButtonWidget* Button = PressedButton;
		PressedButton = nullptr;
		bUIBlockingClick = false;
		Button->SetPressed(false);
		if (HitTestButton() == Button) Button->Click();
		return true;
	}
	if (!bDown && bUIBlockingClick) { bUIBlockingClick = false; return true; }
	if (!bDown) return false;
	bUIBlockingClick = false;
	PressedButton = HitTestButton(&bBlocked);
	SetFocusedTextInput(Cast<UTextInputWidget>(PressedButton));
	if (!PressedButton)
	{
		bUIBlockingClick = bBlocked;
		return bBlocked;
	}
	PressedButton->SetPressed(true);
	return true;
}

bool FGameViewportClient::InputKey(FViewport* Viewport, int32 Key, bool bDown)
{
	if (FocusedTextInput) FocusedTextInput->AppendText(FInputSystem::ConsumeTextInput());
	if (ProcessUIKey(Key, bDown) || HandleUIKey(Key, bDown))
		return true;
	if (FocusedTextInput && bDown)
	{
		if (Key == static_cast<int32>(EKeyCode::Backspace)) { FocusedTextInput->Backspace(); return true; }
		if (Key == static_cast<int32>(EKeyCode::Enter)) { FocusedTextInput->Submit(); return true; }
	}
	if (bDown && Key == static_cast<int32>(EKeyCode::Escape))
	{
		bExitRequested = true;
		return true;
	}

	if (bDown && Key == static_cast<int32>(EKeyCode::F8))
	{
		bSIEModeRequested = true;
		return true;
	}

	if (bSIEMode && Key == static_cast<int32>(EKeyCode::LButton)) return true;
	if (World && World->GetGameMode() && !World->GetGameMode()->AllowsPlayerControllerInput()) return true;

	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
		{
			PC->InputKey(Key, bDown);
			return true;
		}
	}
	return false;
}

bool FGameViewportClient::InputAxis(FViewport* Viewport, int32 ControllerId, EGameInputAxis AxisKey, float Delta, float DeltaTime)
{
	if (HandleUIAxis(AxisKey, Delta))
		return true;
	if (World && World->GetGameMode() && !World->GetGameMode()->AllowsPlayerControllerInput()) return true;

	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
		{
			PC->InputAxis(AxisKey, Delta);
			return true;
		}
	}
	return false;
}

void FGameViewportClient::LostFocus()
{
	if (PressedButton) { PressedButton->SetPressed(false); PressedButton = nullptr; }
	ClearHoveredButton();
	SetFocusedTextInput(nullptr);
	bUIBlockingClick = false;
	std::fill(std::begin(bSIEKeyDown), std::end(bSIEKeyDown), false);
	SIEMouseDeltaX = SIEMouseDeltaY = SIEWheelDelta = 0.0f;
	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
			PC->ClearInputState();
	}
}

UCameraComponent* FGameViewportClient::FindPlayerCamera() const
{
	if (!World) return nullptr;
	if (APawn* Pawn = World->GetPlayerPawn())
	{
		if (GUObjectArray.Find(Pawn) == INDEX_NONE || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return nullptr;
		if (UCameraComponent* Camera = Pawn->GetCameraComponent();
			Camera && GUObjectArray.Find(Camera) != INDEX_NONE && IsValid(Camera) && !Camera->IsBeingDestroyed()) return Camera;
		for (UActorComponent* Component : Pawn->GetComponents())
			if (Component && GUObjectArray.Find(Component) != INDEX_NONE && IsValid(Component) && !Component->IsBeingDestroyed())
				if (UCameraComponent* Camera = Cast<UCameraComponent>(Component)) return Camera;
	}
	return nullptr;
}

void FGameViewportClient::SetWorld(UWorld* InWorld)
{
	if (World == InWorld) return;
	EndSIEMode();
	LostFocus();
	RenderData.ResetScene();
	CameraComponent = nullptr;
	bExitRequested = bSIEModeRequested = false;
	World = InWorld;
}

namespace
{
	bool SetCameraWorldPose(UCameraComponent& Camera, const FVector& Location, const FRotator& Rotation)
	{
		FTransform Relative = Camera.GetTransform();
		if (const USceneComponent* Parent = Camera.GetAttachParent())
		{
			const FMatrix ParentWorld = Parent->GetWorldMatrix();
			const float Determinant = ParentWorld.Determinant();
			if (!std::isfinite(Determinant) || Determinant == 0.0f)
				return false;
			Relative.Location = ParentWorld.Inverse().TransformPosition(Location);
			if (!std::isfinite(Relative.Location.X) || !std::isfinite(Relative.Location.Y) ||
				!std::isfinite(Relative.Location.Z)) return false;
			Relative.Rotation = (Parent->GetWorldRotation().Quaternion().Inverse() *
				Rotation.Quaternion()).ToFRotator();
		}
		else
		{
			Relative.Location = Location;
			Relative.Rotation = Rotation;
		}
		Camera.SetTransform(Relative);
		return true;
	}
}

FSceneView FGameViewportClient::CalcSceneView(const FRect& InViewRect)
{
	FSceneView OutView;
	OutView.ViewIndex = 0;
	OutView.bGameView = !bSIEMode;
	OutView.ViewRect = InViewRect;

	Resize(
		static_cast<uint32>(std::max(InViewRect.Width, 1.0f)),
		static_cast<uint32>(std::max(InViewRect.Height, 1.0f))
	);

	const float Aspect = static_cast<float>(Width) / static_cast<float>(Height);
	FMinimalViewInfo POV;
	const bool bHasPOV = TryGetViewInfo(POV);
	if (!bHasPOV)
	{
		POV.Location = FVector(-5.0f, -10.0f, 10.0f);
		POV.Rotation = FRotator(30.0f, 45.0f, 0.0f);
		POV.FarClipPlane = 1000.0f;
	}

	OutView.ViewLocation = POV.Location;
	OutView.ViewRotation = POV.Rotation;
	OutView.ViewForward = POV.Rotation.Quaternion().GetForwardVector();
	OutView.FieldOfView = std::isfinite(POV.FOV) ? std::clamp(POV.FOV, 1.0f, 179.0f) : 60.0f;
	OutView.AspectRatio = Aspect;
	OutView.NearClip = std::isfinite(POV.NearClipPlane) ? std::max(POV.NearClipPlane, 0.001f) : 0.1f;
	OutView.FarClip = std::isfinite(POV.FarClipPlane) ? std::max(POV.FarClipPlane, OutView.NearClip + 0.001f) : 10000.0f;
	OutView.OrthoWidth = std::isfinite(POV.OrthoWidth) ? std::max(POV.OrthoWidth, 0.001f) : 16.0f;
	OutView.bIsPerspective = !POV.bIsOrthogonal;
	OutView.bIsWireframe = false;
	if (World && World->GetPersistentLevel())
	{
		const auto ApplySequenceEffects = [&OutView](ALevelSequenceActor* Sequence)
		{
			if (!Sequence || !IsValid(Sequence) || Sequence->IsActorBeingDestroyed() ||
				!Sequence->HasCameraEffectsView()) return false;
			bool bHasEffectsTrack = false;
			for (const FSequenceTrack& Track : Sequence->GetSequenceData().Tracks)
				if (!Track.bMuted && Track.TrackType == ESequenceTrackType::CameraEffects && !Track.CameraEffectsKeys.IsEmpty())
				{ bHasEffectsTrack = true; break; }
			if (!bHasEffectsTrack) return false;
			const FSequenceCameraEffects Effects = FLevelSequenceAsset::EvaluateCameraEffects(Sequence->GetSequenceData(), Sequence->GetCurrentFrame());
			OutView.FadeOpacity = 0.0f;
			OutView.LetterboxSize = 0.0f;
			if (Effects.FadeOpacity > 0.0f)
			{
				OutView.bCameraFade = true;
				OutView.FadeAmount = std::max(OutView.FadeAmount, Effects.FadeOpacity);
				OutView.FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
			}
			if (Effects.LetterboxSize > 0.0f)
			{
				OutView.LetterboxRatio = std::max(OutView.LetterboxRatio, Effects.LetterboxSize);
			}
			return true;
		};
		bool bApplied = false;
		if (APlayerController* Controller = World->GetPlayerController())
			bApplied = ApplySequenceEffects(Cast<ALevelSequenceActor>(Controller->GetViewTarget()));
		if (!bApplied)
		{
			const TArray<AActor*>& Actors = World->GetPersistentLevel()->GetActors();
			for (int32 Index = Actors.Num() - 1; Index >= 0; --Index)
				if (ApplySequenceEffects(Cast<ALevelSequenceActor>(Actors[Index]))) break;
		}
	}
	OutView.ViewMatrix = FMatrix::MakeTranslation(POV.Location * -1.0f) *
		POV.Rotation.Quaternion().ToFMatrix().GetTransposed();
	OutView.ProjectionMatrix = !POV.bIsOrthogonal
		? Perspective(FMath::DegreesToRadians(OutView.FieldOfView), Aspect, OutView.NearClip, OutView.FarClip)
		: Ortho(OutView.OrthoWidth, Aspect, OutView.NearClip, OutView.FarClip);
	OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

	OutView.Frustum = ExtractFrustumPlanes(OutView.ViewProjectionMatrix);

	OutView.FrustumCorners = GenerateFrustumCorners(OutView.ViewProjectionMatrix.Inverse());

	// 플레이어 컨트롤러의 카메라 매니저로부터 카메라 페이드 정보 수집
	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
		{
			if (APlayerCameraManager* PCM = PC->GetPlayerCameraManager())
			{
				if (PCM->HasActiveFade())
				{
					OutView.bCameraFade = true;
					OutView.bFadeHUD = PCM->IsFadeHUD();
					OutView.FadeColor = PCM->GetFadeColor();
					OutView.FadeAmount = std::max(OutView.FadeAmount, PCM->GetFadeAmount());
				}
				if (PCM->GetLetterboxRatio() > 0.0f)
				{
					OutView.LetterboxRatio = std::max(OutView.LetterboxRatio, PCM->GetLetterboxRatio());
				}
			}
		}
	}

	return OutView;
}

void FGameViewportClient::SetCameraComponent(UCameraComponent* InCameraComponent)
{
	CameraComponent = InCameraComponent;
}

bool FGameViewportClient::BeginSIEMode()
{
	if (bSIEMode)
		return true;
	if (!World)
		return false;

	ACameraActor* MainCameraActor = World->GetMainCamera();

	if (!IsValid(MainCameraActor) || MainCameraActor->IsActorBeingDestroyed())
	{
		return false;
	}

	UCameraComponent* MainCamera =	MainCameraActor->GetCameraComponent();

	if (!IsValid(MainCamera) || MainCamera->IsBeingDestroyed())
	{
		return false;
	}

	FMinimalViewInfo CurrentView;

	// 아직 bSIEMode가 false이므로 Manager의 POV를 우선 조회한다.
	if (!TryGetViewInfo(CurrentView))
	{
		return false;
	}

	if (!SetCameraWorldPose(*MainCamera, CurrentView.Location, CurrentView.Rotation))
	{
		return false;
	}

	MainCamera->SetFieldOfView(CurrentView.FOV);
	MainCamera->SetNearZ(CurrentView.NearClipPlane);
	MainCamera->SetFarZ(CurrentView.FarClipPlane);
	MainCamera->SetIsOrthogonal(CurrentView.bIsOrthogonal);
	MainCamera->SetOrthoWidth(CurrentView.OrthoWidth);

	if (Width > 0 && Height > 0)
	{
		MainCamera->SetAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
	}

	bPreviousMainCameraExternalInputManaged = MainCamera->IsExternalInputManaged();
	MainCamera->SetExternalInputManaged(true);
	CameraComponent = MainCamera;
	bSIEMode = true;
	LostFocus();

	return true;
}

void FGameViewportClient::EndSIEMode()
{
	if (!bSIEMode)
		return;
	if (World && World->GetMainCamera() && GUObjectArray.Find(World->GetMainCamera()) != INDEX_NONE &&
		IsValid(World->GetMainCamera()) && !World->GetMainCamera()->IsActorBeingDestroyed())
		if (UCameraComponent* MainCamera = World->GetMainCamera()->GetCameraComponent())
			MainCamera->SetExternalInputManaged(bPreviousMainCameraExternalInputManaged);
	CameraComponent = FindPlayerCamera();
	bSIEMode = false;
	LostFocus();
}

void FGameViewportClient::GetKeyInputBySIEMode(int32 Key, bool bDown)
{
	if (bSIEMode && Key >= 0 && Key < 256)
		bSIEKeyDown[Key] = bDown;
}

void FGameViewportClient::GetAxisInputBySIEMode(EGameInputAxis Key, float Delta)
{
	if (!bSIEMode)
		return;
	switch (Key)
	{
	case EGameInputAxis::MouseX: SIEMouseDeltaX += Delta; break;
	case EGameInputAxis::MouseY: SIEMouseDeltaY += Delta; break;
	case EGameInputAxis::MouseWheel: SIEWheelDelta += Delta; break;
	}
}

void FGameViewportClient::TickSIEInput(float DeltaTime, float MoveSpeed, float MouseSensitivity)
{
	if (!bSIEMode || !CameraComponent)
		return;
	FViewportCameraInput Input;
	Input.bCaptured = bSIEKeyDown[static_cast<int32>(EKeyCode::RButton)];
	Input.bAllowWheel = true;
	Input.MouseX = SIEMouseDeltaX;
	Input.MouseY = SIEMouseDeltaY;
	Input.Wheel = SIEWheelDelta;
	Input.bForward = bSIEKeyDown[static_cast<int32>(EKeyCode::W)];
	Input.bBackward = bSIEKeyDown[static_cast<int32>(EKeyCode::S)];
	Input.bRight = bSIEKeyDown[static_cast<int32>(EKeyCode::D)];
	Input.bLeft = bSIEKeyDown[static_cast<int32>(EKeyCode::A)];
	Input.bUp = bSIEKeyDown[static_cast<int32>(EKeyCode::E)];
	Input.bDown = bSIEKeyDown[static_cast<int32>(EKeyCode::Q)];
	FViewportCameraState Camera{CameraComponent->GetWorldLocation(),
		CameraComponent->GetWorldRotation(), CameraComponent->GetOrthoWidth(),
		Width > 0 ? static_cast<float>(Width) : 800.0f, !CameraComponent->GetIsOrthogonal()};
	ApplyViewportCameraMovement(Camera, Input, DeltaTime, MoveSpeed, MouseSensitivity);
	if (SetCameraWorldPose(*CameraComponent, Camera.Location, Camera.Rotation))
		CameraComponent->SetOrthoWidth(Camera.OrthoWidth);
	SIEMouseDeltaX = SIEMouseDeltaY = SIEWheelDelta = 0.0f;
}

void FGameViewportClient::Resize(uint32 InWidth, uint32 InHeight)
{
	if (Width == InWidth && Height == InHeight && ViewTargets.IsValidFor(InWidth, InHeight))
	{
		return;
	}

	Width = InWidth;
	Height = InHeight;

	if (Width == 0 || Height == 0)
	{
		ViewTargets.Reset();
		return;
	}

	ViewTargets.Resize(Width, Height);
}

void FGameViewportClient::Reset()
{
	PendingScenePath.clear();
	FallbackScenePath.clear();
	ClearWidgetRoot();
	ClearWidgetOverlays();
	WidgetRenderer.reset();
	RenderData.ResetScene();
	EndSIEMode();
	LostFocus();
	bExitRequested = false;
	bSIEModeRequested = false;
	World = nullptr;
	GameInstance = nullptr;
	Engine = nullptr;
	CameraComponent = nullptr;

	ViewTargets.Reset();
	Width = 0;
	Height = 0;
}
