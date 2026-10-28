#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include <algorithm>

APlayerCameraManager::APlayerCameraManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void APlayerCameraManager::Initialize(APlayerController* InOwner)
{
	PCOwner = InOwner;
}

namespace
{
	bool IsUsableTarget(const AActor* Target, const UWorld* World)
	{
		return Target && GUObjectArray.Find(const_cast<AActor*>(Target)) != INDEX_NONE &&
			IsValid(Target) && !Target->IsActorBeingDestroyed() && Target->GetWorld() == World;
	}

	FQuat SlerpQuat(const FQuat& QuatA, const FQuat& QuatB, float Slerp)
	{
		float CosAlpha = QuatA.X * QuatB.X + QuatA.Y * QuatB.Y + QuatA.Z * QuatB.Z + QuatA.W * QuatB.W;
		FQuat TargetB = QuatB;
		if (CosAlpha < 0.0f)
		{
			CosAlpha = -CosAlpha;
			TargetB = FQuat(-QuatB.X, -QuatB.Y, -QuatB.Z, -QuatB.W);
		}

		float ScaleA, ScaleB;
		if (CosAlpha > 0.9995f)
		{
			ScaleA = 1.0f - Slerp;
			ScaleB = Slerp;
		}
		else
		{
			const float SinAlpha = std::sqrt(1.0f - CosAlpha * CosAlpha);
			const float Alpha = std::atan2(SinAlpha, CosAlpha);
			ScaleA = std::sin((1.0f - Slerp) * Alpha) / SinAlpha;
			ScaleB = std::sin(Slerp * Alpha) / SinAlpha;
		}

		FQuat Result(
			ScaleA * QuatA.X + ScaleB * TargetB.X,
			ScaleA * QuatA.Y + ScaleB * TargetB.Y,
			ScaleA * QuatA.Z + ScaleB * TargetB.Z,
			ScaleA * QuatA.W + ScaleB * TargetB.W
		);
		return Result.Normalized();
	}
}

void APlayerCameraManager::SetViewTarget(AActor* InTarget)
{
	SetViewTargetWithBlend(InTarget, 0.0f);
}

void APlayerCameraManager::SetViewTargetWithBlend(AActor* NewTarget, float BlendTime, EViewTargetBlendFunction BlendFunc, float BlendExp, bool bLockOutgoing)
{
	if (IsPendingKillPending() || (NewTarget && !IsUsableTarget(NewTarget, GetWorld()))) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld());

	if (NewTarget == ViewTarget.Target && PendingViewTarget.Target == nullptr)
	{
		return;
	}

	if (BlendTime <= 0.0f || ViewTarget.Target == nullptr)
	{
		ViewTarget.Target = NewTarget;
		if (NewTarget)
		{
			NewTarget->CalcCamera(0.0f, ViewTarget.POV);
		}
		PendingViewTarget.Target = nullptr;
		BlendTimeToGo = 0.0f;
		bHasCameraCache = false;
		return;
	}

	// A sequence may be destroyed immediately after requesting its return blend.
	// Freeze the rendered outgoing view instead of evaluating that Actor again.
	if (bLockOutgoing && bHasCameraCache) ViewTarget.POV = CameraCachePOV;
	PendingViewTarget.Target = NewTarget;
	if (NewTarget)
	{
		NewTarget->CalcCamera(0.0f, PendingViewTarget.POV);
	}
	BlendTimeToGo = BlendTime;
	BlendParams.BlendTime = BlendTime;
	BlendParams.BlendFunction = BlendFunc;
	BlendParams.BlendExp = BlendExp > 0.0f ? BlendExp : 2.0f;
	BlendParams.bLockOutgoing = bLockOutgoing;
}

AActor* APlayerCameraManager::GetViewTarget() const
{
	return IsUsableTarget(ViewTarget.Target, GetWorld()) ? ViewTarget.Target : nullptr;
}

