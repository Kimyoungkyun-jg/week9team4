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

class UCameraModifier;
class APlayerController;

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
	APlayerCameraManager() = default;
	~APlayerCameraManager() override;
	void EndPlay() override;

	APlayerCameraManager(const APlayerCameraManager&) = delete;
	APlayerCameraManager& operator=(const APlayerCameraManager&) = delete;

	APlayerCameraManager(APlayerCameraManager&&) = delete;
	APlayerCameraManager& operator=(APlayerCameraManager&&) = delete;

	void InitializeFor(APlayerController* InController);
	APlayerController* GetOwningPlayerController() const { return PCOwner; }

	// 외부에서 매 프레임 호출할 카메라 갱신 진입점.
	void UpdateCamera(float DeltaTime);
	// BlendTime이 0 이하이면 즉시 전환하고, 양수이면 다음 대상까지 선형 블렌딩한다.
	void SetViewTarget(AActor* NewViewTarget, float BlendTime = 0.0f);

	// 반환된 Modifier는 Manager 소유이며 제거 후에는 사용하면 안 된다.
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);
	UCameraModifier* FindCameraModifier(UClass* ModifierClass) const;
	bool RemoveCameraModifier(UCameraModifier* Modifier);
	void RemoveAllCameraModifiers();

	void StartCameraFade(float FromAlpha, float ToAlpha, float Duration, const FLinearColor& Color);
	void StopCameraFade();

	AActor* GetViewTarget() const { return ViewTarget.Target; }
	AActor* GetPendingViewTarget() const { return PendingViewTarget.Target; }
	bool IsBlending() const { return BlendTimeRemaining > 0.0f; }
	const FMinimalViewInfo& GetCameraPOV() const { return CameraPOV; }
	// 블렌딩 결과를 Modifier와 분리해 대체 시점이나 다음 전환에 효과가 재누적되지 않게 한다.
	const FMinimalViewInfo& GetBaseCameraPOV() const { return BaseCameraPOV; }
	bool HasValidCameraPOV() const { return bHasValidCameraPOV; }
	float GetFadeAmount() const { return FadeAmount; }
	const FLinearColor& GetFadeColor() const { return FadeColor; }

protected:
	void UpdateViewTarget(float DeltaTime);
	void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);
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
	FViewTarget PendingViewTarget;
	float BlendDuration = 0.0f;
	float BlendTimeRemaining = 0.0f;

	APlayerController* PCOwner = nullptr;

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
