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
#include <string>

APlayerPawn::APlayerPawn()
{
	// Controller movement runs in PrePhysics; consume its final result once.
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	SetActorEnableCollision(true);
	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>("CollisionComponent");
	SetRootComponent(CollisionComponent);
	CollisionComponent->SetCapsuleSize(0.2f, 1.0f);
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
	FootstepCadence.Reset();
	WalkDisplacement = FVector::ZeroVector;
	UCameraComponent* Camera = GetCameraComponent();
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
	if (!CollisionComponent || ScaleValue == 0.0f) return;
	const FVector FlatDirection(WorldDirection.X, WorldDirection.Y, 0.0f);
	const FVector Delta = FlatDirection.GetSafeNormal() * (ScaleValue * GetMoveSpeed());
	if (Delta.SizeSquared() <= SMALL_NUMBER) return;
	const FVector BeforeMove = GetActorLocation();
	FHitResult Hit;
	CollisionComponent->MoveComponent(Delta, CollisionComponent->GetWorldRotation(), true, &Hit);
	if (Hit.bBlockingHit && !Hit.bStartPenetrating)
	{
		const FVector Normal(Hit.Normal.X, Hit.Normal.Y, 0.0f);
		const FVector Remaining = Delta * (1.0f - Hit.Time);
		const FVector Slide = Remaining - Normal * Remaining.Dot(Normal);
		if (Slide.SizeSquared() > SMALL_NUMBER)
			CollisionComponent->MoveComponent(Slide, CollisionComponent->GetWorldRotation(), true);
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
	const bool bStep = FootstepCadence.Advance(DeltaTime, bPlaying && WalkDisplacement.SizeSquared() > 0.00000001f);
	WalkDisplacement = FVector::ZeroVector;
	if (!bStep) return;
	static constexpr const char* Steps[] = {"Assets/Sounds/footstep00.wav", "Assets/Sounds/footstep01.wav",
		"Assets/Sounds/footstep02.wav", "Assets/Sounds/footstep03.wav", "Assets/Sounds/footstep04.wav"};
	const int Index = std::uniform_int_distribution<int>(0, 4)(FootstepRandom);
	if (UGameInstance* Instance = PawnWorld->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX(Steps[Index], 0.5f);
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
	const int32 PreviousLife = Life;
	Life = std::max(0, InLife);
	if (UWorld* PawnWorld = GetWorld())
	{
		if (Life < PreviousLife && HasBegunPlay() && !PawnWorld->IsPaused())
			if (UGameInstance* Instance = PawnWorld->GetGameInstance())
				if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX("Assets/Sounds/Damaged.wav");
		if (AGameHUD* HUD = PawnWorld->GetHUD())
			HUD->SetText("Life", std::to_string(Life));
	}
}

void APlayerPawn::EndPlay()
{
	FootstepCadence.Reset();
	WalkDisplacement = FVector::ZeroVector;
	if (FlashlightActor)
	{
		FlashlightActor->Destroy();
		FlashlightActor = nullptr;
	}
	Super::EndPlay();
}
