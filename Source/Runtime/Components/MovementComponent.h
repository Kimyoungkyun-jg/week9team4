#pragma once
#include "ActorComponent.h"
#include "SceneComponent.h"
#include "Lua/LuaMacros.h"
#include "Collision/HitResult.h"

class UMovementComponent : public UActorComponent
{
	DECLARE_CLASS(UMovementComponent, UActorComponent)
	REFLECT_START(ClassName)
		PROPERTY(UpdatedComponentName)
		REFLECT_END()

	LUA_BIND_START(UMovementComponent)
		LUA_PROP(Velocity)
		LUA_PROP(Gravity)
		LUA_FUNC(StopMovementImmediately)
		LUA_FUNC(HasValidUpdatedComponent)
		UserType["MoveUpdatedComponent"] = [](UMovementComponent& Self, const FVector& Delta, const FRotator& NewRotation, bool bSweep, sol::optional<FHitResult*> InHit) -> bool {
			return Self.MoveUpdatedComponent(Delta, NewRotation, bSweep, InHit.value_or(nullptr));
		};
	LUA_BIND_END()

protected:
	USceneComponent* UpdatedComponent = nullptr;
	FString UpdatedComponentName;

public:
	FVector Velocity = FVector::ZeroVector;
	
	float Gravity = 9.81f;

	// ActorComponent  Interface
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	virtual void SetUpdatedComponent(USceneComponent* NewComponent);
	bool HasValidUpdatedComponent() const;
	virtual void UpdateComponentVelocity();

	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector);
	virtual FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& HIt) const;

	virtual bool MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit);
	virtual void StopMovementImmediately();

protected:
	bool bConstrainToPlane = false;
	FVector PlaneConstraintNormal = FVector::ZeroVector;
	FVector PlaneConstraintOrigin = FVector::ZeroVector;

	virtual FVector ConstrainDirectionToPlane(FVector Direction) const;
};
