#pragma once

#include "Components/ActorComponent.h"
#include "Math/Vector.h"
#include "Lua/LuaMacros.h"
#include "Containers/Array.h"
#include "json.hpp"

class AMapActor;

struct FGhostTileCoord
{
	int32 Column = 0;
	int32 Row = 0;

	bool operator==(const FGhostTileCoord& Other) const { return Column == Other.Column && Row == Other.Row; }
	bool operator!=(const FGhostTileCoord& Other) const { return !(*this == Other); }
};

class UGhostAIComponent : public UActorComponent
{
	DECLARE_CLASS(UGhostAIComponent, UActorComponent)

	REFLECT_START(UGhostAIComponent)
		PROPERTY(MoveSpeed)
		PROPERTY(WaitTimeBeforeChase)
		PROPERTY(MaxChaseDuration)
		PROPERTY(ForwardDetectionTiles)
		PROPERTY(BackwardDetectionTiles)
		PROPERTY(SideDetectionTiles)
		PROPERTY(ChaseForwardDetectionTiles)
		PROPERTY(ChaseBackwardDetectionTiles)
		PROPERTY(ChaseSideDetectionTiles)
	REFLECT_END()

	LUA_BIND_START(UGhostAIComponent)
		LUA_FUNC(SetDestination)
		LUA_FUNC(ChasePlayer)
		LUA_FUNC(ReturnToHome)
		LUA_FUNC(MoveToNextPatrol)
		LUA_FUNC(StopMovement)
		LUA_FUNC(HasReachedDestination)
		LUA_FUNC(SetMoveSpeed)
		LUA_FUNC(GetMoveSpeed)
		LUA_FUNC(GetWaitTimeBeforeChase)
		LUA_FUNC(SetWaitTimeBeforeChase)
		LUA_FUNC(GetMaxChaseDuration)
		LUA_FUNC(SetMaxChaseDuration)
		LUA_FUNC(GetForwardDetectionTiles)
		LUA_FUNC(SetForwardDetectionTiles)
		LUA_FUNC(GetBackwardDetectionTiles)
		LUA_FUNC(SetBackwardDetectionTiles)
		LUA_FUNC(GetSideDetectionTiles)
		LUA_FUNC(SetSideDetectionTiles)
		LUA_FUNC(GetChaseForwardDetectionTiles)
		LUA_FUNC(SetChaseForwardDetectionTiles)
		LUA_FUNC(GetChaseBackwardDetectionTiles)
		LUA_FUNC(SetChaseBackwardDetectionTiles)
		LUA_FUNC(GetChaseSideDetectionTiles)
		LUA_FUNC(SetChaseSideDetectionTiles)
		LUA_FUNC(IsChasing)
		LUA_FUNC(SetChasing)
		LUA_FUNC(IsPlayerInDetectionRange)
		LUA_FUNC(IsPlayerInChaseRange)
		LUA_FUNC(AddPatrolPoint)
		LUA_FUNC(AddPatrolWorldLocation)
		LUA_FUNC(ClearPatrolPoints)
		LUA_FUNC(GetPatrolPointCount)
		LUA_FUNC(GetCurrentPatrolIndex)
	LUA_BIND_END()

public:
	UGhostAIComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual void Serialize(nlohmann::json& Handle, bool bIsLoading) override;

	bool SetDestination(const FVector& TargetWorldPos);
	bool ChasePlayer();
	bool ReturnToHome();
	void MoveToNextPatrol();
	void StopMovement();
	bool HasReachedDestination() const { return !bIsMoving || CurrentPath.IsEmpty(); }

	// Patrol Management
	void AddPatrolPoint(int32 Column, int32 Row);
	void AddPatrolWorldLocation(const FVector& WorldLocation);
	void RemovePatrolPoint(int32 Index);
	void SetPatrolPoint(int32 Index, int32 Column, int32 Row);
	void MovePatrolPointUp(int32 Index);
	void MovePatrolPointDown(int32 Index);
	void ClearPatrolPoints();

