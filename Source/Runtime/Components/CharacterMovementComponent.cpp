#include "EnginePCH.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
bool Finite(const FVector& V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); }
FVector Approach(const FVector& From, const FVector& To, float MaxDelta)
{
	const FVector Difference = To - From;
	const float Size = Difference.Size();
	return Size > MaxDelta && Size > SMALL_NUMBER ? From + Difference * (MaxDelta / Size) : To;
}
}

UCharacterMovementComponent::UCharacterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::DuringPhysics;
}

bool UCharacterMovementComponent::CanContinue() const
{
	return (!bSimulating || !bInterrupted) && bMovementEnabled && HasValidUpdatedComponent() &&
		(!bSimulating || UpdatedComponent == SimulationComponent);
}

bool UCharacterMovementComponent::CanJump() const
{
	return IsJumpAllowed() && bMovementEnabled && HasValidUpdatedComponent() && IsMovingOnGround();
}

void UCharacterMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	if (bSimulating) bInterrupted = true;
	if (auto* Pawn = Cast<APawn>(GetOwner())) Pawn->ConsumeMovementInputVector();
	bJumpRequested = false;
	LastGroundedDisplacement = FVector::ZeroVector;
}

void UCharacterMovementComponent::SetMovementEnabled(bool bEnabled)
{
	bMovementEnabled = bEnabled;
	if (!bEnabled) { StopMovementImmediately(); MovementMode = ECharacterMovementMode::None; }
	else if (MovementMode == ECharacterMovementMode::None) MovementMode = ECharacterMovementMode::Falling;
}

void UCharacterMovementComponent::ResetAfterTeleport()
{
	StopMovementImmediately();
	CurrentFloor = {};
	MovementMode = bMovementEnabled ? ECharacterMovementMode::Falling : ECharacterMovementMode::None;
	if (bSimulating) bInterrupted = true;
}

FVector UCharacterMovementComponent::SurfaceNormal(const FHitResult& Hit) const
{
	FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	// This kernel reports the separating normal at rounded capsule/box edges.
	// For floor support use the actual upward box face touched by ImpactPoint.
	FCollisionGeometry Geometry;
	if (IsValid(Hit.HitComponent) && Hit.HitComponent->GetCollisionGeometry(Geometry) && Geometry.Type == ECollisionShape::Box)
	{
		const FVector Local = Geometry.Rotation.Inverse().RotateVector(Hit.ImpactPoint - Geometry.Center);
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(std::abs(Local[Axis]) - Geometry.HalfExtent[Axis]) > 0.002f) continue;
			FVector Face = FVector::ZeroVector; Face[Axis] = Local[Axis] >= 0.f ? 1.f : -1.f;
			Face = Geometry.Rotation.RotateVector(Face);
			if (Face.Z > Normal.Z && Face.Dot(Hit.ImpactNormal) > 0.05f) Normal = Face;
		}
	}
	return Normal;
}

bool UCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit || Hit.bStartPenetrating || !Finite(Hit.ImpactNormal)) return false;
	const float Angle = std::clamp(WalkableFloorAngle, 0.f, 89.f) * 0.01745329252f;
	return SurfaceNormal(Hit).Z >= std::cos(Angle);
}

bool UCharacterMovementComponent::SweepAt(const FVector& Start, const FVector& Delta, FHitResult& Hit) const
{
	Hit = FHitResult(1.f);
	const auto* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	if (!Capsule || !Capsule->GetWorld()) return false;
	TArray<FHitResult> Hits;
	Capsule->GetWorld()->ComponentSweepMulti(Hits, const_cast<UCapsuleComponent*>(Capsule), Start, Start + Delta, Capsule->GetWorldRotation().Quaternion());
	for (const FHitResult& Candidate : Hits) if (Candidate.bBlockingHit) { Hit = Candidate; return true; }
	return false;
}

