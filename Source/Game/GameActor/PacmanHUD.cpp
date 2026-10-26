#include "EnginePCH.h"
#include "PacmanHUD.h"

#include "CoinActor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/CameraComponent.h"
#include "UI/Widget.h"
#include <cmath>

APacmanHUD::APacmanHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APacmanHUD::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	UMinimapWidget* Minimap = Cast<UMinimapWidget>(FindWidget("Minimap"));
	if (!World || !Minimap || !World->GetPersistentLevel()) return;
	const AMapActor* Map = nullptr;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if ((Map = Cast<AMapActor>(Actor))) break;
	Minimap->SetMap(Map);
	if (!Map || Map->GetGridTransformWarning())
	{
		Minimap->ClearPlayer();
		Minimap->SetCoins({});
		return;
	}

	TArray<FVector2> Coins;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if (ACoinActor* Coin = Cast<ACoinActor>(Actor))
		{
			FVector2 UV;
			if (Map->TryWorldToGridUVProjected(Coin->GetActorLocation(), UV)) Coins.Add(UV);
		}
	Minimap->SetCoins(std::move(Coins));

	APawn* Player = World->GetPlayerPawn();
	FVector2 PlayerUV;
	if (!Player || !Map->TryWorldToGridUVProjected(Player->GetActorLocation(), PlayerUV))
	{
		Minimap->ClearPlayer();
		return;
	}
	float Yaw = Player->GetActorRotation().Yaw;
	if (ADefaultPawn* DefaultPawn = Cast<ADefaultPawn>(Player))
		if (UCameraComponent* Camera = DefaultPawn->GetCameraComponent())
			Yaw = Camera->GetWorldRotation().Yaw;
	const float Radians = (Yaw - Map->GetActorRotation().Yaw) * 0.01745329252f;
	Minimap->SetPlayer(PlayerUV, {std::cos(Radians), std::sin(Radians)});
}
