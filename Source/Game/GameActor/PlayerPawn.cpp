#include "EnginePCH.h"
#include "PlayerPawn.h"
#include "FlashlightActor.h"
#include "Engine/World.h"
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
		if (AGameHUD* HUD = PawnWorld->GetHUD())
			HUD->SetText("Life", std::to_string(Life));
}

void APlayerPawn::EndPlay()
{
	if (FlashlightActor)
	{
		FlashlightActor->Destroy();
		FlashlightActor = nullptr;
	}
	Super::EndPlay();
}