	int32 GetPatrolPointCount() const { return PatrolPoints.Num(); }
	int32 GetCurrentPatrolIndex() const { return CurrentPatrolIndex; }
	TArray<FGhostTileCoord>& GetPatrolPoints() { return PatrolPoints; }
	const TArray<FGhostTileCoord>& GetPatrolPoints() const { return PatrolPoints; }

	// Speed & Timer Getters / Setters
	void SetMoveSpeed(float Speed) { MoveSpeed = Speed; }
	float GetMoveSpeed() const { return MoveSpeed; }

	float GetWaitTimeBeforeChase() const { return WaitTimeBeforeChase; }
	void SetWaitTimeBeforeChase(float InVal) { WaitTimeBeforeChase = InVal; }

	float GetMaxChaseDuration() const { return MaxChaseDuration; }
	void SetMaxChaseDuration(float InVal) { MaxChaseDuration = InVal; }

	// Directional Detection Range (Tiles) - Normal / Patrol
	int32 GetForwardDetectionTiles() const { return ForwardDetectionTiles; }
	void SetForwardDetectionTiles(int32 InVal);

	int32 GetBackwardDetectionTiles() const { return BackwardDetectionTiles; }
	void SetBackwardDetectionTiles(int32 InVal);

	int32 GetSideDetectionTiles() const { return SideDetectionTiles; }
	void SetSideDetectionTiles(int32 InVal);

	// Directional Detection Range (Tiles) - Chase
	int32 GetChaseForwardDetectionTiles() const { return ChaseForwardDetectionTiles; }
	void SetChaseForwardDetectionTiles(int32 InVal);

	int32 GetChaseBackwardDetectionTiles() const { return ChaseBackwardDetectionTiles; }
	void SetChaseBackwardDetectionTiles(int32 InVal);

	int32 GetChaseSideDetectionTiles() const { return ChaseSideDetectionTiles; }
	void SetChaseSideDetectionTiles(int32 InVal);

	bool IsChasing() const { return bIsChasing; }
	void SetChasing(bool bInChasing);

	bool IsPlayerInDetectionRange(AActor* PlayerActor = nullptr, bool bForceChaseRange = false) const;
	bool IsPlayerInChaseRange(AActor* PlayerActor = nullptr) const { return IsPlayerInDetectionRange(PlayerActor, true); }
	void UpdateDetectionBoxExtent();

	const FVector& GetHomeLocation() const { return HomeLocation; }
	void SetHomeLocation(const FVector& InHome) { HomeLocation = InHome; }

	// Editor Helpers
	AMapActor* GetCachedMap();
	bool GetCurrentTile(int32& OutCol, int32& OutRow);
	bool SnapOwnerToTile(int32 Col, int32 Row);

private:
	bool FindPathBFS(const FGhostTileCoord& StartTile, const FGhostTileCoord& GoalTile, TArray<FGhostTileCoord>& OutPath);
	void InitializeDefaultPatrolPoints();

	AMapActor* CachedMap = nullptr;
	FVector HomeLocation = FVector::ZeroVector;

	TArray<FGhostTileCoord> CurrentPath;
	TArray<FGhostTileCoord> PatrolPoints;
	int32 CurrentPatrolIndex = -1;

	float MoveSpeed = 2.5f;
	float WaitTimeBeforeChase = 1.0f;
	float MaxChaseDuration = 2.0f;

	// Normal Detection Range
	int32 ForwardDetectionTiles = 5;
	int32 BackwardDetectionTiles = 1;
	int32 SideDetectionTiles = 1;

	// Chase Detection Range (추격 시 넓어지는 감지 범위)
	int32 ChaseForwardDetectionTiles = 8;
	int32 ChaseBackwardDetectionTiles = 3;
	int32 ChaseSideDetectionTiles = 4;

	bool bIsChasing = false;
	bool bIsMoving = false;
};
