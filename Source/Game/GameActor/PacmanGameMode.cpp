#include "EnginePCH.h"
#include "PacmanGameMode.h"

#include "CoinActor.h"
#include "PlayerPawn.h"
#include "PacmanHUD.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/GameHUD.h"
#include "GameFramework/PlayerController.h"
#include "Components/LuaScriptComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "UI/Widget.h"
#include <algorithm>
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
	PrimaryActorTick.bCanEverTick = true;
	SetDefaultPawnClass(APlayerPawn::StaticClass());
	SetHUDClass(APacmanHUD::StaticClass());
	ScriptComponent = CreateDefaultSubobject<ULuaScriptComponent>("PacmanGameModeScript");
	ScriptComponent->SetScriptPath("Assets/Scripts/PacmanGameMode.lua");
}

void APacmanGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* PlayWorld = GetWorld())
		if (UGameInstance* GameInstance = PlayWorld->GetGameInstance())
			if (!GameInstance->IsRunTimerRunning()) GameInstance->StartRunTimer();
	if (ScriptComponent)
	{
		sol::table Script = ScriptComponent->GetScriptInstance();
		if (Script.valid()) Script["Mode"] = this;
	}
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
	{
		HUD->SetText("Coins", "0 / " + std::to_string(MaxCoinCount));
		if (APlayerPawn* Player = Cast<APlayerPawn>(World->GetPlayerPawn()))
			HUD->SetText("Life", std::to_string(Player->GetLife()));
	}
	HTR_LOG(Info, "PacmanGameMode: spawned {} coins", MaxCoinCount);
}

int32 APacmanGameMode::GetPlayerLife() const
{
	UWorld* World = GetWorld();
	APlayerPawn* Player = World ? Cast<APlayerPawn>(World->GetPlayerPawn()) : nullptr;
	return Player ? Player->GetLife() : 0;
}

void APacmanGameMode::ShowGameResult(bool bCleared)
{
	if (bResultShown) return;
	UWorld* World = GetWorld();
	AGameHUD* HUD = World ? World->GetHUD() : nullptr;
	if (!HUD) return;
	FadeOverlayHandle = HUD->AddOverlay("Assets/UI/ResultFade.ui.json", true);
	FadeImage = Cast<UImageWidget>(HUD->FindWidget("ResultFade"));
	if (!FadeOverlayHandle || !FadeImage)
	{
		if (FadeOverlayHandle) HUD->RemoveOverlay(FadeOverlayHandle);
		FadeOverlayHandle = 0;
		FadeImage = nullptr;
		return;
	}
	bPendingClear = bCleared;
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		const double Seconds = GameInstance->StopRunTimer();
		if (bCleared) ClearSeconds = Seconds;
	}
	bResultShown = true;
	if (APlayerController* Controller = World->GetPlayerController())
		Controller->ClearInputState();
	if (UGameInstance* Instance = World->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager())
			Sound->PlaySFX(bCleared ? "Assets/Sounds/GameClear.wav" : "Assets/Sounds/GameOver.wav", 0.7f);
	FadeElapsed = 0.0f;
	FadePhase = EResultFadePhase::FadeOut;
}

void APacmanGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!FadeImage || (FadePhase != EResultFadePhase::FadeOut && FadePhase != EResultFadePhase::FadeIn)) return;
	UWorld* World = GetWorld();
	AGameHUD* HUD = World ? World->GetHUD() : nullptr;
	FGameViewportClient* Viewport = HUD ? HUD->GetGameViewport() : nullptr;
	if (!Viewport) return;
	FadeImage->SetSize({static_cast<float>(Viewport->GetWidth()), static_cast<float>(Viewport->GetHeight())});
	FadeElapsed += std::max(0.0f, DeltaTime);
	const float Duration = FadePhase == EResultFadePhase::FadeOut ? 0.4f : 0.5f;
	const float T = std::clamp(FadeElapsed / Duration, 0.0f, 1.0f);
	const float SmoothT = T * T * (3.0f - 2.0f * T);
	FadeImage->SetTint({0.0f, 0.0f, 0.0f, FadePhase == EResultFadePhase::FadeOut ? SmoothT : 1.0f - SmoothT});
	if (T < 1.0f) return;

	if (FadePhase == EResultFadePhase::FadeOut)
	{
		const FString Path = bPendingClear ? "Assets/UI/GameClear.ui.json" : "Assets/UI/GameOver.ui.json";
		if (!HUD->ShowWidget(Path))
		{
			HUD->RemoveOverlay(FadeOverlayHandle);
			FadeOverlayHandle = 0;
			FadeImage = nullptr;
			FadePhase = EResultFadePhase::None;
			bResultShown = false;
			return;
		}
		FadePhase = EResultFadePhase::FadeIn;
		FadeElapsed = 0.0f;
	}
	else
	{
		HUD->RemoveOverlay(FadeOverlayHandle);
		FadeOverlayHandle = 0;
		FadeImage = nullptr;
		FadePhase = EResultFadePhase::Complete;
	}
}
