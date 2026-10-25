#include "EnginePCH.h"
#include "CoinActor.h"

#include "Components/SphereComponent.h"
#include "Components/RotatingMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include <cmath>

ACoinActor::ACoinActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SphereComponent = CreateDefaultSubobject<USphereComponent>("SphereComponent");
	SetRootComponent(SphereComponent);
	SphereComponent->SetSphereRadius(0.5f);
	SphereComponent->SetRelativeScale3D(FVector(5.0f, 5.0f, 5.0f));

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("MeshComponent");
	MeshComponent->SetupAttachment(SphereComponent);
	MeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Coin/Coin.obj"));
	MeshComponent->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));

	RotatingMovementComponent = CreateDefaultSubobject<URotatingMovementComponent>("RotatingMovementComponent");
	RotatingMovementComponent->RotationRate = FRotator(0.0f, 90.0f, 0.0f);
	RotatingMovementComponent->bRotationInLocalSpace = true;
}

void ACoinActor::BeginPlay()
{
	Super::BeginPlay();
	InitialLocation = GetActorLocation();
}

void ACoinActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RunningTime += DeltaTime;
	float DeltaZ = std::sinf(RunningTime * BobbingSpeed) * BobbingHeight;
	SetActorLocation(FVector(InitialLocation.X, InitialLocation.Y, InitialLocation.Z + DeltaZ));
}