AActor* APlayerCameraManager::GetPendingViewTarget() const
{
	return IsUsableTarget(PendingViewTarget.Target, GetWorld()) ? PendingViewTarget.Target : nullptr;
}

void APlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	if (!IsUsableTarget(OutVT.Target, GetWorld()))
	{
		OutVT.Target = nullptr;
		if (IsUsableTarget(PCOwner, GetWorld()))
		{
			OutVT.Target = PCOwner->GetPawn();
			if (!IsUsableTarget(OutVT.Target, GetWorld())) OutVT.Target = nullptr;
		}
		if (!OutVT.Target && GetWorld())
		{
			OutVT.Target = GetWorld()->GetMainCamera();
			if (!IsUsableTarget(OutVT.Target, GetWorld())) OutVT.Target = nullptr;
		}
	}

	if (OutVT.Target)
	{
		OutVT.Target->CalcCamera(DeltaTime, OutVT.POV);
		if (!IsUsableTarget(OutVT.Target, GetWorld())) OutVT.Target = nullptr;
	}
}

UCameraModifier* APlayerCameraManager::AddNewCameraModifier(UClass* ModifierClass)
{
	if (IsPendingKillPending() || !ModifierClass || !ModifierClass->IsChildOf(UCameraModifier::StaticClass()) ||
		!ModifierClass->Constructor) return nullptr;
	UCameraModifier* Modifier = NewObject<UCameraModifier>(this, ModifierClass, NAME_None, EObjectFlags::RF_Transient);
	if (Modifier) Modifiers.Add(Modifier);
	return Modifier;
}

void APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier)
{
	const int32 Index = Modifiers.Find(Modifier);
	if (Index == INDEX_NONE) return;
	Modifiers.RemoveAt(Index);
	if (IsValid(Modifier) && GetWorld())
	{
		Modifier->MarkAsGarbage();
		GetWorld()->QueueObjectDeletion(Modifier);
	}
}

