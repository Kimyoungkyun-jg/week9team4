#include "EnginePCH.h"
#include "Camera/CameraModifier.h"
#include "Camera/PlayerCameraManager.h"

bool UCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    UpdateAlpha(DeltaTime);

    if (bPendingDisable && Alpha <= 0.0f)
    {
        DisableModifier(true);
    }

	return false;
}

void UCameraModifier::EnableModifier()
{
	bDisabled = 0;
	bPendingDisable = 0;

}

void UCameraModifier::DisableModifier(bool bImmediate)
{
    if (bImmediate || AlphaOutTime <= 0.0f)
    {
        Alpha = 0.0f;
        bDisabled = 1;
        bPendingDisable = 0;
        return;
    }

    if (!bDisabled)
    {
        bPendingDisable = 1;
    }
}

AActor* UCameraModifier::GetViewTarget() const
{
	return IsValid(CameraOwner) && !CameraOwner->IsActorBeingDestroyed()
		? CameraOwner->GetViewTarget() : nullptr;
}

void UCameraModifier::UpdateAlpha(float DeltaTime)
{
    const float TargetAlpha = bPendingDisable ? 0.0f : 1.0f;
    const float BlendTime = bPendingDisable ? AlphaOutTime : AlphaInTime;

    if (BlendTime <= 0.0f)
    {
        Alpha = TargetAlpha;
        return;
    }

    const float SafeDeltaTime = DeltaTime > 0.0f ? DeltaTime : 0.0f;
    const float Step = SafeDeltaTime / BlendTime;
    const float Direction = bPendingDisable ? -1.0f : 1.0f;

    Alpha = FMath::Clamp(Alpha + Direction * Step, 0.0f, 1.0f);
}
