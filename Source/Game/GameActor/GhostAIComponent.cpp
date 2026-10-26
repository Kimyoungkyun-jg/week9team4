#include "EnginePCH.h"
#include "GhostAIComponent.h"
#include "GhostActor.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Core/EngineLog.h"
#include <queue>
#include <algorithm>
#include <cmath>

UGhostAIComponent::UGhostAIComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGhostAIComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		HomeLocation = Owner->GetActorLocation();
	}

	GetCachedMap();

	if (PatrolPoints.IsEmpty())
	{
		InitializeDefaultPatrolPoints();
	}

	UpdateDetectionBoxExtent();
}

AMapActor* UGhostAIComponent::GetCachedMap()
{
	if (CachedMap) return CachedMap;

	UWorld* World = GetWorld();
	if (World && World->GetPersistentLevel())
	{
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			if (AMapActor* Map = Cast<AMapActor>(Actor))
			{
				CachedMap = Map;
				break;
			}
		}
	}
	return CachedMap;
}

bool UGhostAIComponent::GetCurrentTile(int32& OutCol, int32& OutRow)
{
	AMapActor* Map = GetCachedMap();
	AActor* Owner = GetOwner();
	if (!Map || !Owner) return false;

	return Map->TryWorldToTileXYProjected(Owner->GetActorLocation(), OutCol, OutRow);
}

bool UGhostAIComponent::SnapOwnerToTile(int32 Col, int32 Row)
{
	AMapActor* Map = GetCachedMap();
	AActor* Owner = GetOwner();
	if (!Map || !Owner) return false;

	EMapTileTerrain Terrain;
	FVector Center;
	if (Map->TryGetTile(Col, Row, Terrain) && Terrain == EMapTileTerrain::Floor &&
		Map->TryTileToWorldCenter(Col, Row, Center))
	{
		FVector CurrentPos = Owner->GetActorLocation();
		Center.Z = CurrentPos.Z; // 높이 유지
		Owner->SetActorLocation(Center);
		HomeLocation = Center;
		return true;
	}
	return false;
}

void UGhostAIComponent::InitializeDefaultPatrolPoints()
{
	AMapActor* Map = GetCachedMap();
	if (!Map || !GetOwner()) return;

	int32 CenterCol = 0, CenterRow = 0;
	if (!Map->TryWorldToTileXYProjected(HomeLocation, CenterCol, CenterRow))
		return;

	PatrolPoints.Add({CenterCol, CenterRow});

	// 상, 우, 하, 좌 방향으로 바닥 타일 검색하여 기본 순찰 지점 설정
	const int32 Offsets[][2] = { {4, 0}, {0, 4}, {-4, 0}, {0, -4} };
	for (const auto& Offset : Offsets)
	{
		int32 TargetCol = CenterCol + Offset[0];
		int32 TargetRow = CenterRow + Offset[1];

		EMapTileTerrain Terrain;
		if (Map->TryGetTile(TargetCol, TargetRow, Terrain) && Terrain == EMapTileTerrain::Floor)
		{
			PatrolPoints.Add({TargetCol, TargetRow});
		}
	}
}

void UGhostAIComponent::AddPatrolPoint(int32 Column, int32 Row)
{
	PatrolPoints.Add({Column, Row});
}

void UGhostAIComponent::AddPatrolWorldLocation(const FVector& WorldLocation)
{
	AMapActor* Map = GetCachedMap();
	if (!Map) return;
	int32 Col = 0, Row = 0;
	if (Map->TryWorldToTileXYProjected(WorldLocation, Col, Row))
	{
		AddPatrolPoint(Col, Row);
	}
}

void UGhostAIComponent::RemovePatrolPoint(int32 Index)
{
	if (Index >= 0 && Index < PatrolPoints.Num())
	{
		PatrolPoints.RemoveAt(Index);
		if (CurrentPatrolIndex >= PatrolPoints.Num())
		{
			CurrentPatrolIndex = PatrolPoints.IsEmpty() ? -1 : 0;
		}
	}
}

void UGhostAIComponent::SetPatrolPoint(int32 Index, int32 Column, int32 Row)
{
	if (Index >= 0 && Index < PatrolPoints.Num())
	{
		PatrolPoints[Index] = { Column, Row };
	}
}

void UGhostAIComponent::MovePatrolPointUp(int32 Index)
{
	if (Index > 0 && Index < PatrolPoints.Num())
	{
		std::swap(PatrolPoints[Index], PatrolPoints[Index - 1]);
	}
}

void UGhostAIComponent::MovePatrolPointDown(int32 Index)
{
	if (Index >= 0 && Index + 1 < PatrolPoints.Num())
	{
		std::swap(PatrolPoints[Index], PatrolPoints[Index + 1]);
	}
}

