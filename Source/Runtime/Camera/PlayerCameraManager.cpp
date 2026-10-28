#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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
		CameraPOV = ViewTarget.POV;
		ApplyCameraModifiers(DeltaTime, CameraPOV);
	}

	if (!IsPendingKillPending())
	{
		UpdateCameraFade(DeltaTime);
	}
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget)
{
	ViewTarget.Target = NewViewTarget;
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
    if (!IsValid(ViewTarget.Target) || ViewTarget.Target->IsActorBeingDestroyed())
    {
        if (!IsValid(PCOwner) || PCOwner->IsActorBeingDestroyed())
        {
            SetViewTarget(nullptr);
            bHasValidCameraPOV = false;
            return;
        }

        // 사라진 Actor를 다시 조회하지 않고,
        // 이미 계산된 시점을 Controller에 보관한다.
        if (bHasValidCameraPOV)
        {
            PCOwner->SetFallbackCameraView(ViewTarget.POV);
        }

        SetViewTarget(PCOwner);
    }

    ViewTarget.Target->CalcCamera(DeltaTime, ViewTarget.POV);
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
