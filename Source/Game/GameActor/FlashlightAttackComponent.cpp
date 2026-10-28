#include "EnginePCH.h"
#include "FlashlightAttackComponent.h"
#include "FlashlightActor.h"
#include "GhostActor.h"
#include "PlayerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include <algorithm>
#include <cmath>

namespace
{
	bool Live(const AActor* Actor)
	{
		return Actor && GUObjectArray.Find(const_cast<AActor*>(Actor)) != INDEX_NONE && IsValid(Actor) && !Actor->IsActorBeingDestroyed();
	}
	float Progress(float Elapsed, float Duration) { return std::clamp(Elapsed / std::max(0.001f, Duration), 0.0f, 1.0f); }
	float Smooth(float T) { return T * T * (3.0f - 2.0f * T); }
}

UFlashlightAttackComponent::UFlashlightAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Evaluate the swing after player movement.
	PrimaryComponentTick.TickGroup = ETickingGroup::PostPhysics;
}

void UFlashlightAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Player = Cast<APlayerPawn>(GetOwner()); Live(Player))
		if (auto* Flashlight = Player->GetFlashlightActor(); Live(Flashlight))
			Flashlight->GetAttackCapsule()->OnComponentBeginOverlap.AddRaw(this, &UFlashlightAttackComponent::HandleAttackOverlap);
}

void UFlashlightAttackComponent::EndPlay()
{
	CancelAttack();
	// EndPlayComponent removes this component's delegate bindings through the World.
	Super::EndPlay();
}

bool UFlashlightAttackComponent::CanContinue() const
{
	const auto* Player = Cast<APlayerPawn>(GetOwner());
	const auto* Controller = GetWorld() ? GetWorld()->GetPlayerController() : nullptr;
	return Live(Player) && Live(Player->GetFlashlightActor()) && GetWorld() &&
		(!Controller || (!Controller->IsMoveInputIgnored() && !Controller->IsInCinematicMode())) &&
		(!GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput());
}

bool UFlashlightAttackComponent::TryStartAttack()
{
	if (IsAttacking() || !CanContinue() || GetWorld()->IsPaused()) return false;
	auto* Player = CastChecked<APlayerPawn>(GetOwner());
	auto* Flashlight = Player->GetFlashlightActor();
	if (!Flashlight->TryConsumeGauge(BatteryCost))
	{
		HTR_LOG(Warning, "FlashlightAttack: insufficient battery ({}/{})", Flashlight->GetGauge(), BatteryCost);
		return false;
	}
	RestPose = Flashlight->GetRootComponent()->GetTransform();
	Phase = EFlashlightAttackPhase::Windup;
	PhaseElapsed = 0.0f;
	ApplyVisuals();
	return true;
}

void UFlashlightAttackComponent::CancelAttack()
{
	EndHitStop();
	StopHitCameraShake();
	const bool bWasAttacking = IsAttacking();
	Phase = EFlashlightAttackPhase::Idle;
	PhaseElapsed = 0.0f;
	if (auto* Player = Cast<APlayerPawn>(GetOwner()); Live(Player))
		if (auto* Flashlight = Player->GetFlashlightActor(); Live(Flashlight))
		{
			Flashlight->GetAttackCapsule()->SetCollisionEnabled(false);
			if (bWasAttacking) Flashlight->GetRootComponent()->SetTransform(RestPose);
			Flashlight->SetAttackPresentation(1.0f, false);
		}
}