void UGhostAIComponent::ClearPatrolPoints()
{
	PatrolPoints.Reset();
	CurrentPatrolIndex = -1;
}

bool UGhostAIComponent::FindPathBFS(const FGhostTileCoord& StartTile, const FGhostTileCoord& GoalTile, TArray<FGhostTileCoord>& OutPath)
{
	OutPath.Reset();
	AMapActor* Map = GetCachedMap();
	if (!Map) return false;
	if (StartTile == GoalTile) return true;

	const int32 Cols = Map->GetColumns();
	const int32 Rows = Map->GetRows();

	if (!Map->IsValidTile(StartTile.Column, StartTile.Row) || 
	    !Map->IsValidTile(GoalTile.Column, GoalTile.Row))
	{
		return false;
	}

	TArray<FGhostTileCoord> Parent;
	Parent.Init(FGhostTileCoord{-1, -1}, Cols * Rows);

	std::queue<FGhostTileCoord> Queue;
	Queue.push(StartTile);
	Parent[StartTile.Row * Cols + StartTile.Column] = StartTile;

	static const FGhostTileCoord Dirs[4] = {
		{1, 0},   // Right
		{-1, 0},  // Left
		{0, 1},   // Up
		{0, -1}   // Down
	};

	bool bFound = false;
	while (!Queue.empty())
	{
		const FGhostTileCoord Current = Queue.front();
		Queue.pop();

		if (Current == GoalTile)
		{
			bFound = true;
			break;
		}

		for (const FGhostTileCoord& Dir : Dirs)
		{
			const FGhostTileCoord Next{ Current.Column + Dir.Column, Current.Row + Dir.Row };
			if (!Map->IsValidTile(Next.Column, Next.Row)) continue;

			const int32 NextIdx = Next.Row * Cols + Next.Column;
			if (Parent[NextIdx] != FGhostTileCoord{-1, -1}) continue; // 이미 방문

			EMapTileTerrain Terrain;
			if (Map->TryGetTile(Next.Column, Next.Row, Terrain) && Terrain == EMapTileTerrain::Floor)
			{
				Parent[NextIdx] = Current;
				Queue.push(Next);
			}
		}
	}

	if (!bFound) return false;

	// GoalTile에서 StartTile로 역추적
	FGhostTileCoord Curr = GoalTile;
	while (Curr != StartTile)
	{
		OutPath.Insert(Curr, 0);
		Curr = Parent[Curr.Row * Cols + Curr.Column];
	}

	return !OutPath.IsEmpty();
}

bool UGhostAIComponent::SetDestination(const FVector& TargetWorldPos)
{
	AMapActor* Map = GetCachedMap();
	if (!Map || !GetOwner()) return false;

	int32 StartCol = 0, StartRow = 0;
	int32 GoalCol = 0, GoalRow = 0;

	if (!Map->TryWorldToTileXYProjected(GetOwner()->GetActorLocation(), StartCol, StartRow))
		return false;

	if (!Map->TryWorldToTileXYProjected(TargetWorldPos, GoalCol, GoalRow))
		return false;

	EMapTileTerrain GoalTerrain;
	if (!Map->TryGetTile(GoalCol, GoalRow, GoalTerrain) || GoalTerrain != EMapTileTerrain::Floor)
	{
		static const int32 Dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
		bool bAdjacentFloor = false;
		for (const auto& Dir : Dirs)
		{
			int32 AdjCol = GoalCol + Dir[0];
			int32 AdjRow = GoalRow + Dir[1];
			if (Map->TryGetTile(AdjCol, AdjRow, GoalTerrain) && GoalTerrain == EMapTileTerrain::Floor)
			{
				GoalCol = AdjCol;
				GoalRow = AdjRow;
				bAdjacentFloor = true;
				break;
			}
		}
		if (!bAdjacentFloor) return false;
	}

	if (FindPathBFS({StartCol, StartRow}, {GoalCol, GoalRow}, CurrentPath))
	{
		bIsMoving = !CurrentPath.IsEmpty();
		return true;
	}

	bIsMoving = false;
	return false;
}

bool UGhostAIComponent::ChasePlayer()
{
	bIsChasing = true;
	UpdateDetectionBoxExtent();

	UWorld* World = GetWorld();
	if (!World) return false;
	APawn* Player = World->GetPlayerPawn();
	if (!Player) return false;

	return SetDestination(Player->GetActorLocation());
}

bool UGhostAIComponent::ReturnToHome()
{
	bIsChasing = false;
	UpdateDetectionBoxExtent();
	return SetDestination(HomeLocation);
}

