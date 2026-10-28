#pragma once

#include "../GameFramework/Actor.h"
#include "UObject/Class.h"

#include "CameraComponent.h"

class UStaticMeshComponent;

//class UCameraComponent;

class ACameraActor : public AActor
{
	DECLARE_CLASS(ACameraActor, AActor)

public:
	// 카메라 컴포넌트를 기본 서브오브젝트로 생성해 루트에 연결
	ACameraActor();
	void BeginPlay() override;
	void Serialize(FStructuredArchive::FRecord Record) override;

	UCameraComponent* GetCameraComponent() const { return CameraComponent; };
	bool ShouldHideEditorMeshFromView(const UStaticMeshComponent* Mesh, const FVector& ViewLocation) const;

private:
	UCameraComponent* CameraComponent;
};
