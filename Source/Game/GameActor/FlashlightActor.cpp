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

	SpotLightComponent = CreateDefaultSubobject<USpotLightComponent>("FlashlightSpotLight");
	SpotLightComponent->SetupAttachment(MeshComponent);
	SpotLightComponent->SetRelativeLocation({1.0f, 0.0f, 0.0f});
	SpotLightComponent->SetInnerConeAngle(15.0f);
	SpotLightComponent->SetOuterConeAngle(30.0f);
	SpotLightComponent->SetAttenuationRadius(25.0f);
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
	if (DeltaTime > 0.0f && Gauge > 0.0f)
		SetGauge(Gauge - std::max(0.0f, DrainRatePerSecond) * DeltaTime);
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
