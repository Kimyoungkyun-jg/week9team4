#pragma once

#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Math/Color.h"
#include "Math/Vector2.h"
#include "Math/EngineMath.h"
#include "Math/Rotator.h"
#include "CameraTypes.h"

// ToDO: UCameraModifier 구현
class UCameraModifier;
class APlayerController;

// 카메라 시점 계산의 기준 대상 정보.
// A ViewTarget is the primary actor the camera is associated with.
struct FViewTarget
{
	AActor* Target = nullptr;
	FMinimalViewInfo POV;

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

	void InitializeFor(APlayerController* InController);
	APlayerController* GetOwningPlayerController() const { return PCOwner; }

	// 외부에서 매 프레임 호출할 카메라 갱신 진입점.
	void UpdateCamera(float DeltaTime);
	void SetViewTarget(AActor* NewViewTarget);
	void StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color);
	void StopCameraFade();

	AActor* GetViewTarget() const { return ViewTarget.Target; }
	const FMinimalViewInfo& GetCameraPOV() const { return ViewTarget.POV; }
	bool HasValidCameraPOV() const { return bHasValidCameraPOV; }

protected:
	void UpdateViewTarget(float DeltaTime);
	void ApplyCameraModifiers(float DeltaTime);
	void UpdateCameraFade(float DeltaTime);

	FLinearColor FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	float FadeAmount = 0.0f;
	// 페이드 시작 알파 값, 목표 알파 값
	FVector2 FadeAlpha = FVector2(0.0f, 0.0f);
	float FadeTime = 0.0f;
	float FadeTimeRemaining = 0.0f;

	// 카메라 동작 방식과 현재 시점 대상.
	FName CameraStyle = FName("Default");
	FViewTarget ViewTarget;

	APlayerController* PCOwner = nullptr;

	// TODO: Modifier의 추가/제거 및 수명 관리 방식을 정한다.
	TArray<UCameraModifier*> ModifierList;
	bool bHasValidCameraPOV = false;
};
