#include "EnginePCH.h"
#include "Camera/HeadBobComponent.h"
#include "GameFramework/Actor.h"
#include <algorithm>
#include <cmath>

void UHeadBobComponent::BeginPlay()
{
	Super::BeginPlay();
	bSuspended = false;
	Reset();
}

void UHeadBobComponent::EndPlay()
{
	Reset();
	Super::EndPlay();
}

void UHeadBobComponent::Reset()
{
	const uint64 NextReset = MovementState.ResetSerial + 1;
	MovementState = {};
	MovementState.ResetSerial = NextReset;
}

void UHeadBobComponent::SetSuspended(bool bValue)
{
	if (bSuspended == bValue) return;
	bSuspended = bValue;
	Reset();
}

void UHeadBobComponent::UpdateBob(float DeltaTime, const FVector& Movement, bool bStep, bool bGrounded)
{
	if (bSuspended) return;
	if (!IsActive() || !bEnabled)
	{
		Reset();
		return;
	}
	if (!std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const float DistanceSquared = Movement.SizeSquared();
	MovementState.bGrounded = bGrounded;
	MovementState.bWalking = bGrounded && std::isfinite(DistanceSquared) && DistanceSquared > 0.00000001f;
	MovementState.bStep = bStep && MovementState.bWalking;
	MovementState.ForwardWeight = 0.0f;
	if (MovementState.bWalking)
	{
		AActor* Owner = GetOwner();
		if (IsValid(Owner) && !Owner->IsActorBeingDestroyed())
		{
			const FVector Forward = Owner->GetActorRotation().Quaternion().GetForwardVector();
			const FVector FlatForward(Forward.X, Forward.Y, 0.0f);
			MovementState.ForwardWeight = std::clamp(std::abs(Movement.GetSafeNormal().Dot(FlatForward.GetSafeNormal())), 0.0f, 1.0f);
		}
	}
	++MovementState.UpdateSerial;
}