bool UCharacterMovementComponent::FindFloor(const FVector& Location, float Distance, FCharacterFloorResult& OutFloor) const
{
	OutFloor = {};
	if (!HasValidUpdatedComponent() || !std::isfinite(Distance) || Distance <= 0.f) return false;
	if (!SweepAt(Location, FVector(0, 0, -Distance), OutFloor.Hit)) return false;
	OutFloor.FloorDistance = Distance * OutFloor.Hit.Time;
	FCollisionGeometry Capsule;
	const bool bLowerContact = UpdatedComponent && Cast<UCapsuleComponent>(UpdatedComponent)->GetCollisionGeometry(Capsule) &&
		OutFloor.Hit.ImpactPoint.Z <= Location.Z - (Capsule.HalfHeight - Capsule.Radius) + 0.003f;
	OutFloor.bWalkableFloor = bLowerContact && IsWalkable(OutFloor.Hit);
	// Keep the capsule's separating normal for motion along a rounded step edge.
	// ImpactNormal describes the support face and is used for the slope policy.
	if (OutFloor.bWalkableFloor) OutFloor.Hit.ImpactNormal = SurfaceNormal(OutFloor.Hit);
	return OutFloor.bWalkableFloor;
}

bool UCharacterMovementComponent::MoveSafely(const FVector& Delta, FHitResult& Hit)
{
	if (!CanContinue()) return false;
	const bool bMoved = SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetWorldRotation(), true, Hit);
	if (!CanContinue()) return false;
	// Overlap/Hit callbacks can teleport the pawn, e.g. a game cutscene or respawn.
	if ((UpdatedComponent->GetWorldLocation() - Hit.Location).SizeSquared() > 0.000025f)
	{
		ResetAfterTeleport();
		bInterrupted = true;
		return false;
	}
	return bMoved;
}

bool UCharacterMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FRotator& Rotation, bool bSweep, FHitResult* Hit)
{
	FHitResult LocalHit(1.f);
	if (bSimulating && !CanContinue()) { if (Hit) *Hit = LocalHit; return false; }
	const bool bMoved = Super::MoveUpdatedComponent(Delta, Rotation, bSweep, &LocalHit);
	if (Hit) *Hit = LocalHit;
	if (bSimulating && CanContinue() && (UpdatedComponent->GetWorldLocation() - LocalHit.Location).SizeSquared() > 0.000025f)
	{
		ResetAfterTeleport();
		bInterrupted = true;
	}
	return bMoved;
}

bool UCharacterMovementComponent::RefreshFloor(float Distance, bool bSnap)
{
	if (!CanContinue()) return false;
	if (!FindFloor(UpdatedComponent->GetWorldLocation(), Distance, CurrentFloor)) return false;
	if (bSnap && CurrentFloor.FloorDistance > FloorGap + 0.001f)
	{
		FHitResult Hit;
		MoveSafely(FVector(0, 0, -(CurrentFloor.FloorDistance - FloorGap)), Hit);
		if (!CanContinue()) return false;
		FindFloor(UpdatedComponent->GetWorldLocation(), std::max(GroundProbeDistance, 0.01f), CurrentFloor);
	}
	return CurrentFloor.bWalkableFloor;
}