void UFlashlightAttackComponent::StartSwing()
{
	Phase = EFlashlightAttackPhase::Swing;
	PhaseElapsed = 0.0f;
	CastChecked<APlayerPawn>(GetOwner())->GetFlashlightActor()->SetAttackTrailsEnabled(true);
	if (UGameInstance* Instance = GetWorld()->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX("Assets/Sounds/lightsaver.wav");
	ApplyVisuals();
	HTR_LOG(Info, "FlashlightAttack: swing started (max reach {} m)", AttackReach);
}

void UFlashlightAttackComponent::StartRecovery()
{
	if (Phase != EFlashlightAttackPhase::Swing) return;
	auto* Player = CastChecked<APlayerPawn>(GetOwner());
	auto* Flashlight = Player->GetFlashlightActor();
	RecoveryStartPose = Flashlight->GetRootComponent()->GetTransform();
	Phase = EFlashlightAttackPhase::Recovery;
	PhaseElapsed = 0.0f;
	Flashlight->GetAttackCapsule()->SetCollisionEnabled(false);
	ApplyVisuals();
}

void UFlashlightAttackComponent::HandleAttackOverlap(UPrimitiveComponent* Self, UPrimitiveComponent* Other)
{
	if (Phase != EFlashlightAttackPhase::Swing || !Other) return;
	auto* Ghost = Cast<AGhostActor>(Other->GetOwner());
	if (!Ghost || Other != Ghost->GetHurtCapsule() || !Ghost->CanReceiveFlashlightHit()) return;
	CastChecked<APlayerPawn>(GetOwner())->GetFlashlightActor()->SetAttackTrailsEnabled(false);
	Ghost->ReceiveFlashlightHit(Self->GetWorldRotation().Quaternion().GetUpVector(), HitStopDuration, GhostStaggerDuration);
	BeginHitStop();
	if (!IsHitStopped()) StartRecovery();
	HTR_LOG(Info, "FlashlightAttack: hit '{}', hitstop {} s", Ghost->GetName(), HitStopDuration);
}

void UFlashlightAttackComponent::BeginHitStop()
{
	if (HitStopDuration <= 0.0f) return;
	if (IsHitStopped())
	{
		HitStopRemaining = HitStopDuration;
		return;
	}
	auto* Player = CastChecked<APlayerPawn>(GetOwner());
	auto* Light = Player->GetFlashlightActor();
	HitStopRemaining = HitStopDuration;
	PlayerTimeDilationBeforeStop = Player->CustomTimeDilation;
	FlashlightTimeDilationBeforeStop = Light->CustomTimeDilation;
	Player->CustomTimeDilation = 0.0f;
	// The flashlight is attached to the player but is a separate Actor.
	Light->CustomTimeDilation = 0.0f;
	StartHitCameraShake();
}

void UFlashlightAttackComponent::StartHitCameraShake()
{
	StopHitCameraShake();
	if (!bHitCameraShakeEnabled) return;
	auto* Controller = GetWorld() ? GetWorld()->GetPlayerController() : nullptr;
	if (!Live(Controller) || Controller->GetPawn() != GetOwner()) return;
	auto* CameraManager = Controller->GetPlayerCameraManager();
	if (!Live(CameraManager) || !CameraManager->IsViewTarget(GetOwner())) return;

	FCameraShakeSettings Settings;
	Settings.Pattern = ECameraShakePattern::PerlinNoise;
	Settings.Seed = ++HitCameraShakeSeed;
	Settings.Duration = HitCameraShakeDuration;
	Settings.BlendInTime = std::min(0.01f, HitCameraShakeDuration * 0.1f);
	Settings.BlendOutTime = HitCameraShakeDuration * 0.75f;
	Settings.LocationAmplitude = HitCameraShakeLocationAmplitude;
	Settings.LocationFrequency = FVector(HitCameraShakeFrequency, HitCameraShakeFrequency, HitCameraShakeFrequency);
	Settings.RotationAmplitude = HitCameraShakeRotationAmplitude;
	Settings.RotationFrequency = Settings.LocationFrequency;
	// The manager advances with World delta, even while the player's delta is zero.
	HitCameraShakeHandle = CameraManager->StartCameraShake(Settings, HitCameraShakeScale);
}

void UFlashlightAttackComponent::StopHitCameraShake()
{
	if (!HitCameraShakeHandle.IsValid()) return;
	auto* Controller = GetWorld() ? GetWorld()->GetPlayerController() : nullptr;
	if (Live(Controller))
		if (auto* CameraManager = Controller->GetPlayerCameraManager(); Live(CameraManager))
			CameraManager->StopCameraShake(HitCameraShakeHandle, true);
	HitCameraShakeHandle = {};
}

void UFlashlightAttackComponent::SetPlayerTimeDilation(float Dilation)
{
	if (IsHitStopped()) PlayerTimeDilationBeforeStop = Dilation;
	else if (auto* Player = Cast<APlayerPawn>(GetOwner()); Live(Player)) Player->SetCustomTimeDilation(Dilation);
}

void UFlashlightAttackComponent::EndHitStop()
{
	if (!IsHitStopped()) return;
	HitStopRemaining = 0.0f;
	if (auto* Player = Cast<APlayerPawn>(GetOwner()); IsValid(Player))
	{
		Player->CustomTimeDilation = PlayerTimeDilationBeforeStop;
		if (auto* Light = Player->GetFlashlightActor(); Live(Light))
			Light->CustomTimeDilation = FlashlightTimeDilationBeforeStop;
	}
}

void UFlashlightAttackComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!IsAttacking()) return;
	if (!CanContinue()) { CancelAttack(); return; }
	if (IsHitStopped())
	{
		// This component still ticks with the owner's dilated delta (zero during
		// hitstop). Only the release countdown uses the World frame delta.
		const float WorldDeltaTime = GetWorld()->GetDeltaSeconds();
		if (HitStopRemaining <= WorldDeltaTime)
		{
			EndHitStop();
			StartRecovery();
		}
		else HitStopRemaining -= WorldDeltaTime;
	}
	PhaseElapsed += DeltaTime;
	ApplyVisuals();
	if (Phase == EFlashlightAttackPhase::Windup)
	{
		if (PhaseElapsed >= WindupDuration) StartSwing();
	}
	else if (Phase == EFlashlightAttackPhase::Swing)
	{
		if (IsHitStopped()) return;
		if (PhaseElapsed >= SwingDuration) StartRecovery();
	}
	else if (PhaseElapsed >= RecoveryDuration) CancelAttack();
}

