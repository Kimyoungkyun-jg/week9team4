#include "EnginePCH.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/ViewportCameraMovement.h"
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

void FGameViewportClient::RenderUI()
{
	if (FocusedTextInput) FocusedTextInput->AppendText(FInputSystem::ConsumeTextInput());
	if ((!WidgetRoot && WidgetOverlays.IsEmpty()) || !ViewTargets.SceneColor) return;
	if (!WidgetRenderer)
	{
		WidgetRenderer = MakeUnique<FWidgetRenderer>();
		if (!WidgetRenderer->Init()) { WidgetRenderer.reset(); return; }
	}
	WidgetRenderer->Render(WidgetRoot.get(), ViewTargets.SceneColor.get());
	for (const FWidgetOverlay& Overlay : WidgetOverlays)
		WidgetRenderer->Render(Overlay.Root.get(), ViewTargets.SceneColor.get());
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
		if (UCameraComponent* Camera = Pawn->GetCameraComponent()) return Camera;
		for (UActorComponent* Component : Pawn->GetComponents())
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

	if (!CameraComponent && World)
	{
		CameraComponent = FindPlayerCamera();
		if (!CameraComponent && World->GetMainCamera())
			CameraComponent = World->GetMainCamera()->GetCameraComponent();
	}

	if (CameraComponent)
	{
		CameraComponent->SetAspectRatio(Aspect);

		OutView.ViewLocation = CameraComponent->GetWorldLocation();
		OutView.ViewRotation = CameraComponent->GetWorldRotation();
		OutView.ViewForward = OutView.ViewRotation.Quaternion().GetForwardVector();
		OutView.ViewMatrix = CameraComponent->GetViewMatrix();
		OutView.ProjectionMatrix = CameraComponent->GetProjectionMatrix();
		OutView.ViewProjectionMatrix = CameraComponent->GetViewProjectionMatrix();
		OutView.FieldOfView = CameraComponent->GetFieldOfView();
		OutView.AspectRatio = Aspect;
		OutView.NearClip = CameraComponent->GetNearZ();
		OutView.FarClip = CameraComponent->GetFarZ();
		OutView.OrthoWidth = CameraComponent->GetOrthoWidth();
		OutView.bIsPerspective = !CameraComponent->GetIsOrthogonal();
		OutView.bIsWireframe = false;
	}
	else
	{
		OutView.ViewLocation = FVector(-5.0f, -10.0f, 10.0f);
		OutView.ViewRotation = FRotator(30.0f, 45.0f, 0.0f);
		OutView.ViewForward = OutView.ViewRotation.Quaternion().GetForwardVector();
		OutView.FieldOfView = 60.0f;
		OutView.AspectRatio = Aspect;
		OutView.NearClip = 0.1f;
		OutView.FarClip = 1000.0f;
		OutView.bIsPerspective = true;
		OutView.bIsWireframe = false;
	}

	OutView.Frustum = ExtractFrustumPlanes(OutView.ViewProjectionMatrix);
	OutView.FrustumCorners = GenerateFrustumCorners(OutView.ViewProjectionMatrix.Inverse());

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
	if (!World || !World->GetMainCamera())
		return false;

	if (!CameraComponent) CameraComponent = FindPlayerCamera();
	if (!CameraComponent) return false;

	UCameraComponent* MainCamera = World->GetMainCamera()->GetCameraComponent();
	if (!MainCamera || MainCamera == CameraComponent) return false;
	if (!SetCameraWorldPose(*MainCamera, CameraComponent->GetWorldLocation(),
		CameraComponent->GetWorldRotation())) return false;

	MainCamera->SetFieldOfView(CameraComponent->GetFieldOfView());
	MainCamera->SetNearZ(CameraComponent->GetNearZ());
	MainCamera->SetFarZ(CameraComponent->GetFarZ());
	MainCamera->SetIsOrthogonal(CameraComponent->GetIsOrthogonal());
	MainCamera->SetOrthoWidth(CameraComponent->GetOrthoWidth());
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
	if (World && World->GetMainCamera())
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
