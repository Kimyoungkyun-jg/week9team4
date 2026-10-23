#include "EnginePCH.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/DefaultPawn.h"
#include "Engine/World.h"
#include "Engine/Level.h"

AGameMode::AGameMode()
	: DefaultPawnClass(ADefaultPawn::StaticClass())
	, PlayerControllerClass(APlayerController::StaticClass())
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

void AGameMode::SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController)
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (!OutPawn)
	{
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			if (APawn* Pawn = Cast<APawn>(Actor)) { OutPawn = Pawn; break; }
		}
	}
	if (!OutPawn)
	{
		UClass* PawnClass = DefaultPawnClass;
		if (!PawnClass || !PawnClass->IsChildOf(APawn::StaticClass()) || !PawnClass->Constructor)
		{
			HTR_LOG(Warning, "Invalid DefaultPawnClass; using ADefaultPawn");
			PawnClass = ADefaultPawn::StaticClass();
		}
		const FTransform Transform = World->GetMainCamera() ? World->GetMainCamera()->GetActorTransform() : FTransform::Identity;
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
