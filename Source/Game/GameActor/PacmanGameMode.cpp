#include "EnginePCH.h"
#include "PacmanGameMode.h"

#include "CoinActor.h"
#include "GhostActor.h"
#include "PlayerPawn.h"
#include "PacmanHUD.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/GameHUD.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LuaScriptComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "UI/Widget.h"
#include <algorithm>
#include <cmath>
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
	if (APlayerPawn* Player = Cast<APlayerPawn>(World->GetPlayerPawn())) LifeCount = Player->GetLife();

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
	bool bHasGhost = false;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if (Cast<AGhostActor>(Actor)) { bHasGhost = true; break; }
	if (!bHasGhost && !FloorTiles.IsEmpty())
	{
		APawn* Player = World->GetPlayerPawn();
		if (!Player) return;
		int32 GhostTile = INDEX_NONE;
		int32 Farthest = -1;
		for (int32 Index = 0; Index < FloorTiles.Num(); ++Index)
		{
			const FCoinTile& Tile = FloorTiles[Index];
			if (Tile.bSelected) continue;
			const int32 DX = Tile.Column - StartColumn, DY = Tile.Row - StartRow;
			const int32 Distance = DX * DX + DY * DY;
			if (Distance > Farthest) { Farthest = Distance; GhostTile = Index; }
		}
		if (GhostTile == INDEX_NONE) GhostTile = FloorTiles.Num() - 1;
		const FVector Location = FloorTiles[GhostTile].Center + FVector(0.0f, 0.0f, 0.25f);
		const FVector ToPlayer = Player->GetActorLocation() - Location;
		const float Yaw = std::atan2(ToPlayer.Y, ToPlayer.X) * 180.0f / PI;
		const FTransform SpawnTransform(FRotator(0.0f, Yaw, 0.0f), Location);
		World->SpawnActor(AGhostActor::StaticClass(), NAME_None, &SpawnTransform);
	}

	if (AGameHUD* HUD = World->GetHUD())
	{
		HUD->SetText("Coins", "0 / " + std::to_string(MaxCoinCount));
		if (APlayerPawn* Player = Cast<APlayerPawn>(World->GetPlayerPawn()))
			HUD->SetText("Life", std::to_string(LifeCount));
	}
	HTR_LOG(Info, "PacmanGameMode: spawned {} coins", MaxCoinCount);
}

int32 APacmanGameMode::GetPlayerLife() const
{
	return LifeCount;
}

