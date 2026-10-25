#include "EnginePCH.h"
#include "PlayerPawn.h"
#include "FlashlightActor.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"

APlayerPawn::APlayerPawn() = default;

void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	UWorld* PawnWorld = GetWorld();
	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game)) return;

	UCameraComponent* Camera = GetCameraComponent();
	if (!Camera) return;

	FlashlightActor = PawnWorld->SpawnActor<AFlashlightActor>();
	FlashlightActor->SetActorEnableCollision(false);
	FlashlightActor->AttachToComponent(Camera);
	FlashlightActor->SetActorRelativeLocation({0.35f, 0.28f, -0.2f});
	FlashlightActor->SetActorRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
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
