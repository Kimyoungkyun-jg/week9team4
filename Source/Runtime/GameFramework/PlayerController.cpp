#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/TestPawn.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
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

	UClass* ManagerClass = PlayerCameraManagerClass;

	if (!ManagerClass || !ManagerClass->IsChildOf(APlayerCameraManager::StaticClass()) || !ManagerClass->Constructor)
	{
		ManagerClass = APlayerCameraManager::StaticClass();
	}

	PlayerCameraManager = Cast<APlayerCameraManager>(World->SpawnActor(ManagerClass));

	if (PlayerCameraManager)
	{
		PlayerCameraManager->InitializeFor(this);
		PlayerCameraManager->SetViewTarget(GetPawn());
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
	if (IsValid(PlayerCameraManager) && !PlayerCameraManager->IsActorBeingDestroyed())
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

	if (IsValid(OldPawn))
	{
		if (!OldPawn->IsActorBeingDestroyed())
		{
			FMinimalViewInfo SavedView{};

			// 활성 카메라가 없는 Pawn도 기본 위치·회전을 제공한다.
			SavedView.Location = OldPawn->GetActorLocation();
			SavedView.Rotation = OldPawn->GetActorRotation();

			// 활성카메라가 있으면 해당 카메라의 설정으로 변경
			OldPawn->CalcCamera(0.0f, SavedView);

			FallbackCameraView = SavedView;
		}
		OldPawn->UnPossessed();
	}

	PossessedPawn = nullptr;

	if (!IsPendingKillPending() && OldPawn && IsValid(PlayerCameraManager) && PlayerCameraManager->GetViewTarget() == OldPawn)
	{
		PlayerCameraManager->SetViewTarget(this);
	}

/*
	if (OldPawn)
	{
		OldPawn->UnPossessed();
	}

	PossessedPawn = nullptr;

	// 현재 카메라 시점을 계산하는 기준 Actor가 이전의 Actor인지 체크
	// UnPossess 과정에서 기존 Pawn이 ViewTarget이면 PlayerController 자신으로 되돌린다.
	if (OldPawn && IsValid(PlayerCameraManager) && PlayerCameraManager->GetViewTarget() == OldPawn)
	{
		// ToFo: CalcCamera() 재정의 필요
		PlayerCameraManager->SetViewTarget(this);
	}*/
}

void APlayerController::InputKey(int32 Key, bool bDown)
{
	if (IsPendingKillPending()) return;
	if (Key < 0 || Key >= 256) return;
	const bool bPressed = bDown && !bKeyDown[Key];
	bKeyDown[Key] = bDown;
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
