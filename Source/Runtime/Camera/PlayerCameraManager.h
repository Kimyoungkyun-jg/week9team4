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
#include "Camera/CameraShake.h"

class UCameraModifier;
class APlayerController;

enum EViewTargetBlendFunction
{
	VTBlend_Linear,
	VTBlend_Cubic,
	VTBlend_EaseIn,
	VTBlend_EaseOut,
	VTBlend_EaseInOut,
	VTBlend_MAX
};

// 시점 전환 보간 매개변수
struct FViewTargetTransitionParams
{
	float BlendTime = 0.0f;
	EViewTargetBlendFunction BlendFunction = VTBlend_Cubic;
	float BlendExp = 2.0f;
	bool bLockOutgoing = false;
};

// 카메라 시점 계산의 기준 대상 정보.
// A ViewTarget is the primary actor the camera is associated with.
struct FViewTarget
{
	AActor* Target = nullptr;
	FMinimalViewInfo POV;
};

// 시점 대상, 카메라 보정 효과, 화면 페이드 상태를 관리하는 액터
class APlayerCameraManager : public AActor
{
	DECLARE_CLASS(APlayerCameraManager, AActor)

public:
	APlayerCameraManager();
	~APlayerCameraManager() override;
	void EndPlay() override;

	APlayerCameraManager(const APlayerCameraManager&) = delete;
	APlayerCameraManager& operator=(const APlayerCameraManager&) = delete;

	APlayerCameraManager(APlayerCameraManager&&) = delete;
	APlayerCameraManager& operator=(APlayerCameraManager&&) = delete;

	void InitializeFor(APlayerController* InController);
	void Initialize(APlayerController* InController) { InitializeFor(InController); }
	APlayerController* GetOwningPlayerController() const { return PCOwner; }

	// 외부에서 매 프레임 호출할 카메라 갱신 진입점.
	void UpdateCamera(float DeltaTime);
	// The short form retains the gameplay linear-blend contract.
	void SetViewTarget(AActor* NewViewTarget, float BlendTime = 0.0f);
	void SetViewTargetWithBlend(AActor* NewTarget, float BlendTime = 0.0f, EViewTargetBlendFunction BlendFunc = VTBlend_Cubic, float BlendExp = 2.0f, bool bLockOutgoing = false);
	bool HasActiveViewTargetBlend() const { return IsBlending(); }
	const FMinimalViewInfo* GetCameraCachePOV() const { return bHasValidCameraPOV ? &CameraPOV : nullptr; }

	// 반환된 Modifier는 Manager 소유이며 제거 후에는 사용하면 안 된다.
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);
	UCameraModifier* FindCameraModifier(UClass* ModifierClass) const;
	bool RemoveCameraModifier(UCameraModifier* Modifier);
	void RemoveAllCameraModifiers();
	void ClearCameraModifiers() { RemoveAllCameraModifiers(); }

	// 필요한 경우 Shake Modifier를 생성한다. 잘못된 설정은 빈 핸들을 반환한다.
	FCameraShakeHandle StartCameraShake(const FCameraShakeSettings& Settings = FCameraShakeSettings{}, float Scale = 1.0f);
	bool StopCameraShake(FCameraShakeHandle Handle, bool bImmediately = false);
	void StopAllCameraShakes(bool bImmediately = false);
	bool IsCameraShakeActive(FCameraShakeHandle Handle) const;
	int32 GetActiveCameraShakeCount() const;

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

	AActor* GetViewTarget() const;
	bool IsViewTarget(const AActor* Target) const { return ViewTarget.Target == Target; }
	AActor* GetPendingViewTarget() const;
	bool IsBlending() const { return BlendTimeRemaining > 0.0f; }
	const FMinimalViewInfo& GetCameraPOV() const { return CameraPOV; }
	// 블렌딩 결과를 Modifier와 분리해 대체 시점이나 다음 전환에 효과가 재누적되지 않게 한다.
	const FMinimalViewInfo& GetBaseCameraPOV() const { return BaseCameraPOV; }
	bool HasValidCameraPOV() const { return bHasValidCameraPOV; }

protected:
	void UpdateViewTarget(float DeltaTime);
	void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);
	void UpdateCameraFade(float DeltaTime);
	void UpdateCameraLetterbox(float DeltaTime);

	bool bEnableFading = false;
	bool bHoldWhenFinished = false;
	bool bFadeHUD = true;
	FLinearColor FadeColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	float FadeAmount = 0.0f;
	// 페이드 시작 알파 값, 목표 알파 값
	FVector2 FadeAlpha = FVector2(0.0f, 0.0f);
	float FadeTime = 0.0f;
	float FadeTimeRemaining = 0.0f;

	// 카메라 동작 방식과 현재 시점 대상.
	FName CameraStyle = FName("Default");
	FViewTarget ViewTarget;
	FViewTarget PendingViewTarget;
	FViewTargetTransitionParams BlendParams;
	float BlendDuration = 0.0f;
	float BlendTimeRemaining = 0.0f;

	APlayerController* PCOwner = nullptr;

	// 레터박스 상태
	bool bEnableLetterbox = false;
	float CurrentLetterboxRatio = 0.0f;
	FVector2 LetterboxRatio = FVector2(0.0f, 0.0f);
	float LetterboxTime = 0.0f;
	float LetterboxTimeRemaining = 0.0f;

private:
	void FlushRemovedCameraModifiers();
	void ClearPendingViewTarget();

	TArray<UCameraModifier*> ModifierList;
	TArray<UCameraModifier*> RemovedModifiers;
	FMinimalViewInfo BaseCameraPOV;
	FMinimalViewInfo CameraPOV;
	// 전환 중 재요청하거나 출발 대상이 사라지면 마지막 출발 POV를 고정한다.
	bool bLockOutgoing = false;
	uint64 ViewTargetRevision = 0;
	bool bHasValidCameraPOV = false;
	bool bIsUpdatingCamera = false;
	bool bIsClearingModifiers = false;
};
