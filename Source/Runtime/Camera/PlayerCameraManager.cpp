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

void APlayerCameraManager::SetViewTarget(AActor* InTarget)
{
	if (InTarget && (GUObjectArray.Find(InTarget) == INDEX_NONE || !IsValid(InTarget) || InTarget->IsActorBeingDestroyed() ||
		InTarget->GetWorld() != GetWorld())) return;
	ViewTarget = InTarget;
}

AActor* APlayerCameraManager::GetViewTarget() const
{
	return ViewTarget && GUObjectArray.Find(ViewTarget) != INDEX_NONE &&
		IsValid(ViewTarget) && !ViewTarget->IsActorBeingDestroyed() ? ViewTarget : nullptr;
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

	UpdateCameraFade(DeltaTime);
	UpdateCameraLetterbox(DeltaTime);

	AActor* Target = GetViewTarget();
	if (!Target && PCOwner && GUObjectArray.Find(PCOwner) != INDEX_NONE &&
		IsValid(PCOwner) && !PCOwner->IsActorBeingDestroyed())
	{
		Target = PCOwner->GetPawn();
		if (!IsValid(Target) || Target->IsActorBeingDestroyed()) Target = nullptr;
	}
	if (!Target && GetWorld())
	{
		Target = GetWorld()->GetMainCamera();
		if (!IsValid(Target) || Target->IsActorBeingDestroyed()) Target = nullptr;
	}
	if (!Target)
	{
		ViewTarget = nullptr;
		bHasCameraCache = false;
		return;
	}
	ViewTarget = Target;

	FMinimalViewInfo NewPOV;
	Target->CalcCamera(DeltaTime, NewPOV);
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
	ViewTarget = nullptr;
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
