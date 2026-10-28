#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"

APlayerCameraManager::APlayerCameraManager() = default;

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	// TODO: 시점 계산, Modifier 적용, 페이드 갱신을 순서대로 호출한다.
	UpdateCameraFade(DeltaTime);
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget)
{
	// TODO: 새 대상을 ViewTarget에 지정하고 필요한 전환 상태를 준비한다.
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

void APlayerCameraManager::UpdateViewTarget(float DeltaTime)
{
	// TODO: 대상의 카메라 정보를 바탕으로 위치, 회전, FOV를 계산한다.
}

void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime)
{
	// TODO: ModifierList의 보정 효과를 계산된 시점에 적용한다.
}

void APlayerCameraManager::UpdateCameraFade(float DeltaTime)
{
	// TODO: 남은 시간을 줄이고 시작/종료 알파 사이의 FadeAmount를 보간한다.
	// TODO: 계산된 색상과 알파를 화면 렌더링에 전달한다.
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
