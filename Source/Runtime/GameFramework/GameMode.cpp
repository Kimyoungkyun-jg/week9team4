#include "EnginePCH.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/Actor/PlayerStart.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

AGameMode::AGameMode()
	: DefaultPawnClass(ADefaultPawn::StaticClass())
	, PlayerControllerClass(APlayerController::StaticClass())
	, HUDClass(AGameHUD::StaticClass())
{
}

bool AGameMode::SetDefaultPawnClass(UClass* Class)
{
	if (!Class || !Class->IsChildOf(APawn::StaticClass()) || !Class->Constructor) return false;
	DefaultPawnClass = Class;
	return true;
}

bool AGameMode::SetPlayerControllerClass(UClass* Class)
{
	if (!Class || !Class->IsChildOf(APlayerController::StaticClass()) || !Class->Constructor) return false;
	PlayerControllerClass = Class;
	return true;
}

bool AGameMode::SetHUDClass(UClass* Class)
{
	if (!Class || !Class->IsChildOf(AGameHUD::StaticClass()) || !Class->Constructor) return false;
	HUDClass = Class;
	return true;
}

void AGameMode::SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController)
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (!OutPawn)
	{
		UClass* PawnClass = DefaultPawnClass;
		if (!PawnClass || !PawnClass->IsChildOf(APawn::StaticClass()) || !PawnClass->Constructor)
		{
			HTR_LOG(Warning, "Invalid DefaultPawnClass; using ADefaultPawn");
			PawnClass = ADefaultPawn::StaticClass();
		}
		FTransform Transform = FTransform::Identity;
		APlayerStart* Start = nullptr;
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			Start = Cast<APlayerStart>(Actor);
			if (Start) break;
		}
		if (Start)
			Transform = Start->GetActorTransform();
		else if (ACameraActor* Camera = World->GetMainCamera())
		{
			if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
				Transform = FTransform(CameraComponent->GetWorldRotation(), CameraComponent->GetWorldLocation());
		}
		OutPawn = Cast<APawn>(World->SpawnActor(PawnClass, NAME_None, &Transform));
	}

	if (!OutController)
	{
		UClass* ControllerClass = PlayerControllerClass;
		if (!ControllerClass || !ControllerClass->IsChildOf(APlayerController::StaticClass()) || !ControllerClass->Constructor)
		{
			HTR_LOG(Warning, "Invalid PlayerControllerClass; using APlayerController");
			ControllerClass = APlayerController::StaticClass();
		}
		OutController = Cast<APlayerController>(World->SpawnActor(ControllerClass));
	}
	if (OutController && OutPawn) OutController->Possess(OutPawn);
}
