#pragma once

#include "GameFramework/Character.h"
#include "Audio/FootstepCadence.h"
#include "Lua/LuaMacros.h"
#include <random>

class AFlashlightActor;
class UCapsuleComponent;
class UHeadBobComponent;

class APlayerPawn : public ACharacter
{
	DECLARE_CLASS(APlayerPawn, ACharacter)
	REFLECT_START(APlayerPawn)
		PROPERTY(Life)
	REFLECT_END()

	LUA_BIND_START(APlayerPawn)
		LUA_FUNC(GetSlomoGauge)
		LUA_FUNC(GetMaxSlomoGauge)
		LUA_FUNC(SetSlomoGauge)
		LUA_FUNC(AddSlomoGauge)
		LUA_FUNC(IsSlomoActive)
		LUA_FUNC(SetSlomoActive)
		sol::table ActorTable = lua["AActor"];
		if (ActorTable.valid())
		{
			ActorTable["GetSlomoGauge"] = [](AActor& Self) -> float {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					return Pawn->GetSlomoGauge();
				return 0.0f;
			};
			ActorTable["GetMaxSlomoGauge"] = [](AActor& Self) -> float {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					return Pawn->GetMaxSlomoGauge();
				return 100.0f;
			};
			ActorTable["SetSlomoGauge"] = [](AActor& Self, float NewGauge) {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					Pawn->SetSlomoGauge(NewGauge);
			};
			ActorTable["AddSlomoGauge"] = [](AActor& Self, float Amount) {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					Pawn->AddSlomoGauge(Amount);
			};
			ActorTable["IsSlomoActive"] = [](AActor& Self) -> bool {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					return Pawn->IsSlomoActive();
				return false;
			};
			ActorTable["SetSlomoActive"] = [](AActor& Self, bool bActive) {
				if (APlayerPawn* Pawn = Cast<APlayerPawn>(&Self))
					Pawn->SetSlomoActive(bActive);
			};
		}
	LUA_BIND_END()

public:
	APlayerPawn();
	void BeginPlay() override;
	void EndPlay() override;
	void Tick(float DeltaTime) override;
	void SetFootstepInterval(float Seconds) { FootstepCadence.IntervalSeconds = Seconds; }
	AFlashlightActor* GetFlashlightActor() const { return FlashlightActor; }
	int32 GetLife() const { return Life; }
	void SetLife(int32 InLife);
	void RespawnAtPlayerStart();
	// The hit sequence owns the camera until it explicitly returns control.
	void SetHeadBobSuspended(bool bSuspended);
	UHeadBobComponent* GetHeadBobComponent() const;

	float GetSlomoGauge() const { return SlomoGauge; }
	float GetMaxSlomoGauge() const { return MaxSlomoGauge; }
	void SetSlomoGauge(float NewGauge) { SlomoGauge = std::clamp(NewGauge, 0.0f, MaxSlomoGauge); }
	void AddSlomoGauge(float Amount) { SetSlomoGauge(SlomoGauge + Amount); }
	bool IsSlomoActive() const { return bIsSlomoActive; }
	void SetSlomoActive(bool bActive) { bIsSlomoActive = bActive; }

private:
	void ResetWalkingEffects();
	FFootstepCadence FootstepCadence;
	std::mt19937 FootstepRandom{std::random_device{}()};
	AFlashlightActor* FlashlightActor = nullptr;
	int32 Life = 3;
	float SlomoGauge = 100.0f;
	float MaxSlomoGauge = 100.0f;
	float SlomoDrainRate = 25.0f;    // 1초당 25 소모 (총 4초 사용 가능)
	float SlomoRecoverRate = 12.5f;  // 1초당 12.5 회복 (완충까지 8초)
	bool bIsSlomoActive = false;
	FVector PlayerStartLocation = FVector::ZeroVector;
	FRotator PlayerStartRotation;
	FVector PlayerStartCameraLocation = FVector::ZeroVector;
	FRotator PlayerStartCameraRotation;
};
