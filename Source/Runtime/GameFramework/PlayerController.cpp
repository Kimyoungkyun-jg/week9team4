#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/TestPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include <cstring>

APlayerController::APlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerCameraManagerClass = APlayerCameraManager::StaticClass();
}

void APlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsPendingKillPending() || IsValid(PlayerCameraManager)) { return; }

	UWorld* World = GetWorld();

	if (!World) { return; }

	// 기본 카메라가 없어도 사용할 수 있는 초기 시점.
	FallbackCameraView = FMinimalViewInfo{};
	FallbackCameraView.Location = GetActorLocation();
	FallbackCameraView.Rotation = GetActorRotation();

	// 기본 카메라가 있으면 그 시점을 초기값으로 사용.
	ACameraActor* MainCamera = World->GetMainCamera();

	if (IsValid(MainCamera) && !MainCamera->IsActorBeingDestroyed())
	{
		UCameraComponent* Camera = MainCamera->GetCameraComponent();

		if (IsValid(Camera) && !Camera->IsBeingDestroyed())
		{
			Camera->GetCameraView(0.0f, FallbackCameraView);
		}
	}

	UClass* ManagerClass = PlayerCameraManagerClass;

	if (!ManagerClass || !ManagerClass->IsChildOf(APlayerCameraManager::StaticClass()) || !ManagerClass->Constructor)
	{
		ManagerClass = APlayerCameraManager::StaticClass();
	}

	PlayerCameraManager = Cast<APlayerCameraManager>(World->SpawnActor(ManagerClass));

	if (IsValid(PlayerCameraManager) &&	!PlayerCameraManager->IsActorBeingDestroyed())
	{
		PlayerCameraManager->InitializeFor(this);

		APawn* Pawn = GetPawn();
		AActor* InitialTarget = this;

		if (IsValid(Pawn) && !Pawn->IsActorBeingDestroyed())
		{
			InitialTarget = Pawn;
		}

		PlayerCameraManager->SetViewTarget(InitialTarget);

		// 첫 화면 출력 전에 POV를 준비한다.
		PlayerCameraManager->UpdateCamera(0.0f);
	}
}

void APlayerController::EndPlay()
{
	APlayerCameraManager* Manager = PlayerCameraManager;
	PlayerCameraManager = nullptr;

	if (IsValid(Manager))
	{
		Manager->InitializeFor(nullptr);
		Manager->SetViewTarget(nullptr);
		Manager->Destroy();
	}

	Super::EndPlay();
}

void APlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessPlayerInput(DeltaTime);
}

void APlayerController::ProcessPlayerInput(float DeltaTime)
{
	if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	UWorld* World = GetWorld();
	if (World && World->GetGameMode() && !World->GetGameMode()->AllowsPlayerControllerInput())
	{
		// Key-up events may be blocked too; discard the previously held input.
		ClearInputState();
		return;
	}
	const float Forward = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::W)]) -
		static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::S)]);
	const float Right = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::D)]) -
		static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::A)]);
	PossessedPawn->AddControllerYawInput(MouseAxisX * LookSensitivity);
	if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	PossessedPawn->AddControllerPitchInput(MouseAxisY * LookSensitivity);
	if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	MouseAxisX = MouseAxisY = 0.0f;
	const FQuat Rotation = PossessedPawn->GetActorRotation().Quaternion();
	if (Forward != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetForwardVector(), Forward);
	if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	if (Right != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetRightVector(), Right);
}