void APlayerCameraManager::ClearCameraModifiers()
{
	while (!Modifiers.IsEmpty()) RemoveCameraModifier(Modifiers.Last());
}

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	if (IsPendingKillPending()) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld());
	DeltaTime = std::max(0.0f, DeltaTime);

	UpdateCameraFade(DeltaTime);
	UpdateCameraLetterbox(DeltaTime);

	// 1. 유효하지 않은 타겟 정리
	if (ViewTarget.Target && (GUObjectArray.Find(ViewTarget.Target) == INDEX_NONE ||
		!IsValid(ViewTarget.Target) || ViewTarget.Target->IsActorBeingDestroyed()))
	{
		ViewTarget.Target = nullptr;
	}
	if (PendingViewTarget.Target && (GUObjectArray.Find(PendingViewTarget.Target) == INDEX_NONE ||
		!IsValid(PendingViewTarget.Target) || PendingViewTarget.Target->IsActorBeingDestroyed()))
	{
		PendingViewTarget.Target = nullptr;
		BlendTimeToGo = 0.0f;
	}

	FMinimalViewInfo NewPOV;

	// 2. 블렌드 진행 여부 확인
	if (PendingViewTarget.Target != nullptr && BlendTimeToGo > 0.0f)
	{
		BlendTimeToGo = std::max(0.0f, BlendTimeToGo - DeltaTime);

		if (!BlendParams.bLockOutgoing)
		{
			UpdateViewTarget(ViewTarget, DeltaTime);
		}
		UpdateViewTarget(PendingViewTarget, DeltaTime);
		if (IsPendingKillPending()) return;
		if (!PendingViewTarget.Target)
		{
			BlendTimeToGo = 0.0f;
			bHasCameraCache = false;
			return;
		}

		const float Duration = std::max(BlendParams.BlendTime, 0.0001f);
		const float Pct = std::clamp(1.0f - (BlendTimeToGo / Duration), 0.0f, 1.0f);
		float BlendWeight = Pct;

		switch (BlendParams.BlendFunction)
		{
		case VTBlend_Linear:
			BlendWeight = Pct;
			break;
		case VTBlend_Cubic:
			BlendWeight = Pct * Pct * (3.0f - 2.0f * Pct);
			break;
		case VTBlend_EaseIn:
			BlendWeight = std::pow(Pct, BlendParams.BlendExp);
			break;
		case VTBlend_EaseOut:
			BlendWeight = 1.0f - std::pow(1.0f - Pct, BlendParams.BlendExp);
			break;
		case VTBlend_EaseInOut:
			if (Pct < 0.5f)
				BlendWeight = 0.5f * std::pow(2.0f * Pct, BlendParams.BlendExp);
			else
				BlendWeight = 1.0f - 0.5f * std::pow(2.0f * (1.0f - Pct), BlendParams.BlendExp);
			break;
		default:
			BlendWeight = Pct;
			break;
		}

		// POV 보간
		NewPOV.Location = ViewTarget.POV.Location + (PendingViewTarget.POV.Location - ViewTarget.POV.Location) * BlendWeight;

		const FQuat AQuat = ViewTarget.POV.Rotation.Quaternion();
		const FQuat BQuat = PendingViewTarget.POV.Rotation.Quaternion();
		NewPOV.Rotation = SlerpQuat(AQuat, BQuat, BlendWeight).ToFRotator();

		NewPOV.FieldOfView = ViewTarget.POV.FieldOfView + (PendingViewTarget.POV.FieldOfView - ViewTarget.POV.FieldOfView) * BlendWeight;
		NewPOV.OrthoWidth = ViewTarget.POV.OrthoWidth + (PendingViewTarget.POV.OrthoWidth - ViewTarget.POV.OrthoWidth) * BlendWeight;
		NewPOV.NearClip = ViewTarget.POV.NearClip + (PendingViewTarget.POV.NearClip - ViewTarget.POV.NearClip) * BlendWeight;
		NewPOV.FarClip = ViewTarget.POV.FarClip + (PendingViewTarget.POV.FarClip - ViewTarget.POV.FarClip) * BlendWeight;
		NewPOV.AspectRatio = ViewTarget.POV.AspectRatio + (PendingViewTarget.POV.AspectRatio - ViewTarget.POV.AspectRatio) * BlendWeight;
		NewPOV.bIsPerspective = PendingViewTarget.POV.bIsPerspective;

		if (BlendTimeToGo <= 0.0f)
		{
			ViewTarget = PendingViewTarget;
			PendingViewTarget.Target = nullptr;
		}
	}
	else
	{
		if (PendingViewTarget.Target != nullptr)
		{
			ViewTarget = PendingViewTarget;
			PendingViewTarget.Target = nullptr;
		}
		UpdateViewTarget(ViewTarget, DeltaTime);
		if (!ViewTarget.Target)
		{
			bHasCameraCache = false;
			return;
		}
		NewPOV = ViewTarget.POV;
	}

	if (IsPendingKillPending()) return;

	TArray<UCameraModifier*> OrderedModifiers;
	for (UCameraModifier* Modifier : Modifiers)
		if (IsValid(Modifier) && Modifier->IsActive()) OrderedModifiers.Add(Modifier);
	std::stable_sort(OrderedModifiers.begin(), OrderedModifiers.end(),
		[](const UCameraModifier* A, const UCameraModifier* B) { return A->GetPriority() < B->GetPriority(); });
	for (UCameraModifier* Modifier : OrderedModifiers)
	{
		if (!IsValid(Modifier) || Modifiers.Find(Modifier) == INDEX_NONE || !Modifier->IsActive()) continue;
		const bool bStop = Modifier->ModifyCamera(DeltaTime, NewPOV);
		if (IsPendingKillPending()) return;
		if (bStop) break;
	}
	CameraCachePOV = NewPOV;
	bHasCameraCache = true;
}

void APlayerCameraManager::EndPlay()
{
	ClearCameraModifiers();
	bHasCameraCache = false;
	ViewTarget.Target = nullptr;
	PendingViewTarget.Target = nullptr;
	BlendTimeToGo = 0.0f;
	PCOwner = nullptr;
	Super::EndPlay();
}

