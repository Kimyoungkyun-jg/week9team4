#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	bool IsUsableCameraTarget(AActor* Target)
	{
		// 프레임 사이에 실제 삭제된 Actor도 역참조하지 않는다.
		return Target && GUObjectArray.Find(Target) != INDEX_NONE &&
			IsValid(Target) && !Target->IsActorBeingDestroyed();
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

	FMinimalViewInfo BlendCameraViews(const FMinimalViewInfo& From, const FMinimalViewInfo& To, float Alpha)
	{
		if (Alpha <= 0.0f) return From;
		if (Alpha >= 1.0f) return To;

		auto Blend = [Alpha](float A, float B) { return A + (B - A) * Alpha; };
		FMinimalViewInfo Result = From;
		Result.Location = From.Location + (To.Location - From.Location) * Alpha;
		Result.Rotation = SlerpQuat(From.Rotation.Quaternion(), To.Rotation.Quaternion(), Alpha).ToFRotator();
		Result.FOV = Blend(From.FOV, To.FOV);
		Result.AspectRatio = Blend(From.AspectRatio, To.AspectRatio);
		Result.NearClipPlane = Blend(From.NearClipPlane, To.NearClipPlane);
		Result.FarClipPlane = Blend(From.FarClipPlane, To.FarClipPlane);
		Result.OrthoWidth = Blend(From.OrthoWidth, To.OrthoWidth);
		// 투영 방식은 수치 보간할 수 없으므로 전환 완료 시 도착 대상의 방식을 채택한다.
		return Result;
	}
}

APlayerCameraManager::APlayerCameraManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

APlayerCameraManager::~APlayerCameraManager()
{
	RemoveAllCameraModifiers();
	FlushRemovedCameraModifiers();
}

void APlayerCameraManager::EndPlay()
{
	RemoveAllCameraModifiers();
	bHasValidCameraPOV = false;
	ViewTarget.Target = nullptr;
	ClearPendingViewTarget();
	PCOwner = nullptr;
	Super::EndPlay();
}

void APlayerCameraManager::InitializeFor(APlayerController* InController)
{
	PCOwner = InController;
	++ViewTargetRevision;
	ClearPendingViewTarget();
	bHasValidCameraPOV = false;
}

// Performs per-tick camera update.
// Called once per tick after all other actors have been ticked.(출처: 언리얼)
void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	if (bIsUpdatingCamera || IsPendingKillPending()) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld());
	bIsUpdatingCamera = true;
	struct FScopedCameraUpdate
	{
		APlayerCameraManager& Manager;
		~FScopedCameraUpdate()
		{
			Manager.bIsUpdatingCamera = false;
			Manager.FlushRemovedCameraModifiers();
		}
	} CameraUpdateScope{*this};

	UpdateViewTarget(DeltaTime);
	if (bHasValidCameraPOV && !IsPendingKillPending())
	{
		CameraPOV = BaseCameraPOV;
		ApplyCameraModifiers(DeltaTime, CameraPOV);
	}

	if (!IsPendingKillPending())
	{
		const float EffectDelta = std::isfinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
		UpdateCameraFade(EffectDelta);
		UpdateCameraLetterbox(EffectDelta);
	}
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget, float BlendTime)
{
	SetViewTargetWithBlend(NewViewTarget, BlendTime, VTBlend_Linear);
}

void APlayerCameraManager::SetViewTargetWithBlend(AActor* NewViewTarget, float BlendTime, EViewTargetBlendFunction BlendFunc, float BlendExp, bool bInLockOutgoing)
{
	if (IsPendingKillPending() || (NewViewTarget && (!IsUsableCameraTarget(NewViewTarget) || NewViewTarget->GetWorld() != GetWorld()))) return;
	if (!std::isfinite(BlendTime) || BlendTime <= 0.0f ||
		!bHasValidCameraPOV || !IsUsableCameraTarget(NewViewTarget))
	{
		++ViewTargetRevision;
		ClearPendingViewTarget();
		ViewTarget.Target = NewViewTarget;
		bHasValidCameraPOV = false;
		return;
	}

	// 매 프레임 같은 요청이 들어와도 타이머를 처음으로 되돌리지 않는다.
	if ((IsBlending() && PendingViewTarget.Target == NewViewTarget) ||
		(!IsBlending() && ViewTarget.Target == NewViewTarget)) return;

	++ViewTargetRevision;
	if (IsBlending() || bInLockOutgoing)
	{
		// 전환 중의 블렌딩 결과에서 다시 출발한다. Modifier 적용 후의 POV는 사용하지 않는다.
		ViewTarget.POV = BaseCameraPOV;
		bLockOutgoing = true;
	}
	PendingViewTarget.Target = NewViewTarget;
	PendingViewTarget.POV = BaseCameraPOV;
	BlendParams = {BlendTime, BlendFunc, std::isfinite(BlendExp) && BlendExp > 0.0f ? BlendExp : 2.0f, bInLockOutgoing};
	BlendDuration = BlendTime;
	BlendTimeRemaining = BlendTime;
}

