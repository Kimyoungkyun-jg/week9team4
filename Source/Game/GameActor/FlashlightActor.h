#pragma once

#include "GameFramework/Actor.h"

class UStaticMeshComponent;
class USpotLightComponent;
class UCapsuleComponent;

class AFlashlightActor : public AActor
{
	DECLARE_CLASS(AFlashlightActor, AActor)
	REFLECT_START(AFlashlightActor)
		PROPERTY(Gauge)
		PROPERTY(DrainRatePerSecond)
		PROPERTY(MaxLightIntensity)
	REFLECT_END()

public:
	static constexpr int32 AttackTrailCount = 2;
	AFlashlightActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostEditChangeProperty(const FProperty& Property) override;

	void SetGauge(float NewGauge);
	void AddGauge(float Amount);
	float GetGauge() const { return Gauge; }
	float GetGaugeFraction() const { return Gauge / MaxGauge; }
	void SetDrainRatePerSecond(float InRate);
	bool TryConsumeGauge(float Amount);
	void UpdateAttackBeam(float MaxLength, float Radius, const AActor* IgnoreActor);
	void UpdateAttackTrail(int32 Index, const FTransform& CameraPose, float MaxLength, float Radius, const AActor* IgnoreActor);
	void SetAttackTrailsEnabled(bool bEnabled);
	void SetAttackPresentation(float BrightnessMultiplier, bool bShowAttackBeam);
	UStaticMeshComponent* GetAttackBeamMesh() const { return AttackBeamBody; }
	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }
	USpotLightComponent* GetSpotLightComponent() const { return SpotLightComponent; }
	UCapsuleComponent* GetAttackCapsule() const { return AttackCapsule; }

private:
	static constexpr float MaxGauge = 100.0f;
	void UpdateLightIntensity();
	void UpdateAttackBeamMeshes(float Length, float Radius);

	UStaticMeshComponent* MeshComponent = nullptr;
	UStaticMeshComponent* AttackBeamBody = nullptr;
	UStaticMeshComponent* AttackBeamFront = nullptr;
	UStaticMeshComponent* AttackBeamBack = nullptr;
	float AttackBeamLength = 0.0f;
	float AttackBeamRadius = 0.0f;
	float AttackBrightnessMultiplier = 1.0f;
	USpotLightComponent* SpotLightComponent = nullptr;
	UCapsuleComponent* AttackCapsule = nullptr;
	struct FAttackTrail
	{
		USceneComponent* Root = nullptr;
		UStaticMeshComponent* Body = nullptr;
		UStaticMeshComponent* Front = nullptr;
		UStaticMeshComponent* Back = nullptr;
	};
	FAttackTrail AttackTrails[AttackTrailCount];
	bool bAttackTrailsEnabled = false;
	float Gauge = MaxGauge;
	float DrainRatePerSecond = 10.0f;
	float MaxLightIntensity = 70.0f;
};