void APlayerController::GetPlayerViewCamera(FVector& OutLocation, FRotator& OutRotation) const
{
	if (IsValid(PlayerCameraManager) && !PlayerCameraManager->IsActorBeingDestroyed() && PlayerCameraManager->HasValidCameraPOV())
	{
		const FMinimalViewInfo& View = PlayerCameraManager->GetCameraPOV();
		OutLocation = View.Location;
		OutRotation = View.Rotation;
		return;
	}

	OutLocation = FallbackCameraView.Location;
	OutRotation = FallbackCameraView.Rotation;

	if (!IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	OutLocation = PossessedPawn->GetActorLocation();
	OutRotation = PossessedPawn->GetActorRotation();
	if (UCameraComponent* Camera = PossessedPawn->GetCameraComponent())
	{
		OutLocation = Camera->GetWorldLocation();
		OutRotation = Camera->GetWorldRotation();
	}
}

void APlayerController::Possess(APawn* InPawn)
{
	if (IsPendingKillPending() || (InPawn && (!IsValid(InPawn) || InPawn->IsActorBeingDestroyed()))) return;
	if (PossessedPawn == InPawn) return;
	UnPossess();
	PossessedPawn = InPawn;
	if (IsValid(PlayerCameraManager))
	{
		PlayerCameraManager->SetViewTarget(PossessedPawn);
	}
	if (PossessedPawn) PossessedPawn->PossessedBy(this);
}

void APlayerController::UnPossess()
{
	APawn* OldPawn = PossessedPawn;

	const bool bViewingOldPawn =
	{ 
		OldPawn != nullptr && 
		IsValid(PlayerCameraManager) && 
		!PlayerCameraManager->IsActorBeingDestroyed() && 
		PlayerCameraManager->GetViewTarget() == OldPawn 
	};

	if (bViewingOldPawn && PlayerCameraManager->HasValidCameraPOV())
	{
		// 정상 해제·파괴 상황 모두 마지막 기본 시점을 보존. Modifier는 다시 적용된다.
		FallbackCameraView = PlayerCameraManager->GetBaseCameraPOV();
	}
	else if (IsValid(OldPawn) && !OldPawn->IsActorBeingDestroyed())
	{
		// Manager가 아직 계산하지 않았다면 살아 있는 Pawn에서 얻는다.
		OldPawn->CalcCamera(0.0f, FallbackCameraView);
	}

		

	if (IsValid(OldPawn))
	{
		OldPawn->UnPossessed();
	}

	PossessedPawn = nullptr;

	// 콜백 실행 중 대상이 바뀌었을 수도 있으므로 다시 확인.
	if (!IsPendingKillPending() && OldPawn && IsValid(PlayerCameraManager) && !PlayerCameraManager->IsActorBeingDestroyed() && PlayerCameraManager->GetViewTarget() == OldPawn)
	{
		PlayerCameraManager->SetViewTarget(this);
	}
}

void APlayerController::InputKey(int32 Key, bool bDown)
{
	if (IsPendingKillPending()) return;
	if (Key < 0 || Key >= 256) return;
	const bool bPressed = bDown && !bKeyDown[Key];
	bKeyDown[Key] = bDown;
	UWorld* World = GetWorld();
	const AGameMode* Mode = World ? World->GetGameMode() : nullptr;
	if (bPressed &&
		(Key == static_cast<int32>(EKeyCode::F6) || Key == static_cast<int32>(EKeyCode::F7)) &&
		IsValid(PlayerCameraManager) && !PlayerCameraManager->IsActorBeingDestroyed() &&
		(!World || !World->IsPaused()))
	{
		const bool bSineWave = Key == static_cast<int32>(EKeyCode::F6);
		FCameraShakeSettings Settings;
		Settings.Pattern = bSineWave
			? ECameraShakePattern::SineWave
			: ECameraShakePattern::PerlinNoise;
		Settings.Duration = 3.0f;
		Settings.BlendInTime = 0.1f;
		Settings.BlendOutTime = 0.25f;
		// Use small pitch/yaw offsets to compare the LMS examples without heavy shake.
		Settings.LocationAmplitude = FVector::ZeroVector;
		Settings.LocationFrequency = FVector::ZeroVector;
		Settings.RotationAmplitude = FRotator(0.35f, 0.2f, 0.0f);
		// Sine uses cycles/sec; Perlin uses lattice units/sec, so the values differ.
		Settings.RotationFrequency = bSineWave
			? FVector(2.2f, 1.1f, 0.0f)
			: FVector(5.0f, 3.5f, 0.0f);
		Settings.FOVAmplitude = 0.0f;
		PlayerCameraManager->StopAllCameraShakes(true);
		PlayerCameraManager->StartCameraShake(Settings);
	}

	if (bPressed && Key == static_cast<int32>(EKeyCode::Q) &&
		IsValid(PossessedPawn) && !PossessedPawn->IsActorBeingDestroyed() &&
		(!World || !World->IsPaused()) &&
		(!Mode || (Mode->AllowsPlayerControllerInput() && Mode->AllowsCameraModeToggle())))
	{
		PossessedPawn->ToggleCameraMode();
	}
	
	if (Key == static_cast<int32>(EKeyCode::Space) && IsValid(PossessedPawn) && !PossessedPawn->IsActorBeingDestroyed())
	{
		if (!bDown) PossessedPawn->StopJumping();
		else if (bPressed && (!GetWorld() || !GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput()))
			PossessedPawn->Jump();
	}
	if (bPressed && Key == static_cast<int32>(EKeyCode::LButton))
		if (IsValid(PossessedPawn) && !PossessedPawn->IsActorBeingDestroyed())
			if (ATestPawn* Pawn = Cast<ATestPawn>(PossessedPawn)) Pawn->FireProjectile();
}

void APlayerController::InputAxis(EGameInputAxis Axis, float Delta)
{
	if (IsPendingKillPending()) return;
	if (Axis == EGameInputAxis::MouseX) MouseAxisX += Delta;
	else if (Axis == EGameInputAxis::MouseY) MouseAxisY += Delta;
}

void APlayerController::ClearInputState()
{
	if (IsValid(PossessedPawn) && !PossessedPawn->IsActorBeingDestroyed())
	{
		PossessedPawn->StopJumping();
		PossessedPawn->ConsumeMovementInputVector();
	}
	std::memset(bKeyDown, 0, sizeof(bKeyDown));
	MouseAxisX = MouseAxisY = 0.0f;
}

void APlayerController::CalcCamera(float DeltaTime, FMinimalViewInfo& OutView)
{
	OutView = FallbackCameraView;
}