void UFlashlightAttackComponent::ApplyVisuals()
{
	auto* Player = CastChecked<APlayerPawn>(GetOwner());
	auto* Flashlight = Player->GetFlashlightActor();
	const FQuat Coordinates = SwingCoordinateRotation.Quaternion();
	const float HalfAngle = std::max(0.0f, SwingHalfAngle);
	auto FanPose = [&](float AngleDegrees)
	{
		// A horizontal XY fan, rotated as a whole in camera space. Position
		// and flashlight direction share these coordinates and the same angle.
		const float Angle = AngleDegrees * PI / 180.0f;
		const FVector Direction = Coordinates.GetForwardVector() * std::cos(Angle) +
			Coordinates.GetRightVector() * std::sin(Angle);
		const FRotator Rotation = (Coordinates * FRotator(0.0f, AngleDegrees, 0.0f).Quaternion()).ToFRotator();
		return FTransform(Rotation, SwingPivot + Direction * std::max(0.0f, SwingHandRadius));
	};
	const FTransform StartPose = FanPose(HalfAngle);
	FTransform From = RestPose, To = StartPose;
	float T = Progress(PhaseElapsed, WindupDuration);
	if (Phase == EFlashlightAttackPhase::Swing)
	{
		T = Progress(PhaseElapsed, SwingDuration);
	}
	else if (Phase == EFlashlightAttackPhase::Recovery)
	{
		From = RecoveryStartPose; To = RestPose;
		T = Progress(PhaseElapsed, RecoveryDuration);
	}
	T = Smooth(T);
	const FTransform Pose = Phase == EFlashlightAttackPhase::Swing ? FanPose(HalfAngle * (1.0f - 2.0f * T)) :
		FTransform(FRotator(
			From.Rotation.Pitch + (To.Rotation.Pitch - From.Rotation.Pitch) * T,
			From.Rotation.Yaw + (To.Rotation.Yaw - From.Rotation.Yaw) * T,
			From.Rotation.Roll + (To.Rotation.Roll - From.Rotation.Roll) * T),
			From.Location + (To.Location - From.Location) * T);
	Flashlight->GetRootComponent()->SetRelativeLocationAndRotation(Pose.Location, Pose.Rotation);
	if (Phase == EFlashlightAttackPhase::Swing)
	{
		Flashlight->UpdateAttackBeam(AttackReach, CapsuleRadius, Player);
		if (Phase != EFlashlightAttackPhase::Swing) return;
		const float Angle = HalfAngle * (1.0f - 2.0f * T);
		for (int32 Index = 0; Index < AFlashlightActor::AttackTrailCount; ++Index)
			Flashlight->UpdateAttackTrail(Index,
				FanPose(std::min(HalfAngle, Angle + std::max(0.0f, TrailAngleStep) * (Index + 1))),
				AttackReach, CapsuleRadius, Player);
	}
	if (!IsAttacking()) return;
	const float Brightness = Phase == EFlashlightAttackPhase::Windup ? 1.0f + (FlashMultiplier - 1.0f) * T :
		Phase == EFlashlightAttackPhase::Recovery ? FlashMultiplier + (1.0f - FlashMultiplier) * T : FlashMultiplier;
	Flashlight->SetAttackPresentation(Brightness, Phase == EFlashlightAttackPhase::Swing);
}
