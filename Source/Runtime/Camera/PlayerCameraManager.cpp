#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	bool IsUsableCameraTarget(AActor* Target)
	{
		// 프레임 사이에 실제 삭제된 Actor도 역참조하지 않는다.
		return Target && GUObjectArray.Find(Target) != INDEX_NONE &&
			IsValid(Target) && !Target->IsActorBeingDestroyed();
	}

	FMinimalViewInfo BlendCameraViews(const FMinimalViewInfo& From, const FMinimalViewInfo& To, float Alpha)
	{
		if (Alpha <= 0.0f) return From;
		if (Alpha >= 1.0f) return To;

		auto Blend = [Alpha](float A, float B) { return A + (B - A) * Alpha; };
		FMinimalViewInfo Result = From;
		Result.Location = From.Location + (To.Location - From.Location) * Alpha;
		// 각 축을 [-180, 180] 범위의 차이로 보간해 179도 -> -179도에서 크게 회전하지 않는다.
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Result.Rotation[Axis] = From.Rotation[Axis] +
				std::remainder(To.Rotation[Axis] - From.Rotation[Axis], 360.0f) * Alpha;
		Result.FOV = Blend(From.FOV, To.FOV);
		Result.AspectRatio = Blend(From.AspectRatio, To.AspectRatio);
		Result.NearClipPlane = Blend(From.NearClipPlane, To.NearClipPlane);
		Result.FarClipPlane = Blend(From.FarClipPlane, To.FarClipPlane);
		Result.OrthoWidth = Blend(From.OrthoWidth, To.OrthoWidth);
		// 투영 방식은 수치 보간할 수 없으므로 전환 완료 시 도착 대상의 방식을 채택한다.
		return Result;
	}
}

APlayerCameraManager::~APlayerCameraManager()
{
	RemoveAllCameraModifiers();
	FlushRemovedCameraModifiers();
}

void APlayerCameraManager::EndPlay()
{
	RemoveAllCameraModifiers();
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
		UpdateCameraFade(DeltaTime);
	}
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget, float BlendTime)
{
	if (IsPendingKillPending()) return;
	if (!std::isfinite(BlendTime) || BlendTime <= 0.0f ||
		!bHasValidCameraPOV || !IsUsableCameraTarget(NewViewTarget))
	{
		++ViewTargetRevision;
		ClearPendingViewTarget();
		ViewTarget.Target = NewViewTarget;
		return;
	}

	// 매 프레임 같은 요청이 들어와도 타이머를 처음으로 되돌리지 않는다.
	if ((IsBlending() && PendingViewTarget.Target == NewViewTarget) ||
		(!IsBlending() && ViewTarget.Target == NewViewTarget)) return;

	++ViewTargetRevision;
	if (IsBlending())
	{
		// 전환 중의 블렌딩 결과에서 다시 출발한다. Modifier 적용 후의 POV는 사용하지 않는다.
		ViewTarget.POV = BaseCameraPOV;
		bLockOutgoing = true;
	}
	PendingViewTarget.Target = NewViewTarget;
	PendingViewTarget.POV = BaseCameraPOV;
	BlendDuration = BlendTime;
	BlendTimeRemaining = BlendTime;
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

void APlayerCameraManager::StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color)
{
	FadeColor = Color;
	FadeAlpha = FVector2(FMath::Clamp(FromAlpha, 0.0f, 1.0f), FMath::Clamp(ToAlpha, 0.0f, 1.0f));

	FadeTime = FMath::Max(Duration, 0.0f);
	FadeTimeRemaining = FadeTime;

	// 시간이 0 이하라면 즉시 목표 알파를 적용한다.
	FadeAmount = FadeTime > 0.0f ? FadeAlpha.X : FadeAlpha.Y;
}

void APlayerCameraManager::StopCameraFade()
{
	FadeAmount = 0.0f;
	FadeAlpha = FVector2(0.0f, 0.0f);
	FadeTime = 0.0f;
	FadeTimeRemaining = 0.0f;
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
			ViewTarget.Target = PCOwner;
		}
	}

	const uint64 Revision = ViewTargetRevision;
	if (!bLockOutgoing)
	{
		FMinimalViewInfo View = ViewTarget.POV;
		ViewTarget.Target->CalcCamera(DeltaTime, View);
		// CalcCamera 콜백에서 새 요청이 발생하면 그 상태를 덮어쓰지 않는다.
		if (IsPendingKillPending() || Revision != ViewTargetRevision) return;
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
			const float Alpha = FMath::Clamp(1.0f - BlendTimeRemaining / BlendDuration, 0.0f, 1.0f);
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
	if (FadeTime <= 0.0f || FadeTimeRemaining <= 0.0f)
	{
		return;
	}

	FadeTimeRemaining = FMath::Max(FadeTimeRemaining - FMath::Max(DeltaTime, 0.0f), 0.0f);

	const float Progress = FMath::Clamp(1.0f - FadeTimeRemaining / FadeTime, 0.0f, 1.0f);

	FadeAmount = FadeAlpha.X + (FadeAlpha.Y - FadeAlpha.X) * Progress;
}