void APacmanGameMode::StartGhostHit(AGhostActor* Ghost, APlayerPawn* Player)
{
	UWorld* World = GetWorld();
	if (!World || !Ghost || !Player || Player != World->GetPlayerPawn() ||
		bResultShown || bGhostHitPlaying || GhostHitCooldown > 0.0f || LifeCount <= 0) return;
	AGameHUD* HUD = World->GetHUD();
	UCameraComponent* Camera = Player->GetCameraComponent();
	if (!HUD || !Camera || !Ghost->GetEyesMesh()) return;
	const uint64 Overlay = HUD->AddOverlay("Assets/UI/GhostHit.ui.json", true);
	UImageWidget* Image = Cast<UImageWidget>(HUD->FindWidget("GhostHitFlash"));
	if (!Overlay || !Image)
	{
		if (Overlay) HUD->RemoveOverlay(Overlay);
		return;
	}
	if (FGameViewportClient* Viewport = HUD->GetGameViewport())
		Image->SetSize({static_cast<float>(Viewport->GetWidth()), static_cast<float>(Viewport->GetHeight())});
	HitOverlayHandle = Overlay;
	HitImage = Image;
	HitGhost = Ghost;
	HitPlayer = Player;
	HitCamera = Camera;
	Player->SetHeadBobSuspended(true);
	SavedCameraLocation = Camera->GetRelativeLocation();
	SavedCameraRotation = Camera->GetRelativeRotation();
	SavedCameraFov = Camera->GetFieldOfView();
	SavedGhostRotation = Ghost->GetActorRotation();
	bGhostHitPlaying = true;
	if (auto* Movement = Player->GetCharacterMovement()) Movement->SetMovementEnabled(false);
	Ghost->SetHitFrozen(true);
	const FVector Offset = Player->GetActorLocation() - Ghost->GetActorLocation();
	const FVector ToPlayer(Offset.X, Offset.Y, 0.0f);
	if (ToPlayer.SizeSquared() > SMALL_NUMBER)
		Ghost->SetActorRotation(FRotator(0.0f, std::atan2(ToPlayer.Y, ToPlayer.X) * 180.0f / PI, 0.0f), false);
	const FBox Bounds = Ghost->GetEyesMesh()->CalcBounds();
	const FVector FaceCenter = (Bounds.Min + Bounds.Max) * 0.5f;
	const FQuat FaceRotation = Ghost->GetActorRotation().Quaternion();
	const FVector Forward = FaceRotation.GetForwardVector().GetSafeNormal();
	const FVector Right = FaceRotation.GetRightVector().GetSafeNormal();
	const FVector EyeExtent = (Bounds.Max - Bounds.Min) * 0.5f;
	const float FrontDepth = std::abs(Forward.X) * EyeExtent.X + std::abs(Forward.Y) * EyeExtent.Y;
	const float HalfWidth = std::abs(Right.X) * EyeExtent.X + std::abs(Right.Y) * EyeExtent.Y;
	const float CameraGap = std::max(0.45f, HalfWidth / 1.3f);
	// 플레이어 본체와 부착된 손전등을 함께 유령 앞으로 이동
	const FVector TargetLocation = FaceCenter + Forward * (FrontDepth + CameraGap);
	const FRotator TargetRotation(0.0f, std::atan2(-Forward.Y, -Forward.X) * 180.0f / PI, 0.0f);
	Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.0f, 0.0f, 0.0f));
	Player->SetActorLocation(TargetLocation, false);
	Player->SetActorRotation(TargetRotation, false);
	Camera->SetFieldOfView(85.0f);
	HitLight = World->SpawnActor<APointLightActor>();
	if (HitLight)
	{
		HitLight->SetActorLocation(TargetLocation - Forward * (CameraGap * 0.25f));
		HitLight->SetActorEnableCollision(false);
		if (UPointLightComponent* Light = HitLight->GetPointLightComponent())
		{
			Light->SetIntensity(30.0f);
			Light->SetAttenuationRadius(4.0f);
			Light->SetCastShadows(false);
		}
	}
	Ghost->ChangeEye();
	if (APlayerController* Controller = World->GetPlayerController()) Controller->ClearInputState();
	GhostHitPhase = EGhostHitPhase::CloseUp;
	GhostHitElapsed = 0.0f;
	if (UGameInstance* Instance = World->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager())
			Sound->PlaySFX("Assets/Sounds/Damaged.wav");
}

void APacmanGameMode::ResolveGhostHit()
{
	if (HitLight) { HitLight->Destroy(); HitLight = nullptr; }
	if (HitGhost)
	{
		HitGhost->RestoreEye();
		HitGhost->SetActorRotation(SavedGhostRotation, false);
		HitGhost->ResetToHome();
	}
	if (HitCamera)
	{
		HitCamera->SetRelativeLocationAndRotation(SavedCameraLocation, SavedCameraRotation);
		HitCamera->SetFieldOfView(SavedCameraFov);
	}
	LifeCount = std::max(0, LifeCount - 1);
	if (HitPlayer) HitPlayer->SetLife(LifeCount);
	if (UWorld* World = GetWorld())
		if (APlayerController* Controller = World->GetPlayerController()) Controller->ClearInputState();
	if (LifeCount > 0)
	{
		if (HitPlayer) HitPlayer->RespawnAtPlayerStart();
		GhostHitPhase = EGhostHitPhase::FadeFromBlack;
		GhostHitElapsed = 0.0f;
	}
	else
	{
		UWorld* World = GetWorld();
		AGameHUD* HUD = World ? World->GetHUD() : nullptr;
		if (HUD && HUD->ShowWidget("Assets/UI/GameOver.ui.json"))
		{
			CommitGameResult(false);
			GhostHitPhase = EGhostHitPhase::FadeFromBlack;
			GhostHitElapsed = 0.0f;
		}
		else
		{
			if (HitGhost) HitGhost->SetHitFrozen(false);
			HitGhost = nullptr;
			if (HUD) HUD->RemoveOverlay(HitOverlayHandle);
			HitOverlayHandle = 0;
			HitImage = nullptr;
			GhostHitPhase = EGhostHitPhase::None;
			bGhostHitPlaying = false;
			ReleaseGhostHitCamera();
		}
	}
	
	if (UWorld* World = GetWorld())
	{
		if (ULevel* Level = World->GetPersistentLevel())
		{
			for (AActor* Actor : Level->GetActors())
			{
				if (AGhostActor* Ghost = Cast<AGhostActor>(Actor))
				{
					if (Ghost != HitGhost)
					{
						Ghost->ResetToHome();
					}
				}
			}
		}
	}
}

