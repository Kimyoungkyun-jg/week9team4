#include "EnginePCH.h"
#include "PlayerPawn.h"
#include "FlashlightActor.h"
#include "FlashlightAttackComponent.h"
#include "Components/LuaScriptComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/HeadBobComponent.h"
#include "Camera/CameraModifier_HeadBob.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "GameFramework/GameHUD.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include <algorithm>
#include <string>

APlayerPawn::APlayerPawn()
{
	auto* SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	// Pacman's authored walls block Pawn but ignore Camera; ghosts only overlap
	// Pawn. Reuse that response table for this query without changing map assets.
	SpringArm->ProbeChannel = ECollisionChannel::Pawn;
	SpringArm->SetupAttachment(GetCapsuleComponent());
	auto* ArmEnd = CreateDefaultSubobject<USceneComponent>("ArmEnd");
	ArmEnd->SetupAttachment(SpringArm);
	SpringArm->SetArmEnd(ArmEnd);
	auto* FlashlightPivot = CreateDefaultSubobject<USceneComponent>("FlashlightPivot");
	FlashlightPivot->SetupAttachment(GetCapsuleComponent());
	if (auto* Camera = GetCameraComponent())
	{
		SpringArm->SetRelativeLocation(Camera->GetRelativeLocation());
		FlashlightPivot->SetRelativeLocation(Camera->GetRelativeLocation());
		Camera->SetupAttachment(ArmEnd);
		Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
	}
	CreateDefaultSubobject<UHeadBobComponent>("HeadBob");
	FlashlightAttack = CreateDefaultSubobject<UFlashlightAttackComponent>("FlashlightAttack");
	auto* Script = CreateDefaultSubobject<ULuaScriptComponent>("PlayerScript");
	Script->SetScriptPath("Assets/Scripts/PlayerPawn.lua");
	// Pacman rules: collision with ghosts is an overlap, and the player cannot jump.
	GetCharacterMovement()->SetJumpAllowed(false);
	auto* CollisionComponent = GetCapsuleComponent();
	CollisionComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
	CollisionComponent->SetGenerateOverlapEvents(true);
}

void APlayerPawn::BeginPlay()
{
	// Old scenes can still attach CameraComponent directly to the capsule. Capture
	// the authored first-person anchor before ACharacter removes spawn Pitch.
	UCameraComponent* Camera = GetCameraComponent();
	const FVector CameraAnchor = Camera && GetCapsuleComponent() ?
		GetCapsuleComponent()->GetWorldMatrix().Inverse().TransformPosition(Camera->GetWorldLocation()) : FVector::ZeroVector;
	ViewPitch = FMath::Clamp(GetActorRotation().Pitch, -89.0f, 89.0f);
	if (auto* Pivot = GetFlashlightPivot())
		Pivot->SetRelativeLocationAndRotation(CameraAnchor, FRotator(ViewPitch, 0.0f, 0.0f));
	UWorld* PawnWorld = GetWorld();
	// Lua BeginPlay runs inside Super::BeginPlay, so its flashlight must exist first.
	if (Camera && PawnWorld && (PawnWorld->GetWorldType() == EWorldType::PIE || PawnWorld->GetWorldType() == EWorldType::Game))
	{
		FlashlightActor = PawnWorld->SpawnActor<AFlashlightActor>();
		if (FlashlightActor)
		{
			FlashlightActor->AttachToComponent(GetFlashlightPivot());
			FlashlightActor->SetActorRelativeLocation({0.35f, 0.28f, -0.2f});
			FlashlightActor->SetActorRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
		}
	}
	Super::BeginPlay();
	ResetWalkingEffects();
	PlayerStartLocation = GetActorLocation();
	PlayerStartRotation = GetActorRotation();
	PlayerStartViewPitch = ViewPitch;
	PlayerStartArmLocation = CameraAnchor;
	if (auto* SpringArm = GetSpringArmComponent())
	{
		SpringArm->SetRelativeLocationAndRotation(CameraAnchor, FRotator(ViewPitch, 0.0f, 0.0f));
		auto* ArmEnd = Cast<USceneComponent>(FindComponentByName(FName("ArmEnd")));
		if (ArmEnd)
		{
			ArmEnd->SetupAttachment(SpringArm);
			SpringArm->SetArmEnd(ArmEnd);
			if (Camera)
			{
				Camera->SetupAttachment(ArmEnd);
				Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
			}
		}
	}
	if (auto* Pivot = GetFlashlightPivot())
	{
		// The light follows the original eye/hand anchor, independently of the
		// camera endpoint, camera lag, and the hit sequence's arm reset.
		Pivot->SetupAttachment(GetCapsuleComponent());
		Pivot->SetRelativeLocationAndRotation(CameraAnchor, FRotator(ViewPitch, 0.0f, 0.0f));
	}
	SetCameraMode(EPlayerCameraMode::FirstPerson);

	SetActorEnableCollision(true);
	auto* CollisionComponent = GetCapsuleComponent();
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionEnabled(true);
		CollisionComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
		CollisionComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
		CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
		CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
		CollisionComponent->SetGenerateOverlapEvents(true);
		CollisionComponent->UpdateOverlaps();
	}
}

