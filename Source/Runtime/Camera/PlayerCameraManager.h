#pragma once

#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Math/Color.h"
#include "Math/Vector2.h"

// ToDO: UCameraModifier 구현
class UCameraModifier;

// 카메라 시점 계산의 기준 대상 정보.
struct FViewTarget
{
	AActor* Target = nullptr;

	// TODO: 최종 시점의 위치, 회전, FOV 등을 담을 정보를 추가한다.
};

// 시점 대상, 카메라 보정 효과, 화면 페이드 상태를 관리하는 액터
class APlayerCameraManager : public AActor
{
	DECLARE_CLASS(APlayerCameraManager, AActor)

public:
	APlayerCameraManager() = default;
	~APlayerCameraManager() override = default;

	APlayerCameraManager(const APlayerCameraManager&) = delete;
	APlayerCameraManager& operator=(const APlayerCameraManager&) = delete;

	APlayerCameraManager(APlayerCameraManager&&) = delete;
	APlayerCameraManager& operator=(APlayerCameraManager&&) = delete;

	// 외부에서 매 프레임 호출할 카메라 갱신 진입점.
	// PlayerController::BeginPlay에서 호출. (추후 Controller 소유자 보관 등 초기화 확장용 스텁)
	void Initialize(class APlayerController* InPC) { (void)InPC; }
	void UpdateCamera(float DeltaTime);
	void SetViewTarget(AActor* NewViewTarget);
	// 현재 설정된 시점 대상 액터 반환
	AActor* GetViewTarget() const { return ViewTarget.Target; }
	// GameViewportClient에서 호출: 현재는 nullptr를 반환하여 폰의 기본 CameraComponent로 Fallback되도록 처리
	// (추후 카메라 캐시 시스템 완성 시 캐싱된 POV를 반환하도록 구현 필요)
	const struct FMinimalViewInfo* GetCameraCachePOV() const { return nullptr; }
	void StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color, bool bHold = false, bool bInFadeHUD = true);
	void StopCameraFade();

	bool HasActiveFade() const { return bEnableFading && FadeAmount > 0.0f; }
	float GetFadeAmount() const { return FadeAmount; }
	const FLinearColor& GetFadeColor() const { return FadeColor; }
	bool IsFadeHUD() const { return bFadeHUD; }

	void FadeIn(float Duration = 1.0f, const FLinearColor& Color = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), bool bInFadeHUD = true);
	void FadeOut(float Duration = 1.0f, const FLinearColor& Color = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), bool bHold = true, bool bInFadeHUD = true);

protected:
	void UpdateViewTarget(float DeltaTime);
	void ApplyCameraModifiers(float DeltaTime);
	void UpdateCameraFade(float DeltaTime);

	bool bEnableFading = false;
	bool bHoldWhenFinished = false; // 페이드 완료 후 그 상태를 유지할 지 여부
	bool bFadeHUD = true;           // true면 UI까지 페이드, false면 3D 월드만 페이드

	FLinearColor FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	float FadeAmount = 0.0f;
	// 페이드 시작 알파 값, 목표 알파 값
	FVector2 FadeAlpha = FVector2(0.0f, 0.0f);
	float FadeTime = 0.0f;
	float FadeTimeRemaining = 0.0f;

	// 카메라 동작 방식과 현재 시점 대상.
	FName CameraStyle = FName("Default");
	FViewTarget ViewTarget;

	// TODO: Modifier의 추가/제거 및 수명 관리 방식을 정한다.
	TArray<UCameraModifier*> ModifierList;
};
