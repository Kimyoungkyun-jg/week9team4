#pragma once

#include "GameFramework/Actor.h"

class USphereComponent;
class URotatingMovementComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;

class ACoinActor : public AActor
{
	DECLARE_CLASS(ACoinActor, AActor)
	REFLECT_START(ACoinActor)
		PROPERTY(BobbingSpeed)
		PROPERTY(BobbingHeight)
	REFLECT_END()

public:
	ACoinActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, UPrimitiveComponent* OtherComponent);

	USphereComponent* GetSphereComponent() const { return SphereComponent; }
	URotatingMovementComponent* GetRotatingMovementComponent() const { return RotatingMovementComponent; }
	UStaticMeshComponent* GetStaticMeshComponent() const { return MeshComponent; }

private:
	USphereComponent* SphereComponent = nullptr;
	URotatingMovementComponent* RotatingMovementComponent = nullptr;
	UStaticMeshComponent* MeshComponent = nullptr;

	FVector InitialLocation = FVector::ZeroVector;
	float RunningTime = 0.0f;
	float BobbingSpeed = 2.0f;
	float BobbingHeight = 0.25f;
	bool bCollected = false;
};
