#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameMode.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "PathTracker.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Engine/EngineTypes.h"
#include "Render/SceneRenderData.h"

//class ACameraActor;
class ULevel;
class UBillboardComponent;
class APawn;
class APlayerController;
class UGameInstance;
class AWorldSettings;

struct FViewContext;

struct FCollisionQueryStats
{
    uint64 IndexBuilds = 0;
    uint64 IndexRefits = 0;
    uint64 GeometryUpdates = 0;
    uint64 SweepQueries = 0;
    uint64 NarrowPhaseTests = 0;
};

class UWorld : public UObject
{
	DECLARE_CLASS(UWorld, UObject)
	REFLECT_START(ClassName)
		CLASS_PROPERTY(GameModeClass, AGameMode)
		CLASS_PROPERTY(DefaultPawnClass, APawn)
		CLASS_PROPERTY(PlayerControllerClass, APlayerController)
		CLASS_PROPERTY(HUDClassOverride, AGameHUD)
		PROPERTY(InitialUIOverride)
	REFLECT_END()

public:
	UWorld() = default;
	virtual ~UWorld();

	bool Init();
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);

	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		return CastChecked<T>(SpawnActor(T::StaticClass(), InName, Transform));
	}

	void Tick(float DeltaSeconds);
	// Dilated and corrected World frame delta, before Actor CustomTimeDilation.
	float GetDeltaSeconds() const { return DeltaTimeSeconds; }
	AWorldSettings* GetWorldSettings() const;

	void ClearWorld();

	void GatherRenderPackets(FRenderQueue& RenderArray, const FViewContext* LODView = nullptr, const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr);

	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	const TArray<ULevel*>& GetLevels() const { return Levels; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }

	FPathTracker& GetPathTracker() { return PathTracker; }

	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);
	void FlushPendingDestruction();

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix(*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit,
		FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr);
	// bReportInitialPenetration=true: 초기 침투를 모두 보존하고, 이후 Hit도 첫 비침투 블록까지 수집한다.
	bool ComponentSweepMulti(TArray<FHitResult>& OutHits, UPrimitiveComponent* Primitive, const FVector& Start, const FVector& End, const FQuat& Rotation, bool bReportInitialPenetration = false);
	bool LineTraceByChannel(const FRay& Ray, float Distance, ECollisionChannel Channel, FHitResult& OutHit, const AActor* IgnoreActor = nullptr);
	// Query-only sphere: closest Block response, including initial penetration; no Hit/Overlap dispatch.
	bool SphereSweepSingleByChannel(const FVector& Start, const FVector& End, float Radius, ECollisionChannel Channel, FHitResult& OutHit, const AActor* IgnoreActor = nullptr);
	bool OverlapMulti(const UPrimitiveComponent& Query, const FCollisionGeometry& Geometry, TArray<UPrimitiveComponent*>& OutComponents);
	void RegisterCollisionComponent(UPrimitiveComponent* Component);
	void UnregisterCollisionComponent(UPrimitiveComponent* Component);
	void MarkCollisionDirty(UPrimitiveComponent* Component);
	void RemoveCollisionListeners(const void* Listener);
	const FCollisionQueryStats& GetCollisionQueryStats() const { return CollisionQueryStats; }

	void BeginPlay();
	void EndPlay();
	bool HasBegunPlay() const { return bBegunPlay; }
	APawn* GetPlayerPawn() const { return PlayerPawn; }
	APlayerController* GetPlayerController() const { return PlayerController; }
	AGameMode* GetGameMode() const { return GameMode; }
	AGameHUD* GetHUD() const { return HUD; }
	UClass* GetGameModeClass() const { return GameModeClass; }
	UClass* GetHUDClassOverride() const { return HUDClassOverride; }
	UClass* GetHUDClass() const;
	bool SetGameModeClass(UClass* Class);
	bool SetHUDClassOverride(UClass* Class);
	const FString& GetInitialUIOverride() const { return InitialUIOverride; }
	void SetInitialUIOverride(const FString& Path) { InitialUIOverride = Path; }
	UClass* GetDefaultPawnClass() const;
	UClass* GetPlayerControllerClass() const;
	bool SetDefaultPawnClass(UClass* Class);
	bool SetPlayerControllerClass(UClass* Class);
	bool IsPaused() const { return bIsPaused; }
	void SetPaused(bool bPaused);

	// Compatibility API for Lua: AWorldSettings owns global time dilation.
	float GetWorldTimeDilation() const;
	void SetWorldTimeDilation(float InTimeDilation);

	FScene& GetScene() { return Scene; }
	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }

	const FRenderStats& GetRenderStats() const { return RenderStats; }

	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	// GameInstance
	UGameInstance* GetGameInstance() const;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
private:
	struct FCollisionEntry { UPrimitiveComponent* Component; FCollisionGeometry Geometry; FBox Bounds; };
	TArray<UPrimitiveComponent*> CollisionComponents;
	TArray<FCollisionEntry> CollisionEntries;
	TBVH<uint32> CollisionBVH{ [this](const uint32& Index) { return CollisionEntries[Index].Bounds; } };
	bool bCollisionTopologyDirty = true;
	TArray<UPrimitiveComponent*> DirtyCollisionComponents;
	FCollisionQueryStats CollisionQueryStats;
	void UpdateCollisionIndex();
	friend class AActor;
	friend class APlayerCameraManager;
	friend class UActorComponent;
	friend class USceneComponent;
	friend class UPrimitiveComponent;
	friend class UMovementComponent;
	friend class UCharacterMovementComponent;
	friend class APlayerCameraManager;
	void QueueObjectDeletion(UObject* Object);
	TArray<UObject*> PendingDeletion;
	bool bIsTicking = false;
	bool bIsClearingWorld = false;
	bool bFlushingDestruction = false;
	int32 ActiveObjectCalls = 0;
	struct FScopedObjectExecution
	{
		explicit FScopedObjectExecution(UWorld* InWorld) : World(InWorld)
		{ if (World) ++World->ActiveObjectCalls; }
		~FScopedObjectExecution() { if (World) --World->ActiveObjectCalls; }
		FScopedObjectExecution(const FScopedObjectExecution&) = delete;
		FScopedObjectExecution& operator=(const FScopedObjectExecution&) = delete;
		UWorld* World;
	};
	EWorldType WorldType = EWorldType::None;

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets;
		TArray<uint32> SlowPathIndices;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};

	TArray<FGatherChunk> GatherChunks;
	void GatherPacketChunk(uint32 Begin, uint32 End, uint32 ChunkIndex,
		const FViewContext* LODView, const uint8* OccludedMask, uint8* SlotDest);

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;

	TQueue<AActor*> BeginPlayList;

	//메인 카메라 
	ACameraActor* MainCamera = nullptr;
	APawn* PlayerPawn = nullptr;
	APlayerController* PlayerController = nullptr;
	AGameMode* GameMode = nullptr;
	AGameHUD* HUD = nullptr;
	UClass* GameModeClass = AGameMode::StaticClass();
	UClass* DefaultPawnClass = nullptr;
	UClass* PlayerControllerClass = nullptr;
	UClass* HUDClassOverride = nullptr;
	FString InitialUIOverride;
	bool bIsPaused = false;
	float DeltaTimeSeconds = 0.0f;
	float DeltaRealTimeSeconds = 0.0f;
	bool bBegunPlay = false;

	FPathTracker PathTracker;

	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;

	// GatherRenderPackets가 매 프레임 채우는 컬링 결과. 용량을 재사용한다.
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;

	FRenderStats RenderStats;

};
