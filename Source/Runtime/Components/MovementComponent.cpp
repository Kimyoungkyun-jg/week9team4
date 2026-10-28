#include "EnginePCH.h"
#include "MovementComponent.h"
#include "PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

// 알림에서 재진입하거나 예외가 발생해도 임시 이동 옵션을 원래 값으로 돌린다.
struct FScopedMoveComponentFlags
{
	FScopedMoveComponentFlags(EMoveComponentFlags& InFlags, EMoveComponentFlags TemporaryFlags)
		: Flags(InFlags), SavedFlags(InFlags) { Flags = TemporaryFlags; }
	~FScopedMoveComponentFlags() { Flags = SavedFlags; }
	FScopedMoveComponentFlags(const FScopedMoveComponentFlags&) = delete;
	FScopedMoveComponentFlags& operator=(const FScopedMoveComponentFlags&) = delete;
	EMoveComponentFlags& Flags;
	EMoveComponentFlags SavedFlags;
};
}

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

FVector UMovementComponent::ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	// 평면에 투영하면 법선의 길이가 바뀐다. VectorPlaneProject는 단위 법선을 요구한다.
	const FVector SlideNormal = ConstrainDirectionToPlane(Normal).GetSafeNormal();
	return FVector::VectorPlaneProject(ConstrainDirectionToPlane(Delta), SlideNormal) * Time;
}

bool UMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	if (HasValidUpdatedComponent())
	{
		return UpdatedComponent->MoveComponent(ConstrainDirectionToPlane(Delta), NewRotation, bSweep, Hit, MoveComponentFlags);
	}

	if (Hit) *Hit = FHitResult(1.f);
	return false;
}

bool UMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult& OutHit)
{
	if (!HasValidUpdatedComponent()) { OutHit = FHitResult(1.f); return false; }
	UWorld::FScopedObjectExecution ObjectScope(GetOwner()->GetWorld());
	USceneComponent* const OriginalComponent = UpdatedComponent;
	bool bMoved;
	{
		FScopedMoveComponentFlags FlagsScope(MoveComponentFlags,
			MoveComponentFlags | MOVECOMP_NeverIgnoreBlockingOverlaps | MOVECOMP_DisableBlockingOverlapDispatch);
		bMoved = MoveUpdatedComponent(Delta, NewRotation, bSweep, &OutHit);
	}
	// 충돌/오버랩 알림이 자신을 파괴하거나 이동 대상을 바꿀 수 있다. 이후 단계를 새 대상에 적용하지 않는다.
	if (!HasValidUpdatedComponent() || UpdatedComponent != OriginalComponent) return bMoved;
	if (OutHit.bStartPenetrating)
	{
		const FVector Adjustment = GetPenetrationAdjustment(OutHit);
		if (!HasValidUpdatedComponent() || UpdatedComponent != OriginalComponent) return bMoved;
		if (ResolvePenetration(Adjustment, OutHit, NewRotation) && HasValidUpdatedComponent() && UpdatedComponent == OriginalComponent)
		{
			// 원래 옵션으로 원래 Delta 전체를 한 번 재시도한다. 벽면 슬라이딩은 호출자가 따로 요청한다.
			bMoved = MoveUpdatedComponent(Delta, NewRotation, bSweep, &OutHit);
		}
	}
	return bMoved;
}

FVector UMovementComponent::GetPenetrationAdjustment(const FHitResult& Hit) const
{
	if (!Hit.bStartPenetrating || !IsFiniteVector(Hit.Normal) || !std::isfinite(Hit.PenetrationDepth)) return FVector::ZeroVector;
	// UE also resolves an initial overlap whose MTD depth is zero.
	const float Depth = Hit.PenetrationDepth > 0.f ? Hit.PenetrationDepth : 0.00125f;
	return ConstrainDirectionToPlane(Hit.Normal.GetSafeNormal() * (Depth + std::abs(PenetrationPullbackDistance)));
}

