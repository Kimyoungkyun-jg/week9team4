#pragma once

#include "Components/SceneComponent.h"
#include "Collision/CollisionShape.h"

// A query-only camera boom. The owning actor supplies direction and activation.
class USpringArmComponent : public USceneComponent
{
	DECLARE_CLASS(USpringArmComponent, USceneComponent)
	REFLECT_START(USpringArmComponent)
		PROPERTY(TargetArmLength)
		PROPERTY(ProbeSize)
		PROPERTY_TYPE(ProbeChannel, Int)
		PROPERTY(bDoCollisionTest)
		PROPERTY(TargetOffset)
		PROPERTY(SocketOffset)
		PROPERTY(bEnableCameraLag)
		PROPERTY(CameraLagSpeed)
		PROPERTY(CameraLagMaxDistance)
		PROPERTY(bEnableCameraRotationLag)
		PROPERTY(CameraRotationLagSpeed)
		PROPERTY_FLAGS(ArmEndComponentName, CPF_None)
	REFLECT_END()
public:
	USpringArmComponent();
	float TargetArmLength = 3.0f;
	float ProbeSize = 0.15f;
	ECollisionChannel ProbeChannel = ECollisionChannel::Camera;
	bool bDoCollisionTest = true;
	// Origin offset in world space; endpoint offset in the arm's view space.
	// Keep CameraComponent's own relative transform neutral so the sweep covers it.
	FVector TargetOffset = FVector::ZeroVector;
	FVector SocketOffset = FVector::ZeroVector;
	// Lag follows the target BEFORE collision. Collision correction is immediate
	// and never feeds back into this history. Zero speed means no lag.
	bool bEnableCameraLag = false;
	float CameraLagSpeed = 10.0f;
	float CameraLagMaxDistance = 0.0f; // Zero means unlimited.
	bool bEnableCameraRotationLag = false;
	float CameraRotationLagSpeed = 10.0f;

	void SetArmEnd(USceneComponent* Component);
	USceneComponent* GetArmEnd() const;
	void Activate(bool bReset = false) override;
	void Deactivate() override;
	void TickComponent(float DeltaTime) override;
	// Zero/invalid delta still refreshes collision, without advancing enabled lag.
	void UpdateArm(float DeltaTime = 0.0f);
	// Also invalidates lag history for mode changes and respawn/teleport.
	void ResetArmEnd();
	FVector GetUnfixedCameraPosition() const { return UnfixedCameraPosition; }
	bool IsCollisionFixApplied() const { return bIsCameraFixed; }

protected:
	// Same extension point as UE: default collision response uses the safe hit
	// location immediately. A subclass can supply a different camera policy.
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation,
		bool bHitSomething, float DeltaTime);

private:
	// Resolve by owner-local name so scene loading and PIE duplication never retain
	// a pointer into the source actor. This is a single endpoint, not a socket system.
	FString ArmEndComponentName;
	FVector PreviousDesiredLocation = FVector::ZeroVector;
	FRotator PreviousDesiredRotation;
	FVector UnfixedCameraPosition = FVector::ZeroVector;
	bool bHasValidLagState = false;
	bool bIsCameraFixed = false;
};
