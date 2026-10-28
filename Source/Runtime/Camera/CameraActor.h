#pragma once

#include "../GameFramework/Actor.h"
#include "UObject/Class.h"

#include "CameraComponent.h"

//class UCameraComponent;

class ACameraActor : public AActor
{
	DECLARE_CLASS(ACameraActor, AActor)

public:
	// 카메라 컴포넌트를 기본 서브오브젝트로 생성해 루트에 연결
	ACameraActor();
	void BeginPlay() override;

	UCameraComponent* GetCameraComponent() const { return CameraComponent; };

private:
	UCameraComponent* CameraComponent;
};
