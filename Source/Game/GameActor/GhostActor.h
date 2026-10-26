#pragma once

#include "GameFramework/Actor.h"
#include "Lua/LuaMacros.h"

#include "GhostAIComponent.h"

class UStaticMeshComponent;
class UCapsuleComponent;
class UBoxComponent;
class UMaterial;
class ULuaScriptComponent;
class UPrimitiveComponent;

class AGhostActor : public AActor
{
	DECLARE_CLASS(AGhostActor, AActor)
	REFLECT_START(AGhostActor)
		PROPERTY(BobbingSpeed)
		PROPERTY(BobbingHeight)
		PROPERTY(BodyRotationSpeed)
	REFLECT_END()

	LUA_BIND_START(AGhostActor)
		LUA_FUNC(ChangeEye)
		LUA_FUNC(RestoreEye)
		UserType["IsPlayerInDetectionRange"] = sol::overload(
			[](const AGhostActor& Self) { return Self.IsPlayerInDetectionRange(false); },
			[](const AGhostActor& Self, bool bChase) { return Self.IsPlayerInDetectionRange(bChase); }
		);
		LUA_FUNC(IsPlayerInChaseRange)
		LUA_FUNC(GetAIComponent)
		sol::table ActorTable = lua["AActor"];
		if (ActorTable.valid())
		{
			ActorTable["GetAIComponent"] = [](AActor& Self) -> UGhostAIComponent* {
				if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
					return Ghost->GetAIComponent();
				return Cast<UGhostAIComponent>(Self.GetComponentByClass(UGhostAIComponent::StaticClass()));
			};
			ActorTable["ChangeEye"] = [](AActor& Self) {
				if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
					Ghost->ChangeEye();
			};
			ActorTable["RestoreEye"] = [](AActor& Self) {
				if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
					Ghost->RestoreEye();
			};
			ActorTable["IsPlayerInDetectionRange"] = sol::overload(
				[](AActor& Self) -> bool {
					if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
						return Ghost->IsPlayerInDetectionRange(false);
					return false;
				},
				[](AActor& Self, bool bChase) -> bool {
					if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
						return Ghost->IsPlayerInDetectionRange(bChase);
					return false;
				}
			);
			ActorTable["IsPlayerInChaseRange"] = [](AActor& Self) -> bool {
				if (AGhostActor* Ghost = Cast<AGhostActor>(&Self))
					return Ghost->IsPlayerInChaseRange();
				return false;
			};
		}
	LUA_BIND_END()

public:
	AGhostActor();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	void ChangeEye();
	void RestoreEye();
	void SetHitFrozen(bool bFrozen);
	bool IsHitFrozen() const { return bHitFrozen; }

	void OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComp, UPrimitiveComponent* OtherComp);
	bool IsPlayerInDetectionRange(bool bForceChaseRange = false) const;
	bool IsPlayerInChaseRange() const { return IsPlayerInDetectionRange(true); }

	UGhostAIComponent* GetAIComponent() const { return AIComponent; }
	UStaticMeshComponent* GetBodyMesh() const { return BodyMesh; }
	UStaticMeshComponent* GetEyesMesh() const { return EyesMesh; }
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	UBoxComponent* GetPatrolDetectionBox() const { return PatrolDetectionBox; }
	UBoxComponent* GetChaseDetectionBox() const { return ChaseDetectionBox; }
	ULuaScriptComponent* GetLuaScriptComponent() const { return LuaScriptComponent; }

private:
	UCapsuleComponent* CapsuleComponent = nullptr;
	UBoxComponent* PatrolDetectionBox = nullptr;
	UBoxComponent* ChaseDetectionBox = nullptr;
	UStaticMeshComponent* BodyMesh = nullptr;
	UStaticMeshComponent* EyesMesh = nullptr;
	UGhostAIComponent* AIComponent = nullptr;
	ULuaScriptComponent* LuaScriptComponent = nullptr;

	UMaterial* PreviousEyeOverride = nullptr;
	UMaterial* PreviousPupilOverride = nullptr;
	UMaterial* ChangedEyeOverride = nullptr;
	UMaterial* ChangedPupilOverride = nullptr;
	int32 EyeSlot = -1;
	int32 PupilSlot = -1;
	bool bEyeChanged = false;
	bool bHitFrozen = false;
	bool bAITickBeforeHit = false;
	bool bLuaTickBeforeHit = false;
	FVector InitialLocation = FVector::ZeroVector;
	float RunningTime = 0.0f;
	float BobbingSpeed = 2.0f;
	float BobbingHeight = 0.25f;
	float BodyRotationSpeed = 30.0f;
};
