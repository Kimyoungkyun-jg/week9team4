#pragma once

#include "GameFramework/DefaultPawn.h"

class AFlashlightActor;
class USphereComponent;

class APlayerPawn : public ADefaultPawn
{
	DECLARE_CLASS(APlayerPawn, ADefaultPawn)

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	USphereComponent* GetCollisionComponent() const { return CollisionComponent; }

private:
	AFlashlightActor* FlashlightActor = nullptr;
	USphereComponent* CollisionComponent = nullptr;
};
