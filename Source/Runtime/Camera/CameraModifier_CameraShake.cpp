#include "EnginePCH.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Math/Quat.h"
#include <cmath>

FCameraShakeHandle UCameraModifier_CameraShake::AddCameraShake(const FCameraShakeSettings& Settings, float Scale)
{
	if (!Settings.IsValid() || !std::isfinite(Scale) || Scale <= 0.0f) return {};
	// Modifier 제거/재생성이나 다른 Manager에서도 이전 핸들과 충돌하지 않는다.
	static uint64 NextId = 1;
	if (NextId == 0) return {};
	FCameraShakeHandle Handle;
	Handle.Id = NextId++;
	ActiveShakes.Add({ Handle, FCameraShakeInstance(Settings, Scale) });
	EnableModifier();
	return Handle;
}

bool UCameraModifier_CameraShake::StopCameraShake(FCameraShakeHandle Handle, bool bImmediately)
{
	for (int32 Index = 0; Index < ActiveShakes.Num(); ++Index)
	{
		auto& Shake = ActiveShakes[Index];
		if (!(Shake.Handle == Handle)) continue;
		Shake.Instance.Stop(bImmediately);
		if (Shake.Instance.IsFinished()) ActiveShakes.RemoveAt(Index);
		return true;
	}
	return false;
}

void UCameraModifier_CameraShake::StopAllCameraShakes(bool bImmediately)
{
	for (int32 Index = static_cast<int32>(ActiveShakes.Num()) - 1; Index >= 0; --Index)
	{
		ActiveShakes[Index].Instance.Stop(bImmediately);
		if (ActiveShakes[Index].Instance.IsFinished()) ActiveShakes.RemoveAt(Index);
	}
}

bool UCameraModifier_CameraShake::IsCameraShakeActive(FCameraShakeHandle Handle) const
{
	for (const auto& Shake : ActiveShakes)
		if (Shake.Handle == Handle) return true;
	return false;
}

bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const float SafeDeltaTime = std::isfinite(DeltaTime) && DeltaTime > 0.0f ? DeltaTime : 0.0f;
	Super::ModifyCamera(SafeDeltaTime, InOutPOV);
	FCameraShakeOffset Total;
	for (int32 Index = 0; Index < ActiveShakes.Num();)
	{
		auto& Shake = ActiveShakes[Index];
		FCameraShakeOffset Offset;
		Shake.Instance.Update(SafeDeltaTime, Offset);
		Total.Location += Offset.Location;
		for (int32 Axis = 0; Axis < 3; ++Axis) Total.Rotation[Axis] += Offset.Rotation[Axis];
		Total.FOV += Offset.FOV;
		if (Shake.Instance.IsFinished()) ActiveShakes.RemoveAt(Index);
		else ++Index;
	}

	// 모든 Shake를 같은 입력 카메라의 로컬 공간에서 합성한 뒤 POV에 한 번 적용한다.
	const FQuat CameraRotation = InOutPOV.Rotation.Quaternion();
	InOutPOV.Location += CameraRotation.RotateVector(Total.Location * GetAlpha());
	if (Total.Rotation.Pitch != 0.0f || Total.Rotation.Yaw != 0.0f || Total.Rotation.Roll != 0.0f)
	{
		InOutPOV.Rotation = (CameraRotation * (Total.Rotation * GetAlpha()).Quaternion()).Normalized().ToFRotator();
	}
	if (Total.FOV != 0.0f)
		InOutPOV.FOV = FMath::Clamp(InOutPOV.FOV + Total.FOV * GetAlpha(), 1.0f, 179.0f);
	return false;
}