bool UMovementComponent::ResolvePenetration(const FVector& ProposedAdjustment, const FHitResult& Hit, const FRotator& NewRotation)
{
	if (!HasValidUpdatedComponent() || !Hit.bStartPenetrating) return false;
	UWorld::FScopedObjectExecution ObjectScope(GetOwner()->GetWorld());
	auto* const Primitive = Cast<UPrimitiveComponent>(UpdatedComponent);
	UWorld* const World = Primitive ? Primitive->GetWorld() : nullptr;
	const FVector Adjustment = ConstrainDirectionToPlane(ProposedAdjustment);
	if (!World || !Primitive->IsCollisionEnabled() || !IsFiniteVector(Adjustment) || Adjustment.SizeSquared() == 0.f ||
		!std::isfinite(NewRotation.Pitch) || !std::isfinite(NewRotation.Yaw) || !std::isfinite(NewRotation.Roll)) return false;
	auto CanContinue = [&]() { return HasValidUpdatedComponent() && UpdatedComponent == Primitive; };

	// 목적지에 조금 부풀린 충돌체가 들어갈 수 있는지 확인한다. Overlap 응답은 보정을 막지 않는다.
	FCollisionGeometry Geometry;
	if (!Primitive->GetCollisionGeometry(Geometry) || !CanContinue()) return false;
	const FVector LocalCenter = Primitive->GetWorldRotation().Quaternion().Inverse().RotateVector(Geometry.Center - Primitive->GetWorldLocation());
	Geometry.Rotation = NewRotation.Quaternion();
	Geometry.Center = Primitive->GetWorldLocation() + Adjustment + Geometry.Rotation.RotateVector(LocalCenter);
	const float Inflation = std::abs(PenetrationOverlapCheckInflation);
	if (!std::isfinite(Inflation)) return false;
	Geometry.HalfExtent += FVector(Inflation);
	Geometry.Radius += Inflation;
	Geometry.HalfHeight += Inflation;
	TArray<UPrimitiveComponent*> Overlaps;
	World->OverlapMulti(*Primitive, Geometry, Overlaps);
	if (!CanContinue()) return false;
	bool bEncroached = false;
	for (UPrimitiveComponent* Other : Overlaps)
	{
		if (IsValid(Other) && ResolveCollisionResponse(Primitive->GetCollisionSettings(), Other->GetCollisionSettings()) == ECollisionResponse::Block)
		{ bEncroached = true; break; }
	}
	if (!bEncroached)
	{
		const bool bMoved = MoveUpdatedComponent(Adjustment, NewRotation, false, nullptr);
		return bMoved && CanContinue();
	}

	// 탈출 방향의 초기 침투를 무시할 수 있도록 보정 중에만 NeverIgnore를 해제한다.
	FScopedMoveComponentFlags FlagsScope(MoveComponentFlags, MoveComponentFlags & ~MOVECOMP_NeverIgnoreBlockingOverlaps);
	FHitResult SweepOutHit(1.f);
	bool bMoved = MoveUpdatedComponent(Adjustment, NewRotation, true, &SweepOutHit);
	if (!CanContinue()) return false;
	if (!bMoved && SweepOutHit.bStartPenetrating)
	{
		const FVector SecondAdjustment = GetPenetrationAdjustment(SweepOutHit);
		if (!CanContinue()) return false;
		const FVector CombinedAdjustment = Adjustment + SecondAdjustment;
		if (SecondAdjustment != Adjustment && CombinedAdjustment.SizeSquared() > 0.f)
		{
			bMoved = MoveUpdatedComponent(CombinedAdjustment, NewRotation, true, nullptr);
			if (!CanContinue()) return false;
		}
	}
	if (!bMoved)
	{
		const FVector MoveDelta = ConstrainDirectionToPlane(Hit.TraceEnd - Hit.TraceStart);
		if (MoveDelta.SizeSquared() > 0.f)
		{
			bMoved = MoveUpdatedComponent(Adjustment + MoveDelta, NewRotation, true, nullptr);
			if (!CanContinue()) return false;
			if (!bMoved && MoveDelta.Dot(Adjustment) > 0.f)
			{
				bMoved = MoveUpdatedComponent(MoveDelta, NewRotation, true, nullptr);
				if (!CanContinue()) return false;
			}
		}
	}
	return bMoved;
}

float UMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact)
{
	if (!HasValidUpdatedComponent() || !Hit.bBlockingHit || !std::isfinite(Time) || Time <= 0.f ||
		!IsFiniteVector(Delta) || !IsFiniteVector(Normal)) return 0.f;
	UWorld::FScopedObjectExecution ObjectScope(GetOwner()->GetWorld());
	USceneComponent* const OriginalComponent = UpdatedComponent;
	auto CanContinue = [&]() { return HasValidUpdatedComponent() && UpdatedComponent == OriginalComponent; };
	Time = std::min(Time, 1.f);
	const FVector OldHitNormal = Normal.GetSafeNormal();
	FVector SlideDelta = ComputeSlideVector(Delta, Time, OldHitNormal, Hit);
	if (!CanContinue() || !(SlideDelta.Dot(Delta) > 0.f)) return 0.f;
	const FRotator Rotation = UpdatedComponent->GetWorldRotation();
	SafeMoveUpdatedComponent(SlideDelta, Rotation, true, Hit);
	const float FirstHitPercent = std::clamp(Hit.Time, 0.f, 1.f);
	float PercentApplied = FirstHitPercent;
	if (!CanContinue()) return PercentApplied;
	if (Hit.bBlockingHit && !Hit.bStartPenetrating)
	{
		if (bHandleImpact)
		{
			HandleImpact(Hit, FirstHitPercent * Time, SlideDelta);
			if (!CanContinue()) return PercentApplied;
		}
		TwoWallAdjust(SlideDelta, Hit, OldHitNormal);
		if (!CanContinue()) return PercentApplied;
		if (SlideDelta.SizeSquared() > 1.e-6f && SlideDelta.Dot(Delta) > 0.f)
		{
			SafeMoveUpdatedComponent(SlideDelta, Rotation, true, Hit);
			const float SecondHitPercent = std::clamp(Hit.Time, 0.f, 1.f) * (1.f - FirstHitPercent);
			PercentApplied += SecondHitPercent;
			if (!CanContinue()) return std::clamp(PercentApplied, 0.f, 1.f);
			if (bHandleImpact && Hit.bBlockingHit) HandleImpact(Hit, SecondHitPercent * Time, SlideDelta);
		}
	}
	return std::clamp(PercentApplied, 0.f, 1.f);
}

void UMovementComponent::TwoWallAdjust(FVector& Delta, const FHitResult& Hit, const FVector& OldHitNormal) const
{
	const FVector DesiredDelta = Delta;
	const FVector HitNormal = Hit.Normal.GetSafeNormal();
	if (OldHitNormal.Dot(HitNormal) <= 0.f)
	{
		// 두 면이 만드는 모서리 방향으로 남은 이동을 투영한다. 직각 모서리도 이 경로이다.
		const FVector EdgeDirection = HitNormal.Cross(OldHitNormal).GetSafeNormal();
		Delta = EdgeDirection * Delta.Dot(EdgeDirection) * (1.f - Hit.Time);
		if (DesiredDelta.Dot(Delta) < 0.f) Delta = -Delta;
	}
	else
	{
		Delta = ComputeSlideVector(Delta, 1.f - Hit.Time, HitNormal, Hit);
		if (Delta.Dot(DesiredDelta) <= 0.f) Delta = FVector::ZeroVector;
		else if (std::abs(HitNormal.Dot(OldHitNormal) - 1.f) < KINDA_SMALL_NUMBER) Delta += HitNormal * 0.001f;
	}
	Delta = ConstrainDirectionToPlane(Delta);
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
