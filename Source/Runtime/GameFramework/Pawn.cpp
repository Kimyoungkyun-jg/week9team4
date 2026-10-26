#include "EnginePCH.h"
#include "GameFramework/Pawn.h"

APawn::APawn()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APawn::AddMovementInput(const FVector& Direction, float ScaleValue)
{
	if (std::isfinite(ScaleValue) && std::isfinite(Direction.X) && std::isfinite(Direction.Y) && std::isfinite(Direction.Z))
		PendingMovementInput += Direction.GetSafeNormal() * ScaleValue;
}

FVector APawn::ConsumeMovementInputVector()
{
	const FVector Input = PendingMovementInput;
	PendingMovementInput = FVector::ZeroVector;
	return Input;
}