void UCharacterMovementComponent::Accelerate(const FVector& Input, float Dt, bool bGrounded)
{
	const FVector Horizontal(Velocity.X, Velocity.Y, 0);
	FVector NewVelocity = Horizontal;
	if (Input.SizeSquared() > SMALL_NUMBER)
		NewVelocity = Approach(Horizontal, Input * std::max(0.f, MaxWalkSpeed), std::max(0.f, MaxAcceleration) * (bGrounded ? 1.f : std::clamp(AirControl, 0.f, 1.f)) * Dt);
	else if (bGrounded)
		NewVelocity = Approach(Horizontal, FVector::ZeroVector, std::max(0.f, BrakingDecelerationWalking) * Dt);
	Velocity.X = NewVelocity.X; Velocity.Y = NewVelocity.Y;
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	if (bSimulating || !IsValid(this) || IsBeingDestroyed()) return;
	Super::TickComponent(DeltaTime);
	LastGroundedDisplacement = FVector::ZeroVector;
	auto* Pawn = Cast<APawn>(GetOwner());
	FVector Input = Pawn ? Pawn->ConsumeMovementInputVector() : FVector::ZeroVector;
	Input.Z = 0.f;
	if (!Finite(Input)) Input = FVector::ZeroVector;
	if (Input.SizeSquared() > 1.f) Input = Input.GetSafeNormal();
	if (!bMovementEnabled || !HasValidUpdatedComponent() || !Cast<UCapsuleComponent>(UpdatedComponent) ||
		!std::isfinite(DeltaTime) || DeltaTime <= 0.f) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld());
	bSimulating = true; bInterrupted = false; SimulationComponent = UpdatedComponent;
	struct FReset { bool& Value; ~FReset() { Value = false; } } Reset{bSimulating};
	if (!Finite(Velocity)) Velocity = FVector::ZeroVector;
	// Bound very long stalls. Ordinary 30/60/144 Hz frames are fully simulated.
	float Remaining = std::min(DeltaTime, 0.25f);
	const int32 Iterations = std::clamp(MaxSimulationIterations, 1, 128);
	for (int32 I = 0; I < Iterations && Remaining > 1.e-6f && CanContinue(); ++I)
	{
		const float Dt = std::min(Remaining, std::max(std::clamp(MaxSimulationTimeStep, 0.001f, 0.05f), Remaining / (Iterations - I)));
		Remaining -= Dt;
		if (Velocity.Z <= 0.f && RefreshFloor(GroundProbeDistance, true))
		{
			MovementMode = ECharacterMovementMode::Walking;
			Velocity.Z = 0.f;
		}
		else MovementMode = ECharacterMovementMode::Falling;
		if (!CanContinue()) break;
		if (bJumpRequested && CanJump())
		{
			Velocity.Z = std::max(0.f, JumpZVelocity);
			MovementMode = ECharacterMovementMode::Falling;
			CurrentFloor = {};
		}
		bJumpRequested = false; // One press, one grounded jump; no automatic midair repeat.
		if (IsMovingOnGround()) PhysWalking(Dt, Input);
		else PhysFalling(Dt, Input);
	}
	if (HasValidUpdatedComponent()) UpdateComponentVelocity();
}

void UCharacterMovementComponent::PhysWalking(float Dt, const FVector& Input)
{
	Accelerate(Input, Dt, true);
	const FVector Start = UpdatedComponent->GetWorldLocation();
	FVector Delta(Velocity.X * Dt, Velocity.Y * Dt, 0);
	const FVector N = CurrentFloor.Hit.Normal;
	if (N.Z > SMALL_NUMBER)
		Delta.Z = std::clamp(-(Delta.X * N.X + Delta.Y * N.Y) / N.Z, -std::max(0.f, MaxStepHeight), std::max(0.f, MaxStepHeight));
	FHitResult Hit;
	if (Delta.SizeSquared() > 1.e-8f) MoveSafely(Delta, Hit);
	if (!CanContinue()) return;
	if (Hit.bBlockingHit)
	{
		HandleImpact(Hit, Dt, Delta);
		if (!CanContinue()) return;
		if (!StepUp(Delta * (1.f - Hit.Time), Hit) && CanContinue())
		{
			SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
			if (!CanContinue()) return;
		}
	}
	if (!RefreshFloor(std::max(0.f, MaxStepHeight) + GroundProbeDistance, true)) MovementMode = ECharacterMovementMode::Falling;
	if (!CanContinue()) return;
	const FVector Moved = UpdatedComponent->GetWorldLocation() - Start;
	if (Hit.bBlockingHit || Delta.SizeSquared() > 1.6e-7f)
	{
		const FVector ActualVelocity = FVector(Moved.X / Dt, Moved.Y / Dt, 0.f).GetClampedToMaxSize(std::max(0.f, MaxWalkSpeed));
		Velocity.X = ActualVelocity.X; Velocity.Y = ActualVelocity.Y;
	}
	// Only grounded motion contributes to game footsteps; teleports interrupt the tick.
	LastGroundedDisplacement += FVector(Moved.X, Moved.Y, 0.f);
}

