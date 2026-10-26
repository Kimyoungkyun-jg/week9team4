#include "EnginePCH.h"
#include "GhostActor.h"
#include "GhostAIComponent.h"
#include "PacmanGameMode.h"
#include "PlayerPawn.h"
#include "Core/EngineLog.h"

#include "Asset/AssetManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LuaScriptComponent.h"
#include "GameFramework/Actor/MapActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include <cmath>

AGhostActor::AGhostActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	CapsuleComponent->SetupAttachment(GetRootComponent());
	CapsuleComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.5f));
	CapsuleComponent->SetCapsuleSize(0.45f, 0.5f);
	CapsuleComponent->SetCollisionEnabled(true);
	CapsuleComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	CapsuleComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	CapsuleComponent->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
	CapsuleComponent->SetGenerateOverlapEvents(true);

	// 순찰 시 사용할 탐지 박스 콜라이더
	PatrolDetectionBox = CreateDefaultSubobject<UBoxComponent>("PatrolDetectionBox");
	PatrolDetectionBox->SetupAttachment(GetRootComponent());
	PatrolDetectionBox->SetBoxExtent(FVector(3.0f, 1.0f, 1.5f));
	PatrolDetectionBox->SetCollisionEnabled(true);
	PatrolDetectionBox->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	PatrolDetectionBox->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	PatrolDetectionBox->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
	PatrolDetectionBox->SetGenerateOverlapEvents(true);

	// 추격 시 사용할 확장 추격 박스 콜라이더
	ChaseDetectionBox = CreateDefaultSubobject<UBoxComponent>("ChaseDetectionBox");
	ChaseDetectionBox->SetupAttachment(GetRootComponent());
	ChaseDetectionBox->SetBoxExtent(FVector(5.0f, 3.0f, 1.5f));
	ChaseDetectionBox->SetCollisionEnabled(true);
	ChaseDetectionBox->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	ChaseDetectionBox->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	ChaseDetectionBox->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
	ChaseDetectionBox->SetGenerateOverlapEvents(true);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>("BodyMesh");
	BodyMesh->SetupAttachment(GetRootComponent());
	BodyMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Ghost/Ghost_Body.obj"));
	BodyMesh->SetRelativeScale3D(FVector(0.4f));
	BodyMesh->SetCollisionEnabled(false);
	BodyMesh->SetReceiveSpecular(false);

	EyesMesh = CreateDefaultSubobject<UStaticMeshComponent>("EyesMesh");
	EyesMesh->SetupAttachment(GetRootComponent());
	EyesMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Ghost/Ghost_Eyes.obj"));
	EyesMesh->SetRelativeScale3D(FVector(0.4f));
	EyesMesh->SetCollisionEnabled(false);
	EyesMesh->SetReceiveSpecular(false);

	// 길찾기/이동 컴포넌트 부착
	AIComponent = CreateDefaultSubobject<UGhostAIComponent>("GhostAIComponent");

	// Lua 스크립트 컴포넌트 부착
	LuaScriptComponent = CreateDefaultSubobject<ULuaScriptComponent>("LuaScriptComponent");
	LuaScriptComponent->SetScriptPath("Assets/Scripts/GhostAI.lua");
}

void AGhostActor::BeginPlay()
{
	Super::BeginPlay();

	// MapActor가 있다면 바닥 타일 높이에 맞춰 위치 보정
	UWorld* World = GetWorld();
	if (World && World->GetPersistentLevel())
	{
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			if (AMapActor* Map = Cast<AMapActor>(Actor))
			{
				int32 Col, Row;
				EMapTileTerrain Terrain;
				FVector FloorCenter;
				if (Map->TryWorldToTileXYProjected(GetActorLocation(), Col, Row) &&
					Map->TryGetTile(Col, Row, Terrain) && Terrain == EMapTileTerrain::Floor &&
					Map->TryTileToWorldCenter(Col, Row, FloorCenter))
				{
					FVector Location = GetActorLocation();
					Location.Z = FloorCenter.Z + CapsuleComponent->GetUnscaledCapsuleHalfHeight();
					SetActorLocation(Location);
					if (AIComponent)
					{
						AIComponent->SetHomeLocation(Location);
					}
				}
				break;
			}
		}
	}

	InitialLocation = GetActorLocation();

	if (CapsuleComponent)
	{
		CapsuleComponent->OnComponentBeginOverlap.AddRaw(this, &AGhostActor::OnCapsuleBeginOverlap);
		CapsuleComponent->UpdateOverlaps();
	}

	if (AIComponent)
	{
		AIComponent->UpdateDetectionBoxes();
	}

	if (PatrolDetectionBox)
	{
		PatrolDetectionBox->UpdateOverlaps();
	}
	if (ChaseDetectionBox)
	{
		ChaseDetectionBox->UpdateOverlaps();
	}
}

void AGhostActor::EndPlay()
{
	if (CapsuleComponent)
	{
		CapsuleComponent->OnComponentBeginOverlap.RemoveAll(this);
	}
	Super::EndPlay();
}

void AGhostActor::OnCapsuleBeginOverlap(UPrimitiveComponent* OverlappedComp, UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;
	AActor* OtherActor = OtherComp->GetOwner();
	if (!OtherActor) return;

	if (APlayerPawn* Player = Cast<APlayerPawn>(OtherActor))
	{
		if (OtherComp != Player->GetCollisionComponent()) return;
		HTR_LOG(Warning, "[Ghost Collision] Ghost '{}' collided with Player '{}'!", GetName(), Player->GetName());
		if (UWorld* World = GetWorld())
			if (APacmanGameMode* Mode = Cast<APacmanGameMode>(World->GetGameMode()))
				Mode->StartGhostHit(this, Player);
	}
}

void AGhostActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bHitFrozen) return;

	// 이동(Root 위치)을 방해하지 않도록 메쉬에만 로컬 Z Bobbing 애니메이션 적용
	RunningTime += DeltaTime;
	const float DeltaZ = std::sinf(RunningTime * BobbingSpeed) * BobbingHeight;
	BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, DeltaZ));
	EyesMesh->SetRelativeLocation(FVector(0.0f, 0.0f, DeltaZ));

	FRotator BodyRotation = BodyMesh->GetRelativeRotation();
	BodyRotation.Yaw += BodyRotationSpeed * DeltaTime;
	BodyMesh->SetRelativeRotation(BodyRotation);
}

void AGhostActor::SetHitFrozen(bool bFrozen)
{
	if (bHitFrozen == bFrozen) return;
	bHitFrozen = bFrozen;
	if (bFrozen)
	{
		bAITickBeforeHit = AIComponent && AIComponent->IsComponentTickEnabled();
		bLuaTickBeforeHit = LuaScriptComponent && LuaScriptComponent->IsComponentTickEnabled();
	}
	if (AIComponent) AIComponent->SetComponentTickEnabled(bFrozen ? false : bAITickBeforeHit);
	if (LuaScriptComponent) LuaScriptComponent->SetComponentTickEnabled(bFrozen ? false : bLuaTickBeforeHit);
}

bool AGhostActor::IsPlayerInDetectionRange(bool bForceChaseRange) const
{
	if (AIComponent)
	{
		return AIComponent->IsPlayerInDetectionRange(nullptr, bForceChaseRange);
	}
	return false;
}

void AGhostActor::ChangeEye()
{
	if (bEyeChanged || !EyesMesh) return;

	int32 NewEyeSlot = -1;
	int32 NewPupilSlot = -1;
	for (int32 Slot = 0; Slot < EyesMesh->GetNumMaterials(); ++Slot)
	{
		const FString Name = EyesMesh->GetMaterialSlotName(Slot);
		if (Name == "Ghost_Eyes") NewEyeSlot = Slot;
		else if (Name == "Ghost_Pupils") NewPupilSlot = Slot;
	}
	if (NewEyeSlot < 0 || NewPupilSlot < 0) return;

	UTexture2D* White = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
	UTexture2D* FlatNormal = UAssetManager::GetAssetByPath<UTexture2D>("FlatNormalTexture");
	if (!White || !FlatNormal) return;

	auto ConfigureOverride = [this, White, FlatNormal](UMaterial*& Override, int32 Slot, const FVector4& Color) -> bool
	{
		if (!Override)
		{
			UMaterial* Source = EyesMesh->GetMaterial(Slot);
			if (!Source) Source = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			Override = UMaterial::CreateInstance(Source);
		}
		if (!Override) return false;
		Override->Textures.SetNum(2);
		Override->Textures[0] = White;
		Override->Textures[1] = FlatNormal;
		Override->BaseColor = Color;
		Override->Shininess = 32.0f;
		Override->UVScrollSpeed = FVector2(0.0f, 0.0f);
		Override->SamplerState = ESamplerState::LinearClamp;
		Override->BlendState = EBlendState::Opaque;
		return true;
	};
	if (!ConfigureOverride(ChangedEyeOverride, NewEyeSlot, FVector4(0.0f, 0.0f, 0.0f, 1.0f)) ||
		!ConfigureOverride(ChangedPupilOverride, NewPupilSlot, FVector4(1.0f, 0.0f, 0.0f, 1.0f))) return;

	PreviousEyeOverride = EyesMesh->GetOverrideMaterial(NewEyeSlot);
	PreviousPupilOverride = EyesMesh->GetOverrideMaterial(NewPupilSlot);
	EyeSlot = NewEyeSlot;
	PupilSlot = NewPupilSlot;
	EyesMesh->SetMaterial(EyeSlot, ChangedEyeOverride);
	EyesMesh->SetMaterial(PupilSlot, ChangedPupilOverride);
	bEyeChanged = true;
}

void AGhostActor::RestoreEye()
{
	if (!bEyeChanged || !EyesMesh) return;
	EyesMesh->SetMaterial(EyeSlot, PreviousEyeOverride);
	EyesMesh->SetMaterial(PupilSlot, PreviousPupilOverride);
	PreviousEyeOverride = nullptr;
	PreviousPupilOverride = nullptr;
	EyeSlot = PupilSlot = -1;
	bEyeChanged = false;
}

void AGhostActor::ResetToHome()
{
	RestoreEye();
	if (AIComponent)
	{
		AIComponent->ResetToHome();
	}
	else if (InitialLocation != FVector::ZeroVector)
	{
		SetActorLocation(InitialLocation);
	}

	if (LuaScriptComponent)
	{
		sol::table SelfTable = LuaScriptComponent->GetScriptInstance();
		if (SelfTable.valid())
		{
			sol::protected_function ResetFunc = SelfTable["ResetToHome"];
			if (ResetFunc.valid())
			{
				sol::protected_function_result CallResult = ResetFunc(SelfTable);
				if (!CallResult.valid())
				{
					sol::error Err = CallResult;
					HTR_LOG(Error, "[GhostAI] ResetToHome error: {}", Err.what());
				}
			}
			else
			{
				SelfTable["State"] = "Patrol";
				SelfTable["AlertTimer"] = 0.0;
				SelfTable["ChaseTimer"] = 0.0;
				SelfTable["RepathTimer"] = 0.0;
				if (AIComponent)
				{
					AIComponent->MoveToNextPatrol();
				}
			}
		}
	}
	else if (AIComponent)
	{
		AIComponent->MoveToNextPatrol();
	}
}