AActor* APlayerCameraManager::GetViewTarget() const
{
	return IsUsableCameraTarget(ViewTarget.Target) ? ViewTarget.Target : nullptr;
}

AActor* APlayerCameraManager::GetPendingViewTarget() const
{
	return IsUsableCameraTarget(PendingViewTarget.Target) ? PendingViewTarget.Target : nullptr;
}

void APlayerCameraManager::ClearPendingViewTarget()
{
	PendingViewTarget = FViewTarget{};
	BlendDuration = 0.0f;
	BlendTimeRemaining = 0.0f;
	bLockOutgoing = false;
}

UCameraModifier* APlayerCameraManager::AddNewCameraModifier(UClass* ModifierClass)
{
	if (IsPendingKillPending() || bIsClearingModifiers || !ModifierClass ||	!ModifierClass->IsChildOf(UCameraModifier::StaticClass()) || !ModifierClass->Constructor)
	{
		return nullptr;
	}

	UCameraModifier* Modifier = NewObject<UCameraModifier>(this, ModifierClass, NAME_None, EObjectFlags::RF_Transient);
	if (!Modifier) return nullptr;
	Modifier->CameraOwner = this;

	// 같은 우선순위는 추가한 순서를 유지한다.
	int32 Index = 0;
	while (Index < ModifierList.Num() && ModifierList[Index]->GetPriority() <= Modifier->GetPriority()) ++Index;
	ModifierList.Insert(Modifier, Index);
	return Modifier;
}

UCameraModifier* APlayerCameraManager::FindCameraModifier(UClass* ModifierClass) const
{
	if (!ModifierClass || !ModifierClass->IsChildOf(UCameraModifier::StaticClass())) return nullptr;
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (IsValid(Modifier) && Modifier->IsA(ModifierClass)) return Modifier;
	}
	return nullptr;
}

bool APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier)
{
	const int32 Index = ModifierList.Find(Modifier);
	if (Index == INDEX_NONE) return false;
	ModifierList.RemoveAt(Index);
	Modifier->CameraOwner = nullptr;
	Modifier->MarkAsGarbage();

	// ModifyCamera 안에서 자신이나 다른 Modifier를 제거해도 호출 종료까지 생존한다.
	if (bIsUpdatingCamera) RemovedModifiers.Add(Modifier);
	else delete Modifier;
	return true;
}

void APlayerCameraManager::RemoveAllCameraModifiers()
{
	const bool bWasClearing = bIsClearingModifiers;
	bIsClearingModifiers = true;
	const auto Modifiers = ModifierList;
	for (UCameraModifier* Modifier : Modifiers) RemoveCameraModifier(Modifier);
	bIsClearingModifiers = bWasClearing;
}

void APlayerCameraManager::FlushRemovedCameraModifiers()
{
	const auto Modifiers = std::move(RemovedModifiers);
	RemovedModifiers.Reset();
	for (UCameraModifier* Modifier : Modifiers) delete Modifier;
}

FCameraShakeHandle APlayerCameraManager::StartCameraShake(const FCameraShakeSettings& Settings, float Scale)
{
	if (IsPendingKillPending() || bIsClearingModifiers || !Settings.IsValid() || !std::isfinite(Scale) || Scale <= 0.0f)
		return {};
	auto* Modifier = Cast<UCameraModifier_CameraShake>(FindCameraModifier(UCameraModifier_CameraShake::StaticClass()));
	if (!Modifier)
		Modifier = Cast<UCameraModifier_CameraShake>(AddNewCameraModifier(UCameraModifier_CameraShake::StaticClass()));
	return Modifier ? Modifier->AddCameraShake(Settings, Scale) : FCameraShakeHandle{};
}

bool APlayerCameraManager::StopCameraShake(FCameraShakeHandle Handle, bool bImmediately)
{
	auto* Modifier = Cast<UCameraModifier_CameraShake>(FindCameraModifier(UCameraModifier_CameraShake::StaticClass()));
	return Modifier && Modifier->StopCameraShake(Handle, bImmediately);
}

void APlayerCameraManager::StopAllCameraShakes(bool bImmediately)
{
	if (auto* Modifier = Cast<UCameraModifier_CameraShake>(FindCameraModifier(UCameraModifier_CameraShake::StaticClass())))
		Modifier->StopAllCameraShakes(bImmediately);
}

bool APlayerCameraManager::IsCameraShakeActive(FCameraShakeHandle Handle) const
{
	const auto* Modifier = Cast<UCameraModifier_CameraShake>(FindCameraModifier(UCameraModifier_CameraShake::StaticClass()));
	return Modifier && Modifier->IsCameraShakeActive(Handle);
}

int32 APlayerCameraManager::GetActiveCameraShakeCount() const
{
	const auto* Modifier = Cast<UCameraModifier_CameraShake>(FindCameraModifier(UCameraModifier_CameraShake::StaticClass()));
	return Modifier ? Modifier->GetActiveCameraShakeCount() : 0;
}

