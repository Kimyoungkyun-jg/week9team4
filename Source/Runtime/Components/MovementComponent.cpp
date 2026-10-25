#include "EnginePCH.h"
#include "MovementComponent.h"
#include "GameFramework/Actor.h"

void UMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (AActor* MyActor = GetOwner())
	{
		if (USceneComponent* NewUpdatedCompoent = UpdatedComponentName.empty() ? MyActor->GetRootComponent() : Cast<USceneComponent>(MyActor->FindComponentByName(FName(UpdatedComponentName))))
		{
			SetUpdatedComponent(NewUpdatedCompoent);
		}
	}

	PrimaryComponentTick.bCanEverTick = true;
}

void UMovementComponent::BeginPlay()
{
	Super::BeginPlay();
}
  
void UMovementComponent::TickComponent(float DeltaTime)
{
	if (!IsValid(this) || IsBeingDestroyed()) return;
	Super::TickComponent(DeltaTime);
	if (!HasValidUpdatedComponent()) SetUpdatedComponent(nullptr);
}

void UMovementComponent::SetUpdatedComponent(USceneComponent* NewComponent)
{
    if (NewComponent && (!IsValid(NewComponent) || NewComponent->IsBeingDestroyed() || NewComponent->GetOwner() != GetOwner())) return;
    UpdatedComponent = NewComponent;
    UpdatedComponentName = NewComponent ? NewComponent->GetName() : "";
}

bool UMovementComponent::HasValidUpdatedComponent() const
{
    return IsValid(this) && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
        UpdatedComponent && GetOwner()->GetComponents().Find(UpdatedComponent) != INDEX_NONE &&
        IsValid(UpdatedComponent) && !UpdatedComponent->IsBeingDestroyed();
}

void UMovementComponent::UpdateComponentVelocity()
{
	if (HasValidUpdatedComponent())
	{
		UpdatedComponent->ComponentVelocity = Velocity;
	}
}

void UMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{

}

FVector UMovementComponent::ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& HIt) const
{
	if (!bConstrainToPlane)
	{
		return FVector::VectorPlaneProject(Delta, Normal) * Time;		
	}
	else
	{
		const FVector ProjectedNormal = ConstrainDirectionToPlane(Normal);
		return FVector::VectorPlaneProject(Delta, ProjectedNormal) * Time;
	}
}

bool UMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	if (HasValidUpdatedComponent())
	{
		return UpdatedComponent->MoveComponent(ConstrainDirectionToPlane(Delta), NewRotation, bSweep, Hit);
	}

	if (Hit) *Hit = FHitResult(1.f);
	return false;
}

void UMovementComponent::StopMovementImmediately()
{
	Velocity = FVector::ZeroVector;
	UpdateComponentVelocity();
}

FVector UMovementComponent::ConstrainDirectionToPlane(FVector Direction) const
{
	if (bConstrainToPlane)
	{
		Direction = FVector::VectorPlaneProject(Direction, PlaneConstraintNormal);
	}

	return Direction;
}
