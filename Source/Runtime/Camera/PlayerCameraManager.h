#pragma once

#include "GameFramework/Actor.h"
#include "Camera/MinimalViewInfo.h"

class APlayerController;
class UCameraModifier;

class APlayerCameraManager : public AActor
{
	DECLARE_CLASS(APlayerCameraManager, AActor)
	REFLECT_START(ClassName)
	REFLECT_END()
public:
	void Initialize(APlayerController* InOwner);
	void SetViewTarget(AActor* InTarget);
	AActor* GetViewTarget() const;
	APlayerController* GetOwningPlayerController() const { return PCOwner; }
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);
	void RemoveCameraModifier(UCameraModifier* Modifier);
	void UpdateCamera(float DeltaTime);
	const FMinimalViewInfo* GetCameraCachePOV() const { return bHasCameraCache ? &CameraCachePOV : nullptr; }
	void EndPlay() override;

private:
	void ClearCameraModifiers();
	APlayerController* PCOwner = nullptr;
	AActor* ViewTarget = nullptr;
	TArray<UCameraModifier*> Modifiers;
	FMinimalViewInfo CameraCachePOV;
	bool bHasCameraCache = false;
};