void APacmanGameMode::ReleaseGhostHitCamera()
{
	if (IsValid(HitPlayer)) HitPlayer->SetHeadBobSuspended(false);
	HitPlayer = nullptr;
	HitCamera = nullptr;
}

void APacmanGameMode::TickGhostHit(float DeltaTime)
{
	if (GhostHitPhase == EGhostHitPhase::None || !HitImage) return;
	UWorld* World = GetWorld();
	AGameHUD* HUD = World ? World->GetHUD() : nullptr;
	FGameViewportClient* Viewport = HUD ? HUD->GetGameViewport() : nullptr;
	if (Viewport) HitImage->SetSize({static_cast<float>(Viewport->GetWidth()), static_cast<float>(Viewport->GetHeight())});
	GhostHitElapsed += std::max(0.0f, DeltaTime);
	switch (GhostHitPhase)
	{
	case EGhostHitPhase::CloseUp:
		HitImage->SetTint({0.8f, 0.0f, 0.0f, 0.05f + 0.07f * std::abs(std::sin(GhostHitElapsed * 11.0f))});
		if (GhostHitElapsed >= 0.55f) { GhostHitPhase = EGhostHitPhase::FadeToBlack; GhostHitElapsed = 0.0f; }
		break;
	case EGhostHitPhase::FadeToBlack:
		HitImage->SetTint({0.0f, 0.0f, 0.0f, std::clamp(GhostHitElapsed / 0.22f, 0.0f, 1.0f)});
		if (GhostHitElapsed >= 0.22f) ResolveGhostHit();
		break;
	case EGhostHitPhase::FadeFromBlack:
		HitImage->SetTint({0.0f, 0.0f, 0.0f, 1.0f - std::clamp(GhostHitElapsed / 0.35f, 0.0f, 1.0f)});
		if (GhostHitElapsed >= 0.35f)
		{
			if (HitGhost && LifeCount > 0) HitGhost->SetHitFrozen(false);
			HitGhost = nullptr;
			if (HUD) HUD->RemoveOverlay(HitOverlayHandle);
			HitOverlayHandle = 0;
			HitImage = nullptr;
			GhostHitPhase = EGhostHitPhase::None;
			bGhostHitPlaying = false;
			ReleaseGhostHitCamera();
			GhostHitCooldown = 1.0f;
		}
		break;
	default: break;
	}
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
	if (FGameViewportClient* Viewport = HUD->GetGameViewport())
		FadeImage->SetSize({static_cast<float>(Viewport->GetWidth()), static_cast<float>(Viewport->GetHeight())});
	bPendingClear = bCleared;
	CommitGameResult(bCleared);
	FadeElapsed = 0.0f;
	FadePhase = EResultFadePhase::FadeOut;
}

void APacmanGameMode::CommitGameResult(bool bCleared)
{
	if (bResultShown) return;
	UWorld* World = GetWorld();
	if (!World) return;
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
}

void APacmanGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	GhostHitCooldown = std::max(0.0f, GhostHitCooldown - DeltaTime);
	TickGhostHit(DeltaTime);
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
			if (HitGhost) HitGhost->SetHitFrozen(false);
			HitGhost = nullptr;
			if (HitOverlayHandle) HUD->RemoveOverlay(HitOverlayHandle);
			HitOverlayHandle = 0;
			HitImage = nullptr;
			GhostHitPhase = EGhostHitPhase::None;
			bGhostHitPlaying = false;
			ReleaseGhostHitCamera();
			HUD->RemoveOverlay(FadeOverlayHandle);
			FadeOverlayHandle = 0;
			FadeImage = nullptr;
			FadePhase = EResultFadePhase::None;
			bResultShown = false;
			return;
		}
		if (HitOverlayHandle)
		{
			HUD->RemoveOverlay(HitOverlayHandle);
			HitOverlayHandle = 0;
			HitImage = nullptr;
			GhostHitPhase = EGhostHitPhase::None;
			bGhostHitPlaying = false;
			ReleaseGhostHitCamera();
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
