#pragma once

#include "GameFramework/Actor.h"

class UStaticMeshComponent;
class UCapsuleComponent;
class UMaterial;
class UPrimitiveComponent;

class AGhostActor : public AActor
{
	DECLARE_CLASS(AGhostActor, AActor)
	REFLECT_START(AGhostActor)
		PROPERTY(BobbingSpeed)
		PROPERTY(BobbingHeight)
		PROPERTY(BodyRotationSpeed)
	REFLECT_END()

public:
	AGhostActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void ChangeEye();
	void RestoreEye();

	UStaticMeshComponent* GetBodyMesh() const { return BodyMesh; }
	UStaticMeshComponent* GetEyesMesh() const { return EyesMesh; }
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }

private:
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, UPrimitiveComponent* OtherComponent);
	UCapsuleComponent* CapsuleComponent = nullptr;
	UStaticMeshComponent* BodyMesh = nullptr;
	UStaticMeshComponent* EyesMesh = nullptr;
	UMaterial* PreviousEyeOverride = nullptr;
	UMaterial* PreviousPupilOverride = nullptr;
	UMaterial* ChangedEyeOverride = nullptr;
	UMaterial* ChangedPupilOverride = nullptr;
	int32 EyeSlot = -1;
	int32 PupilSlot = -1;
	bool bEyeChanged = false;
	FVector InitialLocation = FVector::ZeroVector;
	float RunningTime = 0.0f;
	float BobbingSpeed = 2.0f;
	float BobbingHeight = 0.25f;
	float BodyRotationSpeed = 30.0f;
};
