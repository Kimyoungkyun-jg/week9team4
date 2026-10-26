#include "EnginePCH.h"
#include "CoinActor.h"

#include "Components/SphereComponent.h"
#include "Components/RotatingMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "PlayerPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameHUD.h"
#include "GameFramework/GameMode.h"
#include "PacmanGameMode.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include <cmath>
#include <string>

ACoinActor::ACoinActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SphereComponent = CreateDefaultSubobject<USphereComponent>("SphereComponent");
	SetRootComponent(SphereComponent);
	SphereComponent->SetSphereRadius(0.5f);
	SphereComponent->SetCollisionEnabled(true);
	SphereComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	SphereComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
	SphereComponent->SetGenerateOverlapEvents(true);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("MeshComponent");
	MeshComponent->SetupAttachment(SphereComponent);
	MeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Coin/Coin.obj"));
	MeshComponent->SetRelativeScale3D(FVector(5.0f, 5.0f, 5.0f));
	MeshComponent->SetCollisionEnabled(false);

	RotatingMovementComponent = CreateDefaultSubobject<URotatingMovementComponent>("RotatingMovementComponent");
	RotatingMovementComponent->RotationRate = FRotator(0.0f, 90.0f, 0.0f);
	RotatingMovementComponent->bRotationInLocalSpace = true;

}

void ACoinActor::BeginPlay()
{
	Super::BeginPlay();
	InitialLocation = GetActorLocation();

	SetActorEnableCollision(true);
	if (SphereComponent)
	{
		SphereComponent->SetCollisionEnabled(true);
		SphereComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
		SphereComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
		SphereComponent->SetGenerateOverlapEvents(true);
		SphereComponent->OnComponentBeginOverlap.AddRaw(this, &ACoinActor::OnOverlapBegin);
		SphereComponent->UpdateOverlaps();
	}
}

void ACoinActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RunningTime += DeltaTime;
	float DeltaZ = std::sinf(RunningTime * BobbingSpeed) * BobbingHeight;
	SetActorLocation(FVector(InitialLocation.X, InitialLocation.Y, InitialLocation.Z + DeltaZ));
}

void ACoinActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, UPrimitiveComponent* OtherComponent)
{
	if (bCollected || !OtherComponent) return;

	AActor* OtherActor = OtherComponent->GetOwner();
	if (!OtherActor) return;

	APlayerPawn* PlayerPawn = Cast<APlayerPawn>(OtherActor);
	if (!PlayerPawn) return;

	APlayerController* Controller = PlayerPawn->GetController();
	if (!Controller) return;
	bCollected = true;

	AGameHUD* HUD = Controller->GetHUD();
	UWorld* World = GetWorld();
	if (World)
		if (UGameInstance* Instance = World->GetGameInstance())
			if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX("Assets/Sounds/coin.wav");
	AGameMode* GameMode = World ? Cast<AGameMode>(World->GetGameMode()) : nullptr;

	int32 TotalCoins = 0;
	if (GameMode)
	{
		TotalCoins = GameMode->AddCoin(1);
	}

	if (HUD)
	{
		if (APacmanGameMode* PacmanMode = Cast<APacmanGameMode>(GameMode))
			HUD->SetText("Coins", std::to_string(TotalCoins) + " / " + std::to_string(PacmanMode->GetMaxCoinCount()));
		else
			HUD->SetText("Coins", std::to_string(TotalCoins));
	}

	Destroy();
}