void APlayerCameraManager::StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color, bool bHold, bool bInFadeHUD)
{
	FadeColor = Color;
	FadeAlpha = FVector2(FMath::Clamp(FromAlpha, 0.0f, 1.0f), FMath::Clamp(ToAlpha, 0.0f, 1.0f));
	FadeTime = std::isfinite(Duration) ? FMath::Max(Duration, 0.0f) : 0.0f;
	FadeTimeRemaining = FadeTime;
	bHoldWhenFinished = bHold;
	bFadeHUD = bInFadeHUD;

	if (FadeTime <= 0.0f)
	{
		FadeAmount = FadeAlpha.Y;
		FadeTimeRemaining = 0.0f;
		bEnableFading = bHold;
		return;
	}

	FadeAmount = FadeAlpha.X;
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

void APlayerCameraManager::UpdateViewTarget(float DeltaTime)
{
	if (IsBlending() && !IsUsableCameraTarget(PendingViewTarget.Target))
		ClearPendingViewTarget();

	if (!IsUsableCameraTarget(ViewTarget.Target))
	{
		if (IsBlending())
		{
			// 출발 대상이 사라져도 저장된 POV에서 살아 있는 도착 대상으로 계속 전환한다.
			ViewTarget.Target = nullptr;
			bLockOutgoing = true;
		}
		else
		{
			if (!IsUsableCameraTarget(PCOwner))
			{
				ViewTarget.Target = nullptr;
				bHasValidCameraPOV = false;
				return;
			}
			if (bHasValidCameraPOV) PCOwner->SetFallbackCameraView(BaseCameraPOV);
			AActor* Pawn = PCOwner->GetPawn();
			ViewTarget.Target = IsUsableCameraTarget(Pawn) ? Pawn : PCOwner;
		}
	}

	const uint64 Revision = ViewTargetRevision;
	if (!bLockOutgoing)
	{
		FMinimalViewInfo View = ViewTarget.POV;
		ViewTarget.Target->CalcCamera(DeltaTime, View);
		// CalcCamera 콜백에서 새 요청이 발생하면 그 상태를 덮어쓰지 않는다.
		if (IsPendingKillPending() || Revision != ViewTargetRevision) return;
		if (!IsUsableCameraTarget(ViewTarget.Target))
		{
			bHasValidCameraPOV = false;
			return;
		}
		ViewTarget.POV = View;
	}

	if (IsBlending() && IsUsableCameraTarget(PendingViewTarget.Target))
	{
		FMinimalViewInfo PendingView = PendingViewTarget.POV;
		PendingViewTarget.Target->CalcCamera(DeltaTime, PendingView);
		if (IsPendingKillPending() || Revision != ViewTargetRevision) return;
		if (IsUsableCameraTarget(PendingViewTarget.Target))
		{
			PendingViewTarget.POV = PendingView;
			const float SafeDeltaTime = std::isfinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
			BlendTimeRemaining = FMath::Max(BlendTimeRemaining - SafeDeltaTime, 0.0f);
			float Alpha = FMath::Clamp(1.0f - BlendTimeRemaining / BlendDuration, 0.0f, 1.0f);
			switch (BlendParams.BlendFunction)
			{
			case VTBlend_Cubic: Alpha = Alpha * Alpha * (3.0f - 2.0f * Alpha); break;
			case VTBlend_EaseIn: Alpha = std::pow(Alpha, BlendParams.BlendExp); break;
			case VTBlend_EaseOut: Alpha = 1.0f - std::pow(1.0f - Alpha, BlendParams.BlendExp); break;
			case VTBlend_EaseInOut:
				Alpha = Alpha < 0.5f ? 0.5f * std::pow(2.0f * Alpha, BlendParams.BlendExp)
					: 1.0f - 0.5f * std::pow(2.0f * (1.0f - Alpha), BlendParams.BlendExp); break;
			default: break;
			}
			BaseCameraPOV = BlendCameraViews(ViewTarget.POV, PendingViewTarget.POV, Alpha);
			if (BlendTimeRemaining <= 0.0f)
			{
				ViewTarget = PendingViewTarget;
				ClearPendingViewTarget();
			}
			bHasValidCameraPOV = true;
			return;
		}
	}

	// 도착 대상이 갱신 도중 사라졌다면 전환을 취소한다.
	ClearPendingViewTarget();
	BaseCameraPOV = ViewTarget.POV;
	bHasValidCameraPOV = true;
}

void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// 적용 중 추가된 Modifier는 다음 갱신부터 실행한다.
	const auto Modifiers = ModifierList;
	for (UCameraModifier* Modifier : Modifiers)
	{
		if (IsPendingKillPending()) break;
		if (ModifierList.Find(Modifier) == INDEX_NONE || !IsValid(Modifier) || Modifier->IsDisabled()) continue;
		if (Modifier->ModifyCamera(DeltaTime, InOutPOV)) break;
	}
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
