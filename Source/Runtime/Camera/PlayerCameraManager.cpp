#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

void APlayerCameraManager::InitializeFor(APlayerController* InController)
{
	PCOwner = InController;
	bHasValidCameraPOV = false;
}

// Performs per-tick camera update.
// Called once per tick after all other actors have been ticked.(출처: 언리얼)
void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	// TODO: 시점 계산, Modifier 적용, 페이드 갱신을 순서대로 호출한다.

	UpdateViewTarget(DeltaTime);
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget)
{
	ViewTarget.Target = NewViewTarget;
}

void APlayerCameraManager::StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color)
{
	// TODO: 페이드 색상, 시작/종료 알파, 전체 시간과 남은 시간을 설정한다.
}

void APlayerCameraManager::StopCameraFade()
{
	// TODO: 진행 중인 페이드를 중단하고 페이드 상태를 초기화한다.
}

void APlayerCameraManager::UpdateViewTarget(float DeltaTime)
{
    if (!IsValid(ViewTarget.Target) || ViewTarget.Target->IsActorBeingDestroyed())
    {
        if (!IsValid(PCOwner) || PCOwner->IsActorBeingDestroyed())
        {
            SetViewTarget(nullptr);
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

void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime)
{
	// TODO: ModifierList의 보정 효과를 계산된 시점에 적용한다.
}

void APlayerCameraManager::UpdateCameraFade(float DeltaTime)
{
	// TODO: 남은 시간을 줄이고 시작/종료 알파 사이의 FadeAmount를 보간한다.
	// TODO: 계산된 색상과 알파를 화면 렌더링에 전달한다.
}
