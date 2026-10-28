#include "EnginePCH.h"
#include "Camera/SpringArmComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsFinite(const FVector& V)
	{
		return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
	}

	float LagAlpha(float Speed, float DeltaTime)
	{
		const float SafeSpeed = std::isfinite(Speed) ? std::max(0.0f, Speed) : 10.0f;
		return SafeSpeed == 0.0f ? 1.0f : std::clamp(SafeSpeed * DeltaTime, 0.0f, 1.0f);
	}

	// This engine has no QInterpTo/Slerp API. Interpolate the shortest quaternion
	// arc locally instead of adding camera-specific behavior to the math library.
	FRotator InterpolateRotation(const FRotator& From, const FRotator& To, float Alpha)
	{
		if (Alpha <= 0.0f) return From;
		if (Alpha >= 1.0f) return To;
		FQuat A = From.Quaternion().Normalized(), B = To.Quaternion().Normalized();
		float Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
		if (Dot < 0.0f)
		{
			B = FQuat(-B.X, -B.Y, -B.Z, -B.W);
			Dot = -Dot;
		}
		float FromWeight = 1.0f - Alpha, ToWeight = Alpha;
		if (Dot < 0.9995f)
		{
			const float Angle = std::acos(std::clamp(Dot, 0.0f, 1.0f));
			const float InvSin = 1.0f / std::sin(Angle);
			FromWeight = std::sin((1.0f - Alpha) * Angle) * InvSin;
			ToWeight = std::sin(Alpha * Angle) * InvSin;
		}
		return FQuat(A.X * FromWeight + B.X * ToWeight, A.Y * FromWeight + B.Y * ToWeight,
			A.Z * FromWeight + B.Z * ToWeight, A.W * FromWeight + B.W * ToWeight).Normalized().ToFRotator();
	}
}

USpringArmComponent::USpringArmComponent()
{
	bAutoActivate = false;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
	SetComponentTickEnabled(false);
}

void USpringArmComponent::SetArmEnd(USceneComponent* Component)
{
	ArmEndComponentName = IsValid(Component) && Component->GetOwner() == GetOwner() &&
		Component->GetAttachParent() == this ? Component->GetName() : FString();
	bHasValidLagState = false;
}

USceneComponent* USpringArmComponent::GetArmEnd() const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || ArmEndComponentName.empty()) return nullptr;
	auto* End = Cast<USceneComponent>(Owner->FindComponentByName(FName(ArmEndComponentName)));
	return IsValid(End) && !End->IsBeingDestroyed() && End->GetAttachParent() == this ? End : nullptr;
}

void USpringArmComponent::Activate(bool bReset)
{
	const bool bWasActive = IsActive();
	Super::Activate(bReset);
	if (IsActive() && (bReset || !bWasActive))
	{
		ResetArmEnd();
		UpdateArm(); // Switching on must produce the new endpoint before the next Tick.
	}
}

void USpringArmComponent::Deactivate()
{
	Super::Deactivate();
	SetComponentTickEnabled(false);
	ResetArmEnd(); // Preserve TargetArmLength; only the actual endpoint is collapsed.
}

void USpringArmComponent::ResetArmEnd()
{
	bHasValidLagState = false;
	bIsCameraFixed = false;
	UnfixedCameraPosition = GetWorldLocation();
	if (auto* End = GetArmEnd())
		End->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
}

void USpringArmComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (IsActive()) UpdateArm(DeltaTime);
}

FVector USpringArmComponent::BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation,
	bool bHitSomething, float /*DeltaTime*/)
{
	return bHitSomething ? TraceHitLocation : DesiredArmLocation;
}

void USpringArmComponent::UpdateArm(float DeltaTime)
{
	if (!IsActive()) return;
	auto* End = GetArmEnd();
	if (!End) return;
	const float SafeDelta = std::isfinite(DeltaTime) && DeltaTime > 0.0f ? DeltaTime : 0.0f;
	const float Length = std::isfinite(TargetArmLength) ? std::max(0.0f, TargetArmLength) : 0.0f;
	const FVector Start = GetWorldLocation() + (IsFinite(TargetOffset) ? TargetOffset : FVector::ZeroVector);
	FRotator DesiredRotation = GetWorldRotation(); // Pawn supplies Pitch; parent supplies Yaw.
	FVector DesiredLocation = Start;
	if (bHasValidLagState)
	{
		if (bEnableCameraRotationLag)
			DesiredRotation = InterpolateRotation(PreviousDesiredRotation, DesiredRotation,
				LagAlpha(CameraRotationLagSpeed, SafeDelta));
		if (bEnableCameraLag)
		{
			DesiredLocation = PreviousDesiredLocation + (Start - PreviousDesiredLocation) * LagAlpha(CameraLagSpeed, SafeDelta);
			if (std::isfinite(CameraLagMaxDistance) && CameraLagMaxDistance > 0.0f)
				DesiredLocation = Start + (DesiredLocation - Start).GetClampedToMaxSize(CameraLagMaxDistance);
		}
	}
	// Store the lagged origin, NOT the collision-adjusted endpoint. A wall must
	// never shorten TargetArmLength or leave a distance recovery state behind.
	PreviousDesiredLocation = DesiredLocation;
	PreviousDesiredRotation = DesiredRotation;
	bHasValidLagState = true;
	const FQuat ViewRotation = DesiredRotation.Quaternion();
	DesiredLocation -= ViewRotation.GetForwardVector() * Length;
	DesiredLocation += ViewRotation.RotateVector(IsFinite(SocketOffset) ? SocketOffset : FVector::ZeroVector);
	UnfixedCameraPosition = DesiredLocation;
	FVector ResultLocation = DesiredLocation;
	if (bDoCollisionTest && GetWorld() && (DesiredLocation - Start).SizeSquared() > SMALL_NUMBER)
	{
		FHitResult Hit;
		const float Radius = std::isfinite(ProbeSize) ? std::max(0.0f, ProbeSize) : 0.15f;
		const bool bHit = GetWorld()->SphereSweepSingleByChannel(Start, DesiredLocation, Radius, ProbeChannel, Hit, GetOwner());
		FVector HitLocation = DesiredLocation;
		if (bHit)
		{
			// Match the movement query's small contact margin. An embedded pivot
			// has no clear segment, so collapse immediately instead of interpolating
			// through the wall. The sweep itself only queries, never dispatches events.
			const FVector Trace = DesiredLocation - Start;
			const float Distance = Trace.Size();
			HitLocation = Hit.bStartPenetrating ? Start :
				Start + Trace * (std::max(0.0f, Distance * Hit.Time - 0.001f) / Distance);
		}
		ResultLocation = BlendLocations(DesiredLocation, HitLocation, bHit, SafeDelta);
	}
	bIsCameraFixed = (ResultLocation - DesiredLocation).SizeSquared() > SMALL_NUMBER;
	// ArmEnd substitutes for UE's socket; the existing Camera stays neutral.
	End->SetWorldLocationAndRotation(ResultLocation, DesiredRotation, false);
	// TODO: lag substepping for fluctuating frame times; smooth mode transitions.
}
