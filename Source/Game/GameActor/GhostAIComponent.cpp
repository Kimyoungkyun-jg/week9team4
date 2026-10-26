#include "EnginePCH.h"
#include "GhostAIComponent.h"
#include "GhostActor.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Collision/Ray.h"
#include "Collision/HitResult.h"
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

	const float CurrentSpeed = bIsChasing ? ChaseSpeed : MoveSpeed;
	const float Step = CurrentSpeed * DeltaTime;
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
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetBackwardDetectionTiles(int32 InVal)
{
	BackwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetSideDetectionTiles(int32 InVal)
{
	SideDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetChaseForwardDetectionTiles(int32 InVal)
{
	ChaseForwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetChaseBackwardDetectionTiles(int32 InVal)
{
	ChaseBackwardDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetChaseSideDetectionTiles(int32 InVal)
{
	ChaseSideDetectionTiles = FMath::Max(0, InVal);
	UpdateDetectionBoxes();
}

void UGhostAIComponent::SetChasing(bool bInChasing)
{
	bIsChasing = bInChasing;
	UpdateDetectionBoxes();
}

void UGhostAIComponent::UpdateDetectionBoxes()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	AGhostActor* Ghost = Cast<AGhostActor>(Owner);
	if (!Ghost) return;

	AMapActor* Map = GetCachedMap();
	const float TileSize = Map ? Map->GetTileSize() : 0.5f;

	// 1. 순찰용 탐지 박스 콜라이더(PatrolDetectionBox) 크기 및 위치 갱신
	if (UBoxComponent* PatrolBox = Ghost->GetPatrolDetectionBox())
	{
		const float FwdSpan = (static_cast<float>(ForwardDetectionTiles) + 0.5f) * TileSize;
		const float BwdSpan = (static_cast<float>(BackwardDetectionTiles) + 0.5f) * TileSize;
		const float SideSpan = (static_cast<float>(SideDetectionTiles) + 0.5f) * TileSize;

		const float TotalX = FwdSpan + BwdSpan;
		const float HalfExtentX = TotalX * 0.5f;
		const float CenterOffsetX = (FwdSpan - BwdSpan) * 0.5f;

		PatrolBox->SetRelativeLocation(FVector(CenterOffsetX, 0.0f, 0.5f));
		PatrolBox->SetBoxExtent(FVector(HalfExtentX, SideSpan, 1.0f));
		PatrolBox->SetCollisionEnabled(true);
		PatrolBox->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
		PatrolBox->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
		PatrolBox->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
		PatrolBox->SetGenerateOverlapEvents(true);
		PatrolBox->UpdateOverlaps();
	}

	// 2. 추격용 탐지 박스 콜라이더(ChaseDetectionBox) 크기 및 위치 갱신
	if (UBoxComponent* ChaseBox = Ghost->GetChaseDetectionBox())
	{
		const float FwdSpan = (static_cast<float>(ChaseForwardDetectionTiles) + 0.5f) * TileSize;
		const float BwdSpan = (static_cast<float>(ChaseBackwardDetectionTiles) + 0.5f) * TileSize;
		const float SideSpan = (static_cast<float>(ChaseSideDetectionTiles) + 0.5f) * TileSize;

		const float TotalX = FwdSpan + BwdSpan;
		const float HalfExtentX = TotalX * 0.5f;
		const float CenterOffsetX = (FwdSpan - BwdSpan) * 0.5f;

		ChaseBox->SetRelativeLocation(FVector(CenterOffsetX, 0.0f, 0.5f));
		ChaseBox->SetBoxExtent(FVector(HalfExtentX, SideSpan, 1.0f));
		ChaseBox->SetCollisionEnabled(true);
		ChaseBox->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
		ChaseBox->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
		ChaseBox->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
		ChaseBox->SetGenerateOverlapEvents(true);
		ChaseBox->UpdateOverlaps();
	}
}

bool UGhostAIComponent::HasLineOfSightTo(AActor* TargetActor) const
{
	if (!TargetActor || !GetOwner()) return false;
	UWorld* World = GetWorld();
	if (!World) return false;

	const FVector Start = GetOwner()->GetActorLocation();
	const FVector End = TargetActor->GetActorLocation();

	// 1. 타일 맵 그리드 상의 벽 검사 (Bresenham 알고리즘을 통한 중간 타일 벽 검사)
	AMapActor* Map = const_cast<UGhostAIComponent*>(this)->GetCachedMap();
	if (Map)
	{
		int32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		if (Map->TryWorldToTileXYProjected(Start, x0, y0) &&
			Map->TryWorldToTileXYProjected(End, x1, y1))
		{
			if (x0 != x1 || y0 != y1)
			{
				int32 dx = std::abs(x1 - x0);
				int32 dy = std::abs(y1 - y0);
				int32 sx = (x0 < x1) ? 1 : -1;
				int32 sy = (y0 < y1) ? 1 : -1;
				int32 err = dx - dy;

				int32 cx = x0;
				int32 cy = y0;

				while (cx != x1 || cy != y1)
				{
					int32 e2 = 2 * err;
					int32 stepX = 0, stepY = 0;
					if (e2 > -dy) { err -= dy; cx += sx; stepX = sx; }
					if (e2 < dx)  { err += dx; cy += sy; stepY = sy; }

					if (cx == x1 && cy == y1) break;

					EMapTileTerrain Terrain;
					if (Map->TryGetTile(cx, cy, Terrain) && Terrain == EMapTileTerrain::Wall)
					{
						return false; // 그리드 상 벽에 가로막힘!
					}

					// 대각선 이동 시 대각선 양옆 코너 벽 차폐 검사
					if (stepX != 0 && stepY != 0)
					{
						EMapTileTerrain C1, C2;
						if ((Map->TryGetTile(cx - stepX, cy, C1) && C1 == EMapTileTerrain::Wall) &&
							(Map->TryGetTile(cx, cy - stepY, C2) && C2 == EMapTileTerrain::Wall))
						{
							return false; // 양옆 코너 벽 사이 틈새 차단
						}
					}
				}
			}
		}
	}

	// 2. 물리 3D 월드의 벽 콜라이더 레이캐스트 검사 (Pawn 채널: MapActor 벽 콜라이더 감지)
	FVector Dir = End - Start;
	Dir.Z = 0.0f;
	const float Dist = Dir.Size();
	if (Dist > 0.01f)
	{
		FRay Ray{ Start + FVector(0.0f, 0.0f, 0.3f), Dir / Dist };
		FHitResult Hit;
		// 맵 벽 콜라이더는 Pawn 채널을 Block하도록 설정되어 있습니다.
		if (World->LineTraceByChannel(Ray, Dist, ECollisionChannel::Pawn, Hit, GetOwner()))
		{
			// 맞은 대상이 TargetActor(플레이어)가 아니고 실제 장애물/벽인 경우에만 가로막힌 것으로 판정
			if (Hit.bBlockingHit && Hit.HitComponent)
			{
				AActor* HitActor = Hit.HitComponent->GetOwner();
				if (HitActor && HitActor != TargetActor && HitActor != GetOwner())
				{
					return false; // 중간에 벽 또는 장애물이 가로막음!
				}
			}
		}
	}

	return true;
}

bool UGhostAIComponent::IsPlayerInBoxMath(AActor* PlayerActor, bool bUseChase) const
{
	if (!PlayerActor || !GetOwner()) return false;
	AMapActor* Map = const_cast<UGhostAIComponent*>(this)->GetCachedMap();
	const float TileSize = Map ? Map->GetTileSize() : 0.5f;

	const FVector GhostLoc = GetOwner()->GetActorLocation();
	const FVector PlayerLoc = PlayerActor->GetActorLocation();

	FVector ToPlayer = PlayerLoc - GhostLoc;
	ToPlayer.Z = 0.0f;

	FVector Forward = GetOwner()->GetActorRotation().Quaternion().GetForwardVector();
	Forward.Z = 0.0f;
	Forward = Forward.GetSafeNormal(0.0001f, FVector(1.0f, 0.0f, 0.0f));

	FVector Right = GetOwner()->GetActorRotation().Quaternion().GetRightVector();
	Right.Z = 0.0f;
	Right = Right.GetSafeNormal(0.0001f, FVector(0.0f, 1.0f, 0.0f));

	const float ForwardDist = ToPlayer.Dot(Forward);
	const float SideDist = std::fabs(ToPlayer.Dot(Right));

	const int32 FwdTiles = bUseChase ? ChaseForwardDetectionTiles : ForwardDetectionTiles;
	const int32 BwdTiles = bUseChase ? ChaseBackwardDetectionTiles : BackwardDetectionTiles;
	const int32 SdTiles  = bUseChase ? ChaseSideDetectionTiles : SideDetectionTiles;

	const float MaxForwardDist = (static_cast<float>(FwdTiles) + 0.5f) * TileSize;
	const float MaxBackwardDist = (static_cast<float>(BwdTiles) + 0.5f) * TileSize;
	const float MaxSideDist = (static_cast<float>(SdTiles) + 0.5f) * TileSize;

	// 플레이어 캡슐 반경만큼 확장하여 콜라이더 오버랩과 일치시킴
	float PlayerRadius = 0.2f;
	if (UCapsuleComponent* PlayerCapsule = PlayerActor->GetComponentByClass<UCapsuleComponent>())
	{
		PlayerRadius = PlayerCapsule->GetUnscaledCapsuleRadius();
	}

	if (ForwardDist >= 0.0f)
	{
		if (ForwardDist > MaxForwardDist + PlayerRadius) return false;
	}
	else
	{
		if (-ForwardDist > MaxBackwardDist + PlayerRadius) return false;
	}

	if (SideDist > MaxSideDist + PlayerRadius) return false;

	return true;
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

	AGhostActor* Ghost = Cast<AGhostActor>(Owner);
	if (!Ghost) return false;

	const bool bUseChase = bForceChaseRange || bIsChasing;

	// 1. 박스 콜라이더 선택 (순찰용 PatrolDetectionBox vs 추격용 ChaseDetectionBox)
	UBoxComponent* TargetBox = bUseChase ? Ghost->GetChaseDetectionBox() : Ghost->GetPatrolDetectionBox();

	// 2. 박스 콜라이더 오버랩 검사 (Box Overlap)
	bool bInBox = false;
	if (TargetBox)
	{
		for (UPrimitiveComponent* Overlap : TargetBox->GetOverlappingComponents())
		{
			if (Overlap && Overlap->GetOwner() == PlayerActor)
			{
				bInBox = true;
				break;
			}
		}
	}

	// 물리 틱 갱신 지연 대비 수학적 박스 영역 검사
	if (!bInBox)
	{
		bInBox = IsPlayerInBoxMath(PlayerActor, bUseChase);
	}

	if (!bInBox)
	{
		return false; // 박스 콜라이더 영역 밖에 있음
	}

	// 3. [핵심!] 시야 차폐 검사 (Line of Sight): 플레이어와 고스트 사이에 벽이 가로막고 있으면 감지 불가!
	if (!HasLineOfSightTo(PlayerActor))
	{
		return false; // 벽 뒤에 숨어 있으므로 보이지 않음!
	}

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
		if (Handle.contains("ChaseSpeed")) ChaseSpeed = Handle["ChaseSpeed"].get<float>();
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
		Handle["ChaseSpeed"] = ChaseSpeed;
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