void APlayerCameraManager::StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color, bool bHold, bool bInFadeHUD)
{
	FadeColor = Color;
	FadeAlpha = FVector2(FromAlpha, ToAlpha);
	FadeTime = Duration;
	FadeTimeRemaining = Duration;
	bHoldWhenFinished = bHold;
	bFadeHUD = bInFadeHUD;

	if (Duration <= 0.0f)
	{
		FadeAmount = ToAlpha;
		FadeTimeRemaining = 0.0f;
		bEnableFading = bHold;
		return;
	}

	FadeAmount = FromAlpha;
	bEnableFading = true;
}

void APlayerCameraManager::StopCameraFade()
{
	bEnableFading = false;
	FadeAmount = 0.0f;
	FadeTime = 0.0f;
	FadeTimeRemaining = 0.0f;
	bFadeHUD = true;
}

void APlayerCameraManager::FadeIn(float Duration, const FLinearColor& Color, bool bInFadeHUD)
{
	StartCameraFade(1.0f, 0.0f, Duration, Color, false, bInFadeHUD);
}

void APlayerCameraManager::FadeOut(float Duration, const FLinearColor& Color, bool bHold, bool bInFadeHUD)
{
	StartCameraFade(0.0f, 1.0f, Duration, Color, bHold, bInFadeHUD);
}

void APlayerCameraManager::EnterLetterbox(float Duration, float TargetRatio)
{
	StartLetterbox(CurrentLetterboxRatio, TargetRatio, Duration);
}

void APlayerCameraManager::ExitLetterbox(float Duration)
{
	StartLetterbox(CurrentLetterboxRatio, 0.0f, Duration);
}

void APlayerCameraManager::StartLetterbox(float FromRatio, float ToRatio, float Duration)
{
	bEnableLetterbox = true;
	LetterboxRatio = FVector2(FromRatio, ToRatio);
	LetterboxTime = Duration;
	LetterboxTimeRemaining = Duration;

	if (Duration <= 0.0f)
	{
		CurrentLetterboxRatio = ToRatio;
		LetterboxTimeRemaining = 0.0f;
		bEnableLetterbox = false;
	}
	else
	{
		CurrentLetterboxRatio = FromRatio;
	}
}

void APlayerCameraManager::StopLetterbox()
{
	bEnableLetterbox = false;
	CurrentLetterboxRatio = 0.0f;
	LetterboxTime = 0.0f;
	LetterboxTimeRemaining = 0.0f;
}

void APlayerCameraManager::UpdateCameraFade(float DeltaTime)
{
	if (!bEnableFading) return;

	FadeTimeRemaining -= DeltaTime;

	if (FadeTimeRemaining <= 0.0f)
	{
		FadeTimeRemaining = 0.0f;
		FadeAmount = FadeAlpha.Y;
		if (!bHoldWhenFinished)
		{
			bEnableFading = false;
		}
		return;
	}
	float Progress = 1.0f - (FadeTimeRemaining / FadeTime);
	Progress = std::clamp(Progress, 0.0f, 1.0f);
	FadeAmount = FadeAlpha.X + (FadeAlpha.Y - FadeAlpha.X) * Progress;
}

void APlayerCameraManager::UpdateCameraLetterbox(float DeltaTime)
{
	if (!bEnableLetterbox) return;

	LetterboxTimeRemaining -= DeltaTime;
	if (LetterboxTimeRemaining <= 0.0f)
	{
		LetterboxTimeRemaining = 0.0f;
		CurrentLetterboxRatio = LetterboxRatio.Y;
		bEnableLetterbox = false;
		return;
	}

	float Progress = 1.0f - std::clamp(LetterboxTimeRemaining / LetterboxTime, 0.0f, 1.0f);
	CurrentLetterboxRatio = LetterboxRatio.X + (LetterboxRatio.Y - LetterboxRatio.X) * Progress;
}