void APlayerPawn::ConfigureFlashlight(float DrainPerSecond, float AttackCost, float SwingSeconds, float AttackReach)
{
	if (FlashlightActor) FlashlightActor->SetDrainRatePerSecond(DrainPerSecond);
	FlashlightAttack->BatteryCost = std::max(0.0f, AttackCost);
	FlashlightAttack->SwingDuration = std::max(0.001f, SwingSeconds);
	FlashlightAttack->AttackReach = std::max(0.0f, AttackReach);
}

void APlayerPawn::ConfigureFlashlightSwing(const FVector& Pivot, const FRotator& CoordinateRotation, float HalfAngle, float HandRadius)
{
	FlashlightAttack->SwingPivot = Pivot;
	FlashlightAttack->SwingCoordinateRotation = CoordinateRotation;
	FlashlightAttack->SwingHalfAngle = std::max(0.0f, HalfAngle);
	FlashlightAttack->SwingHandRadius = std::max(0.0f, HandRadius);
}

void APlayerPawn::ConfigureFlashlightAttack(float WindupSeconds, float RecoverySeconds, float CapsuleRadius, float HitStopSeconds, float GhostStaggerSeconds, float FlashMultiplier, float TrailAngleStep)
{
	FlashlightAttack->WindupDuration = std::max(0.0f, WindupSeconds);
	FlashlightAttack->RecoveryDuration = std::max(0.0f, RecoverySeconds);
	FlashlightAttack->CapsuleRadius = std::max(0.0f, CapsuleRadius);
	FlashlightAttack->HitStopDuration = std::max(0.0f, HitStopSeconds);
	FlashlightAttack->GhostStaggerDuration = std::max(0.1f, GhostStaggerSeconds);
	FlashlightAttack->FlashMultiplier = std::max(0.0f, FlashMultiplier);
	FlashlightAttack->TrailAngleStep = std::max(0.0f, TrailAngleStep);
}

void APlayerPawn::ConfigureFlashlightHitCameraShake(bool bEnabled, float Duration, float Scale, float Frequency, const FVector& LocationAmplitude, const FRotator& RotationAmplitude)
{
	FlashlightAttack->bHitCameraShakeEnabled = bEnabled;
	FlashlightAttack->HitCameraShakeDuration = std::max(0.0f, Duration);
	FlashlightAttack->HitCameraShakeScale = std::max(0.0f, Scale);
	FlashlightAttack->HitCameraShakeFrequency = std::max(0.0f, Frequency);
	FlashlightAttack->HitCameraShakeLocationAmplitude = LocationAmplitude;
	FlashlightAttack->HitCameraShakeRotationAmplitude = RotationAmplitude;
}

void APlayerPawn::PrimaryAction()
{
	if (FlashlightAttack) FlashlightAttack->TryStartAttack();
}

bool APlayerPawn::IsInputSuppressed() const
{
	return FlashlightAttack && FlashlightAttack->IsHitStopped();
}

void APlayerPawn::SetSlomoGauge(float NewGauge)
{
	SlomoGauge = std::isfinite(NewGauge) ? std::clamp(NewGauge, 0.0f, MaxSlomoGauge) : 0.0f;
	if (SlomoGauge <= 0.0f) SetSlomoActive(false);
}

bool APlayerPawn::StartSlomo(float Dilation)
{
	if (bIsSlomoActive) return true;
	UWorld* PawnWorld = GetWorld();
	const auto* Controller = PawnWorld ? PawnWorld->GetPlayerController() : nullptr;
	if (IsPendingKillPending() || !PawnWorld || PawnWorld->IsPaused() || IsInputSuppressed() ||
		SlomoGauge <= 0.0f || GetCustomTimeDilation() <= 0.0f || !std::isfinite(Dilation) || Dilation <= 0.0f ||
		!PawnWorld->GetWorldSettings() || !PawnWorld->GetWorldSettings()->bAllowTimeDilation ||
		(PawnWorld->GetGameMode() && !PawnWorld->GetGameMode()->AllowsPlayerControllerInput()) ||
		(Controller && (Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored() || Controller->IsInCinematicMode()))) return false;
	WorldDilationBeforeSlomo = PawnWorld->GetWorldTimeDilation();
	PlayerDilationBeforeSlomo = GetCustomTimeDilation();
	PawnWorld->SetWorldTimeDilation(Dilation);
	SetCustomTimeDilation(PlayerDilationBeforeSlomo * WorldDilationBeforeSlomo / PawnWorld->GetWorldTimeDilation());
	bIsSlomoActive = true;
	return true;
}

