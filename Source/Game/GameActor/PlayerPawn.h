#pragma once

#include "GameFramework/DefaultPawn.h"

class AFlashlightActor;

class APlayerPawn : public ADefaultPawn
{
	DECLARE_CLASS(APlayerPawn, ADefaultPawn)

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }

private:
	AFlashlightActor* FlashlightActor = nullptr;
};
