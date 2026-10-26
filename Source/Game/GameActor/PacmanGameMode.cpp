#include "EnginePCH.h"
#include "PacmanGameMode.h"

#include "CoinActor.h"
#include "PlayerPawn.h"
#include "PacmanHUD.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/GameHUD.h"
#include <limits>
#include <string>

namespace
{
	constexpr int32 TargetCoinCount = 100;
	struct FCoinTile
	{
		int32 Column;
		int32 Row;
		FVector Center;
		int32 NearestCoinDistanceSquared = std::numeric_limits<int32>::max();
		bool bSelected = false;
	};
}

APacmanGameMode::APacmanGameMode()
{
	SetDefaultPawnClass(APlayerPawn::StaticClass());
	SetHUDClass(APacmanHUD::StaticClass());
}

void APacmanGameMode::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!World || !World->GetPersistentLevel()) return;

	AMapActor* Map = nullptr;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if ((Map = Cast<AMapActor>(Actor))) break;
	if (!Map)
	{
		HTR_LOG(Warning, "PacmanGameMode: no MapActor in level");
		return;
	}
	if (const char* Warning = Map->GetGridTransformWarning())
	{
		HTR_LOG(Warning, "PacmanGameMode: {}", Warning);
		return;
	}

	SetCoinCount(0);
	MaxCoinCount = 0;
	int32 StartColumn = -1, StartRow = -1;
	if (APawn* Player = World->GetPlayerPawn())
		Map->TryWorldToTileXYProjected(Player->GetActorLocation(), StartColumn, StartRow);

	TArray<FCoinTile> FloorTiles;
	for (int32 Row = 0; Row < Map->GetRows(); ++Row)
		for (int32 Column = 0; Column < Map->GetColumns(); ++Column)
		{
			if (Column == StartColumn && Row == StartRow) continue;
			EMapTileTerrain Terrain;
			if (!Map->TryGetTile(Column, Row, Terrain) || Terrain != EMapTileTerrain::Floor) continue;
			FVector Center;
			if (!Map->TryTileToWorldCenter(Column, Row, Center)) continue;
			FloorTiles.Add({Column, Row, Center});
		}
	for (int32 Attempt = 0; Attempt < FloorTiles.Num() && MaxCoinCount < TargetCoinCount; ++Attempt)
	{
		int32 BestIndex = INDEX_NONE;
		int32 BestScore = std::numeric_limits<int32>::min();
		for (int32 Index = 0; Index < FloorTiles.Num(); ++Index)
		{
			const FCoinTile& Tile = FloorTiles[Index];
			if (Tile.bSelected) continue;
			const int32 CenterX = Tile.Column * 2 + 1 - Map->GetColumns();
			const int32 CenterY = Tile.Row * 2 + 1 - Map->GetRows();
			const int32 Score = Attempt == 0 ? -(CenterX * CenterX + CenterY * CenterY) : Tile.NearestCoinDistanceSquared;
			if (Score > BestScore) { BestScore = Score; BestIndex = Index; }
		}
		if (BestIndex == INDEX_NONE) break;
		FCoinTile& Chosen = FloorTiles[BestIndex];
		Chosen.bSelected = true;
		const FTransform SpawnTransform(FRotator(), Chosen.Center + FVector(0.0f, 0.0f, 0.25f), FVector(0.4f, 0.4f, 0.4f));
		if (World->SpawnActor(ACoinActor::StaticClass(), NAME_None, &SpawnTransform)) ++MaxCoinCount;
		for (FCoinTile& Tile : FloorTiles)
		{
			const int32 DX = Tile.Column - Chosen.Column, DY = Tile.Row - Chosen.Row;
			Tile.NearestCoinDistanceSquared = std::min(Tile.NearestCoinDistanceSquared, DX * DX + DY * DY);
		}
	}

	if (AGameHUD* HUD = World->GetHUD())
		HUD->SetText("Coins", "0 / " + std::to_string(MaxCoinCount));
	HTR_LOG(Info, "PacmanGameMode: spawned {} coins", MaxCoinCount);
}
