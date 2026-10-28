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

void APlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsPendingKillPending() || !GetWorld()) return;
	PlayerCameraManager = GetWorld()->SpawnActor<APlayerCameraManager>();
	if (PlayerCameraManager)
	{
		PlayerCameraManager->SetFlags(EObjectFlags::RF_Transient);
		PlayerCameraManager->Initialize(this);
		PlayerCameraManager->SetViewTarget(PossessedPawn);
	}
}

void APlayerController::EndPlay()
{
	APlayerCameraManager* Manager = GetPlayerCameraManager();
	PlayerCameraManager = nullptr;
	if (Manager && GetWorld() && GetWorld()->HasBegunPlay()) Manager->Destroy();
	Super::EndPlay();
}

APlayerCameraManager* APlayerController::GetPlayerCameraManager() const
{
	return PlayerCameraManager && GUObjectArray.Find(PlayerCameraManager) != INDEX_NONE &&
		IsValid(PlayerCameraManager) && !PlayerCameraManager->IsActorBeingDestroyed()
		? PlayerCameraManager : nullptr;
}

void APlayerController::SetViewTarget(AActor* InTarget)
{
	if (APlayerCameraManager* Manager = GetPlayerCameraManager()) Manager->SetViewTarget(InTarget);
}

void APlayerController::SetViewTargetWithBlend(AActor* NewTarget, float BlendTime, EViewTargetBlendFunction BlendFunc, float BlendExp, bool bLockOutgoing)
{
	if (APlayerCameraManager* Manager = GetPlayerCameraManager())
	{
		Manager->SetViewTargetWithBlend(NewTarget, BlendTime, BlendFunc, BlendExp, bLockOutgoing);
	}
}

AActor* APlayerController::GetViewTarget() const
{
	if (APlayerCameraManager* Manager = GetPlayerCameraManager()) return Manager->GetViewTarget();
	return nullptr;
}

void APlayerController::SetIgnoreMoveInput(bool bNewMoveInput)
{
	IgnoreMoveInputCount += bNewMoveInput ? 1 : -1;
	IgnoreMoveInputCount = std::max(0, IgnoreMoveInputCount);
}

void APlayerController::SetIgnoreLookInput(bool bNewLookInput)
{
	IgnoreLookInputCount += bNewLookInput ? 1 : -1;
	IgnoreLookInputCount = std::max(0, IgnoreLookInputCount);
}

void APlayerController::SetCinematicMode(bool bInCinematicMode, bool bHidePlayer, bool bAffectsMovement, bool bAffectsTurning)
{
	bCinematicMode = bInCinematicMode;
	if (bAffectsMovement)
	{
		SetIgnoreMoveInput(bInCinematicMode);
	}
	if (bAffectsTurning)
	{
		SetIgnoreLookInput(bInCinematicMode);
	}
	if (PossessedPawn && PossessedPawn->GetRootComponent())
	{
		PossessedPawn->GetRootComponent()->SetVisibility(!bHidePlayer);
	}
}

void APlayerController::UpdateCameraManager(float DeltaTime)
{
	if (APlayerCameraManager* Manager = GetPlayerCameraManager()) Manager->UpdateCamera(DeltaTime);
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

	if (!IsLookInputIgnored())
	{
		PossessedPawn->AddControllerYawInput(MouseAxisX * LookSensitivity);
		if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
		PossessedPawn->AddControllerPitchInput(MouseAxisY * LookSensitivity);
		if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
	}
	MouseAxisX = MouseAxisY = 0.0f;

	if (!IsMoveInputIgnored())
	{
		const float Forward = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::W)]) -
			static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::S)]);
		const float Right = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::D)]) -
			static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::A)]);
		const FQuat Rotation = PossessedPawn->GetActorRotation().Quaternion();
		if (Forward != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetForwardVector(), Forward);
		if (IsPendingKillPending() || !IsValid(PossessedPawn) || PossessedPawn->IsActorBeingDestroyed()) return;
		if (Right != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetRightVector(), Right);
	}
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
	if (APlayerCameraManager* Manager = GetPlayerCameraManager())
		if (!Manager->GetViewTarget()) Manager->SetViewTarget(PossessedPawn);
}

void APlayerController::UnPossess()
{
	if (APlayerCameraManager* Manager = GetPlayerCameraManager())
		if (Manager->GetViewTarget() == PossessedPawn) Manager->SetViewTarget(nullptr);
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
