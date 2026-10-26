#include "EnginePCH.h"
#include "FlashlightActor.h"

#include "Asset/AssetManager.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"

AFlashlightActor::AFlashlightActor()
{
	PrimaryActorTick.bCanEverTick = true;
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FlashlightMesh");
	MeshComponent->SetupAttachment(GetRootComponent());
	MeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Flashlight/Flashlight.obj"));
	MeshComponent->SetRelativeScale3D({0.2f, 0.2f, 0.2f});
	MeshComponent->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	MeshComponent->SetCastShadow(false);

	SpotLightComponent = CreateDefaultSubobject<USpotLightComponent>("FlashlightSpotLight");
	SpotLightComponent->SetupAttachment(GetRootComponent());
	SpotLightComponent->SetRelativeLocation({0.2f, 0.0f, 0.0f});
	SpotLightComponent->SetInnerConeAngle(20.0f);
	SpotLightComponent->SetOuterConeAngle(35.0f);
	SpotLightComponent->SetAttenuationRadius(15.0f);
	UpdateLightIntensity();
}

void AFlashlightActor::BeginPlay()
{
	Super::BeginPlay();
	SetGauge(Gauge);
}

void AFlashlightActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	//if (DeltaTime > 0.0f && Gauge > 0.0f)
	//	SetGauge(Gauge - std::max(0.0f, DrainRatePerSecond) * DeltaTime);
}

void AFlashlightActor::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	DrainRatePerSecond = std::max(0.0f, DrainRatePerSecond);
	MaxLightIntensity = std::max(0.0f, MaxLightIntensity);
	SetGauge(Gauge);
}

void AFlashlightActor::SetGauge(float NewGauge)
{
	Gauge = FMath::Clamp(NewGauge, 0.0f, MaxGauge);
	UpdateLightIntensity();
}

void AFlashlightActor::AddGauge(float Amount)
{
	if (Amount > 0.0f) SetGauge(Gauge + Amount);
}

void AFlashlightActor::UpdateLightIntensity()
{
	if (SpotLightComponent)
		SpotLightComponent->SetIntensity(MaxLightIntensity * (Gauge / MaxGauge));
}
