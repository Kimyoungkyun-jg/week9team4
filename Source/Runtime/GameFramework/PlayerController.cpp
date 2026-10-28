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
}

void APlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessPlayerInput(DeltaTime);

	if (PlayerCameraManager)
	{
		PlayerCameraManager->UpdateCamera(DeltaTime);
	}
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
	if (PossessedPawn) PossessedPawn->PossessedBy(this);
	SpawnPlayerCameraManager();
}

APlayerCameraManager* APlayerController::GetPlayerCameraManager()
{
	if (!PlayerCameraManager)
	{
		SpawnPlayerCameraManager();
	}
	return PlayerCameraManager;
}

void APlayerController::SpawnPlayerCameraManager()
{
	if (!PlayerCameraManager)
	{
		if (UWorld* World = GetWorld())
		{
			PlayerCameraManager = Cast<APlayerCameraManager>(World->SpawnActor(APlayerCameraManager::StaticClass()));
		}
	}
}

void APlayerController::UnPossess()
{
	if (PossessedPawn) PossessedPawn->UnPossessed();
	PossessedPawn = nullptr;
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
