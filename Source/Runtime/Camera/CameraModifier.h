#pragma once

#include "UObject/Class.h"
#include "CameraTypes.h"

class APlayerCameraManager;
class AActor;

// Manager가 생성하고 소유하는 카메라 보정의 기본 클래스.
class UCameraModifier : public UObject
{
	DECLARE_CLASS(UCameraModifier, UObject)

public:
	// InOutPOV를 직접 보정한다. true를 반환하면 이후 Modifier 적용을 중단한다.
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV);

	void EnableModifier();
	void DisableModifier(bool bImmediate = false);

	bool IsDisabled() const { return bDisabled != 0; }
	bool IsPendingDisable() const { return bPendingDisable != 0; }
	float GetAlpha() const { return Alpha; }

	APlayerCameraManager* GetCameraOwner() const { return CameraOwner; }
	AActor* GetViewTarget() const;
	uint8 GetPriority() const { return Priority; }

protected:
	// 작은 값부터 적용한다.
	uint8 Priority = 127;
	void UpdateAlpha(float DeltaTime);
	// 0이면 블렌딩 없이 즉시 적용한다.
	float AlphaInTime = 0.0f;
	float AlphaOutTime = 0.0f;
	float Alpha = 0.0f;

private:
	friend class APlayerCameraManager;
	APlayerCameraManager* CameraOwner = nullptr;
	uint32 bDisabled = 0;
	uint32 bPendingDisable = 0;
};
