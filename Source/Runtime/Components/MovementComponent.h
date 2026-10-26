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
		LUA_FUNC(SafeMoveUpdatedComponent)
		LUA_FUNC(GetPenetrationAdjustment)
		LUA_FUNC(ResolvePenetration)
		UserType["SlideAlongSurface"] = [](UMovementComponent& Self, const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, sol::optional<bool> bHandleImpact) {
			return Self.SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact.value_or(false));
		};
	LUA_BIND_END()

protected:
	USceneComponent* UpdatedComponent = nullptr;
	FString UpdatedComponentName;

public:
	FVector Velocity = FVector::ZeroVector;
	
	float Gravity = 9.81f;
	// MoveUpdatedComponent가 USceneComponent::MoveComponent의 마지막 인수로 전달한다.
	EMoveComponentFlags MoveComponentFlags = MOVECOMP_NoFlags;
	// 프로젝트의 MoveComponent 후퇴 거리(0.001)에 맞춘 여유 거리. UE의 cm 단위 기본값과 다르다.
	float PenetrationPullbackDistance = 0.001f;
	float PenetrationOverlapCheckInflation = 0.0005f;

	// ActorComponent  Interface
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	virtual void SetUpdatedComponent(USceneComponent* NewComponent);
	bool HasValidUpdatedComponent() const;
	virtual void UpdateComponentVelocity();

	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector);
	virtual FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const;
	// Hit을 만든 원래 Delta와 남은 비율(보통 1-Hit.Time)을 받는다. 내부 이동 시 SafeMoveUpdatedComponent를 사용한다.
	// 첫 면을 따라 이동하고, 새 면에 막히면 TwoWallAdjust 후 한 번 더 이동한다(무한 반복하지 않음).
	// 반환값은 요청한 슬라이드(Delta*Time) 중 적용한 비율 0~1이다. Hit은 마지막 이동 결과로 바뀐다.
	// bHandleImpact는 UMovementComponent::HandleImpact 호출만 제어하며 Actor/Component Hit 알림은 별도이다.
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact = false);
	virtual void TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const;

	virtual bool MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit);
	// 초기 검사에 NeverIgnore|DisableBlockingOverlapDispatch를 임시 적용하고, 초기 침투면 보정 후 원래 Delta를 한 번 재시도한다.
	// 반환값/OutHit은 마지막 MoveUpdatedComponent의 결과이며, SlideAlongSurface를 자동 호출하지 않는다.
	virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult& OutHit);
	// 검출된 초기 침투의 법선*(깊이+여유 거리)을 평면 제약에 맞춰 보정 벡터로 만든다.
	virtual FVector GetPenetrationAdjustment(const FHitResult& Hit) const;
	// 보정 목적지의 Block 겹침이 없으면 직접 이동한다. 막히면 스윕/두 보정 벡터 합산/원래 방향을 제한된 횟수로 시도한다.
	// true는 보정 이동이 발생하여 재시도할 수 있다는 뜻이며, 완전한 침투 해소를 보장하지 않는다.
	virtual bool ResolvePenetration(const FVector& Adjustment, const FHitResult& Hit, const FRotator& NewRotation);
	virtual void StopMovementImmediately();

protected:
	bool bConstrainToPlane = false;
	FVector PlaneConstraintNormal = FVector::ZeroVector;
	FVector PlaneConstraintOrigin = FVector::ZeroVector;

	virtual FVector ConstrainDirectionToPlane(FVector Direction) const;
};