void APlayerPawn::SetSlomoActive(bool bActive)
{
	if (bActive) { StartSlomo(0.3f); return; }
	if (!bIsSlomoActive) return;
	bIsSlomoActive = false;
	if (UWorld* PawnWorld = GetWorld()) PawnWorld->SetWorldTimeDilation(WorldDilationBeforeSlomo);
	if (FlashlightAttack && IsValid(FlashlightAttack) && !FlashlightAttack->IsBeingDestroyed())
		FlashlightAttack->SetPlayerTimeDilation(PlayerDilationBeforeSlomo);
	else SetCustomTimeDilation(PlayerDilationBeforeSlomo);
}

void APlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Future per-frame mode features belong here; the arm itself ticks ONLY in
	// PostUpdateWork, after movement and before PlayerCameraManager computes POV.
	// if (IsThirdPersonMode()) { /* TODO: third-person per-frame features. */ }
	// else { /* TODO: first-person per-frame features. */ }
	UWorld* PawnWorld = GetWorld();
	const bool bPlaying = PawnWorld && !PawnWorld->IsPaused() &&
		(PawnWorld->GetWorldType() == EWorldType::Game || PawnWorld->GetWorldType() == EWorldType::PIE);
	const auto* Movement = GetCharacterMovement();
	auto* Bob = GetHeadBobComponent();
	const bool bGrounded = Movement && Movement->IsMovingOnGround();
	const FVector FrameMovement = bGrounded ? Movement->GetLastGroundedDisplacement() : FVector::ZeroVector;
	const bool bMoved = bPlaying && (!Bob || !Bob->IsSuspended()) && FrameMovement.SizeSquared() > 0.00000001f;
	const bool bStep = FootstepCadence.Advance(DeltaTime, bMoved);
	const auto* Controller = PawnWorld ? PawnWorld->GetPlayerController() : nullptr;
	const bool bAllowsGameplay = bPlaying &&
		(!PawnWorld->GetGameMode() || PawnWorld->GetGameMode()->AllowsPlayerControllerInput()) &&
		(!Controller || (!Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored() && !Controller->IsInCinematicMode()));
	if (bIsSlomoActive && !bAllowsGameplay) SetSlomoActive(false);
	// Gauge uses the owner's Tick delta: normal player time in slomo, zero in hitstop.
	if (bAllowsGameplay && DeltaTime > 0.0f)
	{
		if (bIsSlomoActive)
		{
			SetSlomoGauge(SlomoGauge - SlomoDrainRate * DeltaTime);
		}
		else
		{
			SetSlomoGauge(SlomoGauge + SlomoRecoverRate * DeltaTime);
		}
	}

	// Camera effects use the completed grounded move, never input or a respawn teleport.
	if (bPlaying && Bob)
	{
		Bob->UpdateBob(DeltaTime, FrameMovement, bStep, bGrounded);
		if (auto* Controller = GetController())
		{
			if (auto* Manager = Controller->GetPlayerCameraManager(); IsValid(Manager) && !Manager->IsActorBeingDestroyed())
			{
				if (!Manager->FindCameraModifier(UCameraModifier_HeadBob::StaticClass()))
					Manager->AddNewCameraModifier(UCameraModifier_HeadBob::StaticClass());
			}
		}
	}
	if (!bStep) return;
	static constexpr const char* Steps[] = {"Assets/Sounds/footstep00.wav", "Assets/Sounds/footstep01.wav",
		"Assets/Sounds/footstep02.wav", "Assets/Sounds/footstep03.wav", "Assets/Sounds/footstep04.wav"};
	const int Index = std::uniform_int_distribution<int>(0, 4)(FootstepRandom);
	if (UGameInstance* Instance = PawnWorld->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX(Steps[Index], 0.5f);
}

USpringArmComponent* APlayerPawn::GetSpringArmComponent() const
{
	return Cast<USpringArmComponent>(FindComponentByName(FName("SpringArm")));
}

USceneComponent* APlayerPawn::GetFlashlightPivot() const
{
	// Resolve inside this pawn after scene load/PIE duplication, like ArmEnd.
	return Cast<USceneComponent>(FindComponentByName(FName("FlashlightPivot")));
}