void UGhostAIComponent::MoveToNextPatrol()
{
	bIsChasing = false;
	UpdateDetectionBoxExtent();

	AMapActor* Map = GetCachedMap();
	if (PatrolPoints.IsEmpty() || !Map) return;

	CurrentPatrolIndex = (CurrentPatrolIndex + 1) % PatrolPoints.Num();
	const FGhostTileCoord& TargetTile = PatrolPoints[CurrentPatrolIndex];

	FVector TargetWorldPos;
	if (Map->TryTileToWorldCenter(TargetTile.Column, TargetTile.Row, TargetWorldPos))
	{
		SetDestination(TargetWorldPos);
	}
}

void UGhostAIComponent::StopMovement()
{
	bIsMoving = false;
	CurrentPath.Reset();
}

void UGhostAIComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	AMapActor* Map = GetCachedMap();
	if (!bIsMoving || CurrentPath.IsEmpty() || !Map) return;

	AActor* Owner = GetOwner();
	if (!Owner) return;

	FVector TargetWorldPos;
	if (!Map->TryTileToWorldCenter(CurrentPath[0].Column, CurrentPath[0].Row, TargetWorldPos))
	{
		CurrentPath.RemoveAt(0);
		return;
	}

	FVector CurrentPos = Owner->GetActorLocation();
	TargetWorldPos.Z = CurrentPos.Z;

	FVector ToTarget = TargetWorldPos - CurrentPos;
	ToTarget.Z = 0.0f;
	const float Dist = ToTarget.Size();

	const float Step = MoveSpeed * DeltaTime;
	if (Dist <= Step || Dist < 0.05f)
	{
		Owner->SetActorLocation(TargetWorldPos);
		CurrentPath.RemoveAt(0);
		if (CurrentPath.IsEmpty())
		{
			bIsMoving = false;
		}
	}
	else
	{
		FVector Dir = ToTarget / Dist;
		Owner->SetActorLocation(CurrentPos + Dir * Step);
		const float TargetYaw = FMath::RadiansToDegrees(std::atan2(Dir.Y, Dir.X));
		Owner->SetActorRotation(FRotator(0.0f, TargetYaw, 0.0f));
	}
}

