#pragma once

#include "Camera/CameraModifier.h"
#include "Camera/CameraShake.h"
#include "Containers/Array.h"

// 여러 Shake의 수명과 합성을 담당한다. CameraComponent의 Transform은 변경하지 않는다.
class UCameraModifier_CameraShake : public UCameraModifier
{
	DECLARE_CLASS(UCameraModifier_CameraShake, UCameraModifier)

public:
	FCameraShakeHandle AddCameraShake(const FCameraShakeSettings& Settings, float Scale = 1.0f);
	bool StopCameraShake(FCameraShakeHandle Handle, bool bImmediately = false);
	void StopAllCameraShakes(bool bImmediately = false);
	bool IsCameraShakeActive(FCameraShakeHandle Handle) const;
	int32 GetActiveCameraShakeCount() const { return ActiveShakes.Num(); }
	bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

private:
	struct FActiveCameraShake
	{
		FCameraShakeHandle Handle;
		FCameraShakeInstance Instance;
	};
	TArray<FActiveCameraShake> ActiveShakes;
};
