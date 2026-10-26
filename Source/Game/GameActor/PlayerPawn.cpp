#include "EnginePCH.h"
#include "PlayerPawn.h"
#include "FlashlightActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "Engine/Level.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/GameHUD.h"
#include "Collision/HitResult.h"
#include <algorithm>
#include <cmath>
#include <string>

APlayerPawn::APlayerPawn()
{
	// Controller movement runs in PrePhysics; consume its final result once.
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	SetActorEnableCollision(true);
	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>("CollisionComponent");
	SetRootComponent(CollisionComponent);
	CollisionComponent->SetCapsuleSize(0.5f, 1.0f);
	CollisionComponent->SetCollisionEnabled(true);
	CollisionComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
	CollisionComponent->SetGenerateOverlapEvents(true);

	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetupAttachment(CollisionComponent);
	}
}

void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	UCameraComponent* Camera = GetCameraComponent();
	bHeadBobSuspended = false;
	if (Camera) HeadBobBaseLocation = Camera->GetRelativeLocation();
	ResetWalkingEffects();
	const FRotator SpawnRotation = GetActorRotation();
	SetActorRotation(FRotator(0.0f, SpawnRotation.Yaw, 0.0f));
	if (Camera) Camera->SetRelativeRotation(FRotator(SpawnRotation.Pitch, 0.0f, 0.0f));

	UWorld* PawnWorld = GetWorld();
	if (PawnWorld && PawnWorld->GetPersistentLevel())
		for (AActor* Actor : PawnWorld->GetPersistentLevel()->GetActors())
			if (AMapActor* Map = Cast<AMapActor>(Actor))
			{
				int32 Column, Row;
				EMapTileTerrain Terrain;
				FVector FloorCenter;
				if (Map->TryWorldToTileXYProjected(GetActorLocation(), Column, Row) &&
					Map->TryGetTile(Column, Row, Terrain) && Terrain == EMapTileTerrain::Floor &&
					Map->TryTileToWorldCenter(Column, Row, FloorCenter))
				{
					FVector Location = GetActorLocation();
					Location.Z = FloorCenter.Z + CollisionComponent->GetUnscaledCapsuleHalfHeight();
					SetActorLocation(Location);
				}
				break;
			}
	PlayerStartLocation = GetActorLocation();
	PlayerStartRotation = GetActorRotation();
	if (Camera)
	{
		PlayerStartCameraLocation = Camera->GetRelativeLocation();
		PlayerStartCameraRotation = Camera->GetRelativeRotation();
	}

	SetActorEnableCollision(true);
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

	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game)) return;

	if (!Camera) return;

	FlashlightActor = PawnWorld->SpawnActor<AFlashlightActor>();
	FlashlightActor->SetActorEnableCollision(false);
	FlashlightActor->AttachToComponent(Camera);
	FlashlightActor->SetActorRelativeLocation({0.35f, 0.28f, -0.2f});
	FlashlightActor->SetActorRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
}

void APlayerPawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	if (!CollisionComponent || bHeadBobSuspended || ScaleValue == 0.0f) return;
	const FVector FlatDirection(WorldDirection.X, WorldDirection.Y, 0.0f);
	const FVector Delta = FlatDirection.GetSafeNormal() * (ScaleValue * GetMoveSpeed());
	if (Delta.SizeSquared() <= SMALL_NUMBER) return;
	const FVector BeforeMove = GetActorLocation();
	const uint32 MoveRevision = WalkStateRevision;
	FHitResult Hit;
	const bool bMoved = CollisionComponent->MoveComponent(Delta, CollisionComponent->GetWorldRotation(), true, &Hit);
	// Overlap callbacks can start a hit sequence, respawn, or teleport during the sweep.
	if (MoveRevision != WalkStateRevision || bHeadBobSuspended ||
		(bMoved && (GetActorLocation() - Hit.Location).SizeSquared() > 0.00000001f)) return;
	if (Hit.bBlockingHit && !Hit.bStartPenetrating)
	{
		const FVector Normal(Hit.Normal.X, Hit.Normal.Y, 0.0f);
		const FVector Remaining = Delta * (1.0f - Hit.Time);
		const FVector Slide = Remaining - Normal * Remaining.Dot(Normal);
		if (Slide.SizeSquared() > SMALL_NUMBER)
		{
			FHitResult SlideHit;
			const bool bSlid = CollisionComponent->MoveComponent(Slide, CollisionComponent->GetWorldRotation(), true, &SlideHit);
			if (MoveRevision != WalkStateRevision || bHeadBobSuspended ||
				(bSlid && (GetActorLocation() - SlideHit.Location).SizeSquared() > 0.00000001f)) return;
		}
	}
	// Sample only the completed swept move, not arbitrary location changes between frames.
	// A teleport from an overlap callback must not count as a walk either.
	const FVector AfterMove = GetActorLocation();
	const FVector Actual(AfterMove.X - BeforeMove.X, AfterMove.Y - BeforeMove.Y, 0.0f);
	UWorld* PawnWorld = GetWorld();
	if (HasBegunPlay() && PawnWorld && !PawnWorld->IsPaused() &&
		(PawnWorld->GetWorldType() == EWorldType::Game || PawnWorld->GetWorldType() == EWorldType::PIE) &&
		Actual.SizeSquared() <= Delta.SizeSquared() + 0.0001f)
		WalkDisplacement += Actual;
}

void APlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* PawnWorld = GetWorld();
	const bool bPlaying = PawnWorld && !PawnWorld->IsPaused() &&
		(PawnWorld->GetWorldType() == EWorldType::Game || PawnWorld->GetWorldType() == EWorldType::PIE);
	const FVector FrameMovement = WalkDisplacement;
	const bool bMoved = bPlaying && !bHeadBobSuspended && FrameMovement.SizeSquared() > 0.00000001f;
	const bool bStep = FootstepCadence.Advance(DeltaTime, bMoved);
	WalkDisplacement = FVector::ZeroVector;
	// Bob progression and recovery must run even on frames without a footstep or audio device.
	if (bPlaying && !bHeadBobSuspended) UpdateHeadBob(DeltaTime, FrameMovement, bStep);
	if (!bStep) return;
	static constexpr const char* Steps[] = {"Assets/Sounds/footstep00.wav", "Assets/Sounds/footstep01.wav",
		"Assets/Sounds/footstep02.wav", "Assets/Sounds/footstep03.wav", "Assets/Sounds/footstep04.wav"};
	const int Index = std::uniform_int_distribution<int>(0, 4)(FootstepRandom);
	if (UGameInstance* Instance = PawnWorld->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX(Steps[Index], 0.5f);
}

void APlayerPawn::ResetHeadBob()
{
	HeadBobOffset = 0.0f;
	HeadBobStartOffset = 0.0f;
	HeadBobStepAmplitude = 0.0f;
	HeadBobElapsed = 0.0f;
	bHeadBobActive = false;
	bHeadBobReturning = false;
	if (!bHeadBobSuspended)
		if (UCameraComponent* Camera = GetCameraComponent()) Camera->SetRelativeLocation(HeadBobBaseLocation);
}

void APlayerPawn::ResetWalkingEffects()
{
	++WalkStateRevision;
	FootstepCadence.Reset();
	WalkDisplacement = FVector::ZeroVector;
	ResetHeadBob();
}

void APlayerPawn::SetHeadBobSuspended(bool bSuspended)
{
	if (bHeadBobSuspended == bSuspended) return;
	if (bSuspended)
	{
		// Remove the offset before the hit sequence saves the normal camera transform.
		ResetWalkingEffects();
		bHeadBobSuspended = true;
	}
	else
	{
		// The caller has restored the camera (or respawned) before returning ownership.
		if (UCameraComponent* Camera = GetCameraComponent()) HeadBobBaseLocation = Camera->GetRelativeLocation();
		bHeadBobSuspended = false;
		ResetWalkingEffects();
	}
}