void UGhostAIComponent::SetForwardDetectionTiles(int32 InVal)
{
	ForwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetBackwardDetectionTiles(int32 InVal)
{
	BackwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetSideDetectionTiles(int32 InVal)
{
	SideDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetChaseForwardDetectionTiles(int32 InVal)
{
	ChaseForwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetChaseBackwardDetectionTiles(int32 InVal)
{
	ChaseBackwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetChaseSideDetectionTiles(int32 InVal)
{
	ChaseSideDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::SetChasing(bool bInChasing)
{
	bIsChasing = bInChasing;
	UpdateDetectionBoxExtent();
}

void UGhostAIComponent::UpdateDetectionBoxExtent()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	AGhostActor* Ghost = Cast<AGhostActor>(Owner);
	if (!Ghost) return;

	UBoxComponent* DetectionBox = Ghost->GetDetectionBox();
	if (!DetectionBox) return;

	AMapActor* Map = GetCachedMap();
	const float TileSize = Map ? Map->GetTileSize() : 0.5f;

	const int32 FwdTiles = bIsChasing ? ChaseForwardDetectionTiles : ForwardDetectionTiles;
	const int32 BwdTiles = bIsChasing ? ChaseBackwardDetectionTiles : BackwardDetectionTiles;
	const int32 SdTiles  = bIsChasing ? ChaseSideDetectionTiles : SideDetectionTiles;

	const float ForwardSpan = (static_cast<float>(FwdTiles) + 0.5f) * TileSize;
	const float BackwardSpan = (static_cast<float>(BwdTiles) + 0.5f) * TileSize;
	const float SideSpan = (static_cast<float>(SdTiles) + 0.5f) * TileSize;

	// Total length along forward (+X) axis: ForwardSpan + BackwardSpan
	const float TotalX = ForwardSpan + BackwardSpan;
	const float HalfExtentX = TotalX * 0.5f;
	const float CenterOffsetX = (ForwardSpan - BackwardSpan) * 0.5f;

	DetectionBox->SetRelativeLocation(FVector(CenterOffsetX, 0.0f, 0.5f));
	DetectionBox->SetBoxExtent(FVector(HalfExtentX, SideSpan, 1.0f));
	DetectionBox->UpdateOverlaps();
}

bool UGhostAIComponent::IsPlayerInDetectionRange(AActor* PlayerActor, bool bForceChaseRange) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	if (!PlayerActor)
	{
		PlayerActor = World->GetPlayerPawn();
	}
	if (!PlayerActor) return false;

	AActor* Owner = GetOwner();
	if (!Owner) return false;

	AMapActor* Map = const_cast<UGhostAIComponent*>(this)->GetCachedMap();
	const float TileSize = Map ? Map->GetTileSize() : 0.5f;

	const FVector GhostLoc = Owner->GetActorLocation();
	const FVector PlayerLoc = PlayerActor->GetActorLocation();

	FVector ToPlayer = PlayerLoc - GhostLoc;
	ToPlayer.Z = 0.0f;

	// 고스트의 현재 정면(+X) 및 우측(+Y) 방향 벡터
	FVector Forward = Owner->GetActorRotation().Quaternion().GetForwardVector();
	Forward.Z = 0.0f;
	Forward = Forward.GetSafeNormal(0.0001f, FVector(1.0f, 0.0f, 0.0f));

	FVector Right = Owner->GetActorRotation().Quaternion().GetRightVector();
	Right.Z = 0.0f;
	Right = Right.GetSafeNormal(0.0001f, FVector(0.0f, 1.0f, 0.0f));

	const float ForwardDist = ToPlayer.Dot(Forward);
	const float SideDist = std::fabs(ToPlayer.Dot(Right));

	// 추격 모드이거나 강제 추격 범위 요청 시 확장된 범위 사용
	const bool bUseChase = bForceChaseRange || bIsChasing;
	const int32 FwdTiles = bUseChase ? ChaseForwardDetectionTiles : ForwardDetectionTiles;
	const int32 BwdTiles = bUseChase ? ChaseBackwardDetectionTiles : BackwardDetectionTiles;
	const int32 SdTiles  = bUseChase ? ChaseSideDetectionTiles : SideDetectionTiles;

	const float MaxForwardDist = (static_cast<float>(FwdTiles) + 0.5f) * TileSize;
	const float MaxBackwardDist = (static_cast<float>(BwdTiles) + 0.5f) * TileSize;
	const float MaxSideDist = (static_cast<float>(SdTiles) + 0.5f) * TileSize;

	if (ForwardDist >= 0.0f)
	{
		if (ForwardDist > MaxForwardDist)
			return false;
	}
	else
	{
		if (-ForwardDist > MaxBackwardDist)
			return false;
	}

	if (SideDist > MaxSideDist)
		return false;

	return true;
}

void UGhostAIComponent::Serialize(nlohmann::json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);

	if (bIsLoading)
	{
		if (Handle.contains("PatrolPoints") && Handle["PatrolPoints"].is_array())
		{
			PatrolPoints.Reset();
			for (const auto& Pt : Handle["PatrolPoints"])
			{
				if (Pt.is_array() && Pt.size() >= 2)
				{
					PatrolPoints.Add({ Pt[0].get<int32>(), Pt[1].get<int32>() });
				}
			}
		}
		if (Handle.contains("MoveSpeed")) MoveSpeed = Handle["MoveSpeed"].get<float>();
		if (Handle.contains("WaitTimeBeforeChase")) WaitTimeBeforeChase = Handle["WaitTimeBeforeChase"].get<float>();
		if (Handle.contains("MaxChaseDuration")) MaxChaseDuration = Handle["MaxChaseDuration"].get<float>();
		if (Handle.contains("ForwardDetectionTiles")) ForwardDetectionTiles = Handle["ForwardDetectionTiles"].get<int32>();
		if (Handle.contains("BackwardDetectionTiles")) BackwardDetectionTiles = Handle["BackwardDetectionTiles"].get<int32>();
		if (Handle.contains("SideDetectionTiles")) SideDetectionTiles = Handle["SideDetectionTiles"].get<int32>();
		if (Handle.contains("ChaseForwardDetectionTiles")) ChaseForwardDetectionTiles = Handle["ChaseForwardDetectionTiles"].get<int32>();
		if (Handle.contains("ChaseBackwardDetectionTiles")) ChaseBackwardDetectionTiles = Handle["ChaseBackwardDetectionTiles"].get<int32>();
		if (Handle.contains("ChaseSideDetectionTiles")) ChaseSideDetectionTiles = Handle["ChaseSideDetectionTiles"].get<int32>();
	}
	else
	{
		nlohmann::json PtArray = nlohmann::json::array();
		for (const auto& Pt : PatrolPoints)
		{
			PtArray.push_back({ Pt.Column, Pt.Row });
		}
		Handle["PatrolPoints"] = PtArray;
		Handle["MoveSpeed"] = MoveSpeed;
		Handle["WaitTimeBeforeChase"] = WaitTimeBeforeChase;
		Handle["MaxChaseDuration"] = MaxChaseDuration;
		Handle["ForwardDetectionTiles"] = ForwardDetectionTiles;
		Handle["BackwardDetectionTiles"] = BackwardDetectionTiles;
		Handle["SideDetectionTiles"] = SideDetectionTiles;
		Handle["ChaseForwardDetectionTiles"] = ChaseForwardDetectionTiles;
		Handle["ChaseBackwardDetectionTiles"] = ChaseBackwardDetectionTiles;
		Handle["ChaseSideDetectionTiles"] = ChaseSideDetectionTiles;
	}
}
