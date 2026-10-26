#include "EnginePCH.h"
#include "Camera/HeadBobComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include <algorithm>
#include <cmath>

UCameraComponent* UHeadBobComponent::GetCamera() const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || Owner->IsPendingKillPending()) return nullptr;
	auto* Camera = Owner->GetComponentByClass<UCameraComponent>();
	return IsValid(Camera) && !Camera->IsBeingDestroyed() ? Camera : nullptr;
}

void UHeadBobComponent::BeginPlay()
{
	Super::BeginPlay();
	bSuspended = false;
	Rebase();
}

void UHeadBobComponent::EndPlay()
{
	Reset();
	Super::EndPlay();
}

void UHeadBobComponent::ClearPulse()
{
	Offset = StartOffset = StepAmplitude = Elapsed = 0.f;
	bActivePulse = bReturning = false;
}

void UHeadBobComponent::Reset()
{
	ClearPulse();
	if (!bSuspended)
		if (auto* Camera = GetCamera()) Camera->SetRelativeLocation(BaseLocation);
}

void UHeadBobComponent::Rebase()
{
	ClearPulse();
	if (auto* Camera = GetCamera()) BaseLocation = Camera->GetRelativeLocation();
}

void UHeadBobComponent::SetSuspended(bool bValue)
{
	if (bSuspended == bValue) return;
	if (bValue) Reset();
	else Rebase();
	bSuspended = bValue;
}

void UHeadBobComponent::UpdateBob(float DeltaTime, const FVector& Movement, bool bStep)
{
	if (bSuspended) return;
	if (!IsActive() || !bEnabled || !std::isfinite(Amplitude) || Amplitude <= 0.f ||
		!std::isfinite(Duration) || Duration <= 0.f)
	{
		Reset();
		return;
	}
	if (!std::isfinite(DeltaTime) || DeltaTime <= 0.f) return;
	UCameraComponent* Camera = GetCamera();
	if (!Camera) return;
	const float PulseDuration = std::max(0.01f, Duration);
	const bool bMoved = Movement.SizeSquared() > 0.00000001f;
	const float Multiplier = std::isfinite(ForwardMultiplier) ? std::max(0.f, ForwardMultiplier) : 1.f;
	const float MaxAmplitude = Amplitude * std::max(1.f, Multiplier);
	Offset = std::clamp(Offset, -MaxAmplitude, 0.f);
	if (bStep)
	{
		const FVector Forward = GetOwner()->GetActorRotation().Quaternion().GetForwardVector();
		const FVector FlatForward(Forward.X, Forward.Y, 0.f);
		const float Weight = std::clamp(std::abs(Movement.GetSafeNormal().Dot(FlatForward.GetSafeNormal())), 0.f, 1.f);
		StepAmplitude = Amplitude * (1.f + (Multiplier - 1.f) * Weight);
		StartOffset = Offset;
		Elapsed = 0.f;
		bActivePulse = true;
		bReturning = false;
	}
	else if (!bMoved && bActivePulse && !bReturning)
	{
		StartOffset = Offset;
		Elapsed = 0.f;
		bReturning = true;
	}
	if (bActivePulse)
	{
		Elapsed += DeltaTime;
		const float ReturnDuration = std::min(0.1f, PulseDuration * 0.5f);
		const float T = std::clamp(Elapsed / (bReturning ? ReturnDuration : PulseDuration), 0.f, 1.f);
		const auto Smooth = [](float V) { return V * V * (3.f - 2.f * V); };
		if (bReturning) Offset = StartOffset * (1.f - Smooth(T));
		else if (T < 0.25f) Offset = StartOffset + (-StepAmplitude - StartOffset) * Smooth(T / 0.25f);
		else Offset = -StepAmplitude * (1.f - Smooth((T - 0.25f) / 0.75f));
		Offset = std::clamp(Offset, -MaxAmplitude, 0.f);
		if (T >= 1.f) ClearPulse();
	}
	Camera->SetRelativeLocation(BaseLocation + FVector(0.f, 0.f, Offset));
}