void APlayerPawn::UpdateHeadBob(float DeltaTime, const FVector& Movement, bool bStep)
{
	if (!bHeadBobEnabled || !std::isfinite(HeadBobAmplitude) || HeadBobAmplitude <= 0.0f ||
		!std::isfinite(HeadBobDuration) || HeadBobDuration <= 0.0f)
	{
		ResetHeadBob();
		return;
	}
	if (!std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const float Duration = std::max(0.01f, HeadBobDuration);
	const bool bMoved = Movement.SizeSquared() > 0.00000001f;
	const float ForwardMultiplier = std::isfinite(HeadBobForwardMultiplier) ? std::max(0.0f, HeadBobForwardMultiplier) : 1.0f;
	const float MaxAmplitude = HeadBobAmplitude * std::max(1.0f, ForwardMultiplier);
	HeadBobOffset = std::clamp(HeadBobOffset, -MaxAmplitude, 0.0f);
	if (bStep)
	{
		// Use completed movement relative to the capsule's heading, including backward steps.
		const FVector Forward = GetActorRotation().Quaternion().GetForwardVector();
		const FVector FlatForward(Forward.X, Forward.Y, 0.0f);
		const float ForwardWeight = std::clamp(std::abs(Movement.GetSafeNormal().Dot(FlatForward.GetSafeNormal())), 0.0f, 1.0f);
		HeadBobStepAmplitude = HeadBobAmplitude * (1.0f + (ForwardMultiplier - 1.0f) * ForwardWeight);
		// Restart from the current bounded offset, never add another pulse to it.
		HeadBobStartOffset = HeadBobOffset;
		HeadBobElapsed = 0.0f;
		bHeadBobActive = true;
		bHeadBobReturning = false;
	}
	else if (!bMoved && bHeadBobActive && !bHeadBobReturning)
	{
		HeadBobStartOffset = HeadBobOffset;
		HeadBobElapsed = 0.0f;
		bHeadBobReturning = true;
	}
	if (bHeadBobActive)
	{
		HeadBobElapsed += DeltaTime;
		const float ReturnDuration = std::min(0.1f, Duration * 0.5f);
		const float T = std::clamp(HeadBobElapsed / (bHeadBobReturning ? ReturnDuration : Duration), 0.0f, 1.0f);
		const auto Smooth = [](float Value) { return Value * Value * (3.0f - 2.0f * Value); };
		if (bHeadBobReturning)
			HeadBobOffset = HeadBobStartOffset * (1.0f - Smooth(T));
		else if (T < 0.25f)
			HeadBobOffset = HeadBobStartOffset + (-HeadBobStepAmplitude - HeadBobStartOffset) * Smooth(T / 0.25f);
		else
			HeadBobOffset = -HeadBobStepAmplitude * (1.0f - Smooth((T - 0.25f) / 0.75f));
		HeadBobOffset = std::clamp(HeadBobOffset, -MaxAmplitude, 0.0f);
		if (T >= 1.0f)
		{
			HeadBobOffset = 0.0f;
			bHeadBobActive = false;
			bHeadBobReturning = false;
		}
	}
	// Relative translation is in capsule space, independent of camera pitch.
	if (UCameraComponent* Camera = GetCameraComponent())
		Camera->SetRelativeLocation(HeadBobBaseLocation + FVector(0.0f, 0.0f, HeadBobOffset));
}

void APlayerPawn::AddControllerPitchInput(float Value)
{
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		FRotator Rotation = Camera->GetRelativeRotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
		Camera->SetRelativeRotation(Rotation);
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
	ResetWalkingEffects();
	SetHeadBobSuspended(false);
	HeadBobBaseLocation = PlayerStartCameraLocation;
	SetActorLocation(PlayerStartLocation);
	SetActorRotation(PlayerStartRotation);
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetRelativeLocation(PlayerStartCameraLocation);
		Camera->SetRelativeRotation(PlayerStartCameraRotation);
	}
	if (CollisionComponent) CollisionComponent->UpdateOverlaps();
}

void APlayerPawn::EndPlay()
{
	ResetWalkingEffects();
	bHeadBobSuspended = false;
	HeadBobBaseLocation = FVector::ZeroVector;
	if (FlashlightActor)
	{
		FlashlightActor->Destroy();
		FlashlightActor = nullptr;
	}
	Super::EndPlay();
}