void APlayerPawn::AddControllerPitchInput(float Value)
{
	if (bCameraControlSuspended || !std::isfinite(Value)) return;
	ViewPitch = FMath::Clamp(ViewPitch + Value, -89.0f, 89.0f);
	if (auto* SpringArm = GetSpringArmComponent())
		SpringArm->SetRelativeRotation(FRotator(ViewPitch, 0.0f, 0.0f));
	if (auto* Pivot = GetFlashlightPivot())
		Pivot->SetRelativeRotation(FRotator(ViewPitch, 0.0f, 0.0f));
}

void APlayerPawn::ToggleCameraMode()
{
	SetCameraMode(IsThirdPersonMode() ? EPlayerCameraMode::FirstPerson : EPlayerCameraMode::ThirdPerson);
}

void APlayerPawn::SetCameraMode(EPlayerCameraMode NewMode)
{
	// The hit sequence keeps its camera ownership and Head Bob suspension.
	if (bCameraControlSuspended || (NewMode != EPlayerCameraMode::FirstPerson && NewMode != EPlayerCameraMode::ThirdPerson)) return;
	// Also guard direct calls, not just Q input. First-person restoration remains
	// available for BeginPlay, respawn, and cinematic release in restricted modes.
	const UWorld* PawnWorld = GetWorld();
	const AGameMode* Mode = PawnWorld ? PawnWorld->GetGameMode() : nullptr;
	if (NewMode == EPlayerCameraMode::ThirdPerson && Mode && !Mode->AllowsCameraModeToggle()) return;
	CameraMode = NewMode;
	if (auto* SpringArm = GetSpringArmComponent())
	{
		if (IsThirdPersonMode())
		{
			SpringArm->Activate(true);
			// TODO: third-person model visibility and mode-specific settings.
		}
		else
		{
			SpringArm->Deactivate();
			// TODO: first-person model visibility and mode-specific settings.
		}
	}
	// TODO: smooth mode transitions; keep immediate endpoint reset when interrupted.
}

UHeadBobComponent* APlayerPawn::GetHeadBobComponent() const
{
	return GetComponentByClass<UHeadBobComponent>();
}

void APlayerPawn::ResetWalkingEffects()
{
	FootstepCadence.Reset();
	if (auto* Bob = GetHeadBobComponent()) Bob->Reset();
}

void APlayerPawn::SetHeadBobSuspended(bool bSuspended)
{
	FootstepCadence.Reset();
	if (auto* Bob = GetHeadBobComponent()) Bob->SetSuspended(bSuspended);
	if (bCameraControlSuspended == bSuspended) return;
	bCameraControlSuspended = bSuspended;
	if (auto* SpringArm = GetSpringArmComponent())
	{
		if (bSuspended)
		{
			SpringArm->Deactivate();
			// Existing hit code drives Camera relative to the pawn root. Remove the
			// inherited view offset/Pitch while it owns that transform.
			SpringArm->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
		}
		else
		{
			SpringArm->SetRelativeLocationAndRotation(PlayerStartArmLocation, FRotator(ViewPitch, 0.0f, 0.0f));
			SetCameraMode(CameraMode);
		}
	}
}

void APlayerPawn::SetLife(int32 InLife)
{
	Life = std::max(0, InLife);
	if (UWorld* PawnWorld = GetWorld())
	{
		if (AGameHUD* HUD = PawnWorld->GetHUD())
			HUD->SetText("Life", std::to_string(Life));
	}
}

void APlayerPawn::RespawnAtPlayerStart()
{
	if (FlashlightAttack) FlashlightAttack->CancelAttack();
	SetSlomoActive(false);
	ResetWalkingEffects();
	if (auto* Movement = GetCharacterMovement())
	{
		Movement->SetMovementEnabled(true);
		Movement->ResetAfterTeleport();
	}
	SetActorLocation(PlayerStartLocation);
	SetActorRotation(PlayerStartRotation);
	ViewPitch = PlayerStartViewPitch;
	if (auto* SpringArm = GetSpringArmComponent())
	{
		SpringArm->SetRelativeLocationAndRotation(PlayerStartArmLocation, FRotator(ViewPitch, 0.0f, 0.0f));
		SpringArm->ResetArmEnd();
	}
	if (auto* Pivot = GetFlashlightPivot())
		Pivot->SetRelativeLocationAndRotation(PlayerStartArmLocation, FRotator(ViewPitch, 0.0f, 0.0f));
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
	}
	SetCameraMode(CameraMode);
	// ResetWalkingEffects clears modifier input without releasing cinematic suspension.
	if (auto* CollisionComponent = GetCapsuleComponent()) CollisionComponent->UpdateOverlaps();
}

void APlayerPawn::EndPlay()
{
	if (FlashlightAttack) FlashlightAttack->CancelAttack();
	ResetWalkingEffects();
	SetSlomoActive(false);
	if (FlashlightActor)
	{
		FlashlightActor->Destroy();
		FlashlightActor = nullptr;
	}
	Super::EndPlay();
}