void UCharacterMovementComponent::PhysFalling(float Dt, const FVector& Input)
{
	Accelerate(Input, Dt, false);
	const float OldZ = Velocity.Z;
	Velocity.Z = std::max(-std::max(0.f, MaxFallSpeed), OldZ - std::max(0.f, Gravity * GravityScale) * Dt);
	const FVector Delta(Velocity.X * Dt, Velocity.Y * Dt, (OldZ + Velocity.Z) * 0.5f * Dt);
	FHitResult Hit;
	MoveSafely(Delta, Hit);
	if (!CanContinue()) return;
	if (Hit.bBlockingHit)
	{
		HandleImpact(Hit, Dt, Delta);
		if (!CanContinue()) return;
		if (Velocity.Z <= 0.f && IsWalkable(Hit))
		{
			Velocity.Z = 0.f;
			MovementMode = ECharacterMovementMode::Walking;
			RefreshFloor(GroundProbeDistance, true);
		}
		else
		{
			if (Velocity.Z > 0.f && Hit.Normal.Z < -0.01f) Velocity.Z = 0.f;
			const FVector N = Hit.Normal.GetSafeNormal();
			const float Inward = Velocity.Dot(N);
			if (Inward < 0.f)
			{
				const float MaxUpwardSpeed = std::max(0.f, Velocity.Z);
				Velocity -= N * Inward;
				Velocity.Z = std::min(Velocity.Z, MaxUpwardSpeed);
			}
			SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
			if (!CanContinue()) return;
		}
	}
}

FVector UCharacterMovementComponent::ComputeSlideVector(const FVector& Delta, float Time, const FVector& Normal, const FHitResult& Hit) const
{
	FVector N = Normal;
	const bool bTooHigh = Hit.ImpactPoint.Z - CurrentFloor.Hit.ImpactPoint.Z > std::max(0.f, MaxStepHeight) + 0.003f;
	if (IsMovingOnGround() && (!IsWalkable(Hit) || bTooHigh) && N.Z > 0.f) N = FVector(N.X, N.Y, 0).GetSafeNormal();
	FVector Slide = Super::ComputeSlideVector(Delta, Time, N, Hit);
	// A steep wall must not convert horizontal falling input into extra upward speed.
	if (IsFalling()) Slide.Z = std::min(Slide.Z, std::max(0.f, Delta.Z * Time));
	return Slide;
}

bool UCharacterMovementComponent::StepUp(const FVector& RemainingDelta, const FHitResult& Obstacle)
{
	if (!CanContinue() || MaxStepHeight <= 0.f || IsWalkable(Obstacle) || RemainingDelta.SizeSquared() < 1.e-8f) return false;
	const FVector Start = UpdatedComponent->GetWorldLocation();
	const FVector Up(0, 0, MaxStepHeight + FloorGap);
	const FVector Forward(RemainingDelta.X, RemainingDelta.Y, 0);
	FHitResult Preview;
	// Preview before moving: rejected stairs must not issue speculative overlap callbacks.
	if (SweepAt(Start, Up, Preview) || SweepAt(Start + Up, Forward, Preview)) return false;
	const FVector RaisedEnd = Start + Up + Forward;
	FCharacterFloorResult Landing;
	if (!FindFloor(RaisedEnd, MaxStepHeight + GroundProbeDistance + FloorGap, Landing)) return false;
	// Rounded capsule contact can perch below a tall box's top. Limit the actual
	// support surface height, otherwise repeated small steps can climb any wall.
	if (Landing.Hit.ImpactPoint.Z - CurrentFloor.Hit.ImpactPoint.Z > MaxStepHeight + 0.003f) return false;
	const float DownDistance = std::max(0.f, Landing.FloorDistance - FloorGap);
	const float Rise = Up.Z - DownDistance;
	if (Rise < -GroundProbeDistance || Rise > MaxStepHeight + 0.003f) return false;
	MoveSafely(Up, Preview);
	if (!CanContinue() || Preview.bBlockingHit) return true;
	MoveSafely(Forward, Preview);
	if (!CanContinue() || Preview.bBlockingHit) return true;
	MoveSafely(FVector(0, 0, -DownDistance), Preview);
	return true;
}
