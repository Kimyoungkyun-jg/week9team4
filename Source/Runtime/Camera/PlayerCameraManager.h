#pragma once

#include "GameFramework/Actor.h"
#include "Camera/MinimalViewInfo.h"
#include "Math/Color.h"
#include "Math/Vector2.h"
#include "Containers/Array.h"
#include "Core/NameTypes.h"

class APlayerController;
class UCameraModifier;

// 카메라 시점 계산의 기준 대상 정보 및 블렌드 지원
struct FViewTarget
{
	AActor* Target = nullptr;
	FMinimalViewInfo POV;
};

// 시점 대상, 카메라 보정 효과, 화면 페이드/레터박스 상태를 관리하는 액터
class APlayerCameraManager : public AActor
{
	DECLARE_CLASS(APlayerCameraManager, AActor)
	REFLECT_START(ClassName)
	REFLECT_END()

public:
	APlayerCameraManager();
	~APlayerCameraManager() override = default;

	void Initialize(APlayerController* InOwner);
	void SetViewTarget(AActor* InTarget);
	AActor* GetViewTarget() const;
	APlayerController* GetOwningPlayerController() const { return PCOwner; }
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);
	void RemoveCameraModifier(UCameraModifier* Modifier);
	void ClearCameraModifiers();
	void UpdateCamera(float DeltaTime);
	const FMinimalViewInfo* GetCameraCachePOV() const { return bHasCameraCache ? &CameraCachePOV : nullptr; }
	void EndPlay() override;

	// 페이드 기능
	void StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color, bool bHold = false, bool bInFadeHUD = true);
	void StopCameraFade();
	bool HasActiveFade() const { return bEnableFading && FadeAmount > 0.0f; }
	float GetFadeAmount() const { return FadeAmount; }
	const FLinearColor& GetFadeColor() const { return FadeColor; }
	bool IsFadeHUD() const { return bFadeHUD; }

	void FadeIn(float Duration = 1.0f, const FLinearColor& Color = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), bool bInFadeHUD = true);
	void FadeOut(float Duration = 1.0f, const FLinearColor& Color = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f), bool bHold = true, bool bInFadeHUD = true);

	// 레터박스 기능
	void EnterLetterbox(float Duration = 0.5f, float TargetRatio = 0.12f);
	void ExitLetterbox(float Duration = 0.5f);
	void StartLetterbox(float FromRatio, float ToRatio, float Duration);
	void StopLetterbox();
	float GetLetterboxRatio() const { return CurrentLetterboxRatio; }

protected:
	void UpdateCameraFade(float DeltaTime);
	void UpdateCameraLetterbox(float DeltaTime);

	APlayerController* PCOwner = nullptr;
	AActor* ViewTarget = nullptr;
	TArray<UCameraModifier*> Modifiers;
	FMinimalViewInfo CameraCachePOV;
	bool bHasCameraCache = false;

	// 페이드 상태
	bool bEnableFading = false;
	bool bHoldWhenFinished = false;
	bool bFadeHUD = true;
	FLinearColor FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	float FadeAmount = 0.0f;
	FVector2 FadeAlpha = FVector2(0.0f, 0.0f);
	float FadeTime = 0.0f;
	float FadeTimeRemaining = 0.0f;

	// 레터박스 상태
	bool bEnableLetterbox = false;
	float CurrentLetterboxRatio = 0.0f;
	FVector2 LetterboxRatio = FVector2(0.0f, 0.0f);
	float LetterboxTime = 0.0f;
	float LetterboxTimeRemaining = 0.0f;
};
