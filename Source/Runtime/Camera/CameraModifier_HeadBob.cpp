#include "EnginePCH.h"
#include "Camera/CameraModifier_HeadBob.h"
#include "Camera/HeadBobComponent.h"
#include "Camera/PlayerCameraManager.h"
#include <algorithm>
#include <cmath>

namespace
{
	float NonNegative(float Value, float Fallback = 0.0f)
	{
		return std::isfinite(Value) ? std::max(0.0f, Value) : Fallback;
	}

	float Smooth(float Value) { return Value * Value * (3.0f - 2.0f * Value); }

	float Approach(float Current, float Target, float Step)
	{
		return Current < Target ? std::min(Current + Step, Target) : std::max(Current - Step, Target);
	}
}

void UCameraModifier_HeadBob::ResetAnimation()
{
	SourceUUID = 0;
	ResetSerial = LastInputSerial = 0;
	IdlePhase = 0.0;
	IdleWeight = WalkWeight = WalkOffset = StartOffset = StepAmplitude = Elapsed = 0.0f;
	bActivePulse = bReturning = false;
}

bool UCameraModifier_HeadBob::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const float SafeDelta = std::isfinite(DeltaTime) && DeltaTime > 0.0f ? DeltaTime : 0.0f;
	Super::ModifyCamera(SafeDelta, InOutPOV);
	APlayerCameraManager* Manager = GetCameraOwner();
	AActor* Target = GetViewTarget();
	// Modifiers run after view blending. Suppress bob during that blend so an
	// outgoing player's movement never shakes the cinematic camera.
	if (!Manager || Manager->IsBlending() || !Target || GUObjectArray.Find(Target) == INDEX_NONE ||
		!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		ResetAnimation();
		return false;
	}
	auto* Bob = Target->GetComponentByClass<UHeadBobComponent>();
	if (!IsValid(Bob) || Bob->IsBeingDestroyed() || Bob->IsEndingPlay() ||
		!Bob->IsActive() || !Bob->bEnabled || Bob->IsSuspended() || Bob->GetMovementState().UpdateSerial == 0)
	{
		ResetAnimation();
		return false;
	}
	const auto& Input = Bob->GetMovementState();
	if (SourceUUID != Bob->GetUUID() || ResetSerial != Input.ResetSerial)
	{
		ResetAnimation();
		SourceUUID = Bob->GetUUID();
		ResetSerial = Input.ResetSerial;
	}

	const float BlendTime = NonNegative(Bob->StateBlendTime, 0.3f);
	const float BlendStep = BlendTime > 0.0f ? SafeDelta / BlendTime : 1.0f;
	IdleWeight = Approach(IdleWeight, Input.bGrounded && !Input.bWalking ? 1.0f : 0.0f, BlendStep);
	WalkWeight = Approach(WalkWeight, Input.bWalking ? 1.0f : 0.0f, BlendStep);
	// Keep the breathing phase running through state changes instead of
	// restarting the sine wave every time the player stops.
	constexpr double TwoPi = 6.283185307179586;
	IdlePhase = std::fmod(IdlePhase + TwoPi * NonNegative(Bob->IdleFrequency) * SafeDelta, TwoPi);
	const float IdleOffset = NonNegative(Bob->IdleAmplitude) * static_cast<float>(std::sin(IdlePhase));
	const float Amplitude = NonNegative(Bob->Amplitude);
	const float Multiplier = NonNegative(Bob->ForwardMultiplier, 1.0f);
	const float MaxAmplitude = Amplitude * std::max(1.0f, Multiplier);
	const float PulseDuration = std::max(0.01f, NonNegative(Bob->Duration, 0.24f));
	WalkOffset = std::clamp(WalkOffset, -MaxAmplitude, 0.0f);
	if (SafeDelta > 0.0f && LastInputSerial != Input.UpdateSerial)
	{
		LastInputSerial = Input.UpdateSerial;
		if (Input.bStep && Input.bWalking)
		{
			StepAmplitude = Amplitude * (1.0f + (Multiplier - 1.0f) * Input.ForwardWeight);
			StartOffset = WalkOffset;
			Elapsed = 0.0f;
			bActivePulse = true;
			bReturning = false;
		}
	}
	if (!Input.bWalking && bActivePulse && !bReturning)
	{
		StartOffset = WalkOffset;
		Elapsed = 0.0f;
		bReturning = true;
	}
	if (bActivePulse)
	{
		Elapsed += SafeDelta;
		const float ReturnDuration = std::min(0.1f, PulseDuration * 0.5f);
		const float T = std::clamp(Elapsed / (bReturning ? ReturnDuration : PulseDuration), 0.0f, 1.0f);
		if (bReturning) WalkOffset = StartOffset * (1.0f - Smooth(T));
		else if (T < 0.25f) WalkOffset = StartOffset + (-StepAmplitude - StartOffset) * Smooth(T / 0.25f);
		else WalkOffset = -StepAmplitude * (1.0f - Smooth((T - 0.25f) / 0.75f));
		WalkOffset = std::clamp(WalkOffset, -MaxAmplitude, 0.0f);
		if (T >= 1.0f)
		{
			WalkOffset = 0.0f;
			bActivePulse = bReturning = false;
		}
	}
	const float Offset = (IdleOffset * IdleWeight + WalkOffset * WalkWeight) * GetAlpha();
	// Preserve the original pawn-relative vertical axis, independent of camera pitch.
	InOutPOV.Location += Target->GetActorRotation().Quaternion().RotateVector(FVector(0.0f, 0.0f, Offset));
	return false;
}
