#pragma once

#include "GameFramework/Actor.h"

class UStaticMeshComponent;
class USpotLightComponent;

class AFlashlightActor : public AActor
{
	DECLARE_CLASS(AFlashlightActor, AActor)
	REFLECT_START(AFlashlightActor)
		PROPERTY(Gauge)
		PROPERTY(DrainRatePerSecond)
		PROPERTY(MaxLightIntensity)
	REFLECT_END()

public:
	AFlashlightActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostEditChangeProperty(const FProperty& Property) override;

	void SetGauge(float NewGauge);
	void AddGauge(float Amount);
	float GetGauge() const { return Gauge; }
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }
	USpotLightComponent* GetSpotLightComponent() const { return SpotLightComponent; }

private:
	static constexpr float MaxGauge = 100.0f;
	void UpdateLightIntensity();

	UStaticMeshComponent* MeshComponent = nullptr;
	USpotLightComponent* SpotLightComponent = nullptr;
	float Gauge = MaxGauge;
	float DrainRatePerSecond = 1.0f;
	float MaxLightIntensity = 30.0f;
};
