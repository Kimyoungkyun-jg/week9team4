#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	// TODO: 시점 계산, Modifier 적용, 페이드 갱신을 순서대로 호출한다.
}

void APlayerCameraManager::SetViewTarget(AActor* NewViewTarget)
{
	// TODO: 새 대상을 ViewTarget에 지정하고 필요한 전환 상태를 준비한다.
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
}
