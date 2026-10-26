#pragma once

#include "GameFramework/Actor/StaticMeshActor.h"
#include "Math/Vector2.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <functional>

// Authored terrain only: this is NOT an occupancy or agent-clearance test.
enum class EMapTileTerrain : uint8 { Floor = 0, Wall = 1, Unused = 2 };
class UBoxComponent;

// Values relative to the MapActor root. Extent contains half lengths, never full size.
struct FMapBoxColliderData
{
	FString Name;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation;
	FVector Scale = FVector::OneVector;
	FVector Extent = FVector(0.5f);
	FCollisionSettings Collision;
};

class AMapActor : public AStaticMeshActor
{
	DECLARE_CLASS(AMapActor, AStaticMeshActor)
public:
	AMapActor();
	void BeginPlay() override;
	static constexpr int32 MaxGridDimension = 256;
	static constexpr int32 MaxTileCount = 65536;
	static constexpr EMapTileTerrain DefaultTerrain = EMapTileTerrain::Floor;
	static constexpr int32 InitialColumns = 56;
	static constexpr int32 InitialRows = 48;
	static constexpr float InitialTileSize = 0.5f;

	int32 GetColumns() const { return Columns; }
	int32 GetRows() const { return Rows; }
	float GetTileSize() const { return TileSize; }
	const FVector& GetGridOrigin() const { return GridOrigin; }
	uint64 GetGridRevision() const { return GridRevision; }
	// Same local boxes used for runtime wall collision and editor debug preview.
	TArray<FBox> GetWallCollisionLocalBoxes() const;
	bool HasExistingWallCollision() const;
	bool GenerateWallCollisionComponents(FString* OutError = nullptr, const std::function<void()>& BeforeReplace = {});
	void ClearWallCollisionComponents();
	TArray<UBoxComponent*> GetBoxCollisionComponents() const;
	static constexpr int32 MaxBoxColliderCount = MaxTileCount;
	bool CaptureBoxColliders(TArray<FMapBoxColliderData>& OutBoxes, FString& Error) const;
	bool SaveBoxCollidersToJson(const FString& Path, FString& Error, bool bAllowEmpty = false) const;
	static bool ReadBoxCollidersFromJson(const FString& Path, TArray<FMapBoxColliderData>& OutBoxes, FString& Error);
	bool ReplaceBoxColliders(const TArray<FMapBoxColliderData>& Boxes, FString& Error,
		const std::function<void()>& BeforeReplace = {});
	bool IsValidTile(int32 Column, int32 Row) const;
	bool TryGetTile(int32 Column, int32 Row, EMapTileTerrain& OutTerrain) const;
	// Owning copy of current terrain, including edits/loaded data. Index = Row * Columns + Column.
	// Capture GetColumns()/GetRows() with the copy; later edits/resizes do not update it.
	TArray<EMapTileTerrain> GetTilesCopy() const;
	bool SetTile(int32 Column, int32 Row, EMapTileTerrain Terrain);
	bool FillTiles(EMapTileTerrain Terrain);
	// Restore the creation grid: mesh analysis, then authored Wall overrides. Keeps mesh and Actor transform.
	// Uses the current map mesh's floor height; missing mesh data fails without changing the grid.
	bool ResetGridToInitial();
	// Atomic configuration change. Preserve overlapping (column,row), new cells = Floor.
	// Does not scale the mesh. Invalid input leaves the entire grid unchanged.
	bool ConfigureGrid(int32 NewColumns, int32 NewRows, float NewTileSize, const FVector& NewOrigin);

	// Inverse full Actor/root transform, then local XY projection (local Z is ignored).
	// Half-open bounds: [origin, origin + dimensions * size). Never clamps outside points.
	// All Try APIs leave output arguments unchanged on failure.
	bool TryWorldToTileXYProjected(const FVector& WorldPosition, int32& OutColumn, int32& OutRow) const;
	bool TryWorldToGridUVProjected(const FVector& WorldPosition, FVector2& OutUV) const;
	bool TryTileToWorldCenter(int32 Column, int32 Row, FVector& OutWorldCenter) const;
	bool TryGetGridToWorld(FMatrix& OutMatrix) const;
	// nullptr = supported. Also rejects a separately moved/rotated/scaled mesh or missing mesh component.
	// Shared root is authoritative; attach future wall components to that root.
	const char* GetGridTransformWarning() const;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	// Mesh analysis followed by Wall-only overrides at creation/reset. Resize/load/edit keep their data.
	void InitializeDefaultTerrain();
	void InitializeDefaultTerrainFromMesh();
	void ApplyInitialWallOverrides();
	static bool IsValidConfiguration(int32 InColumns, int32 InRows, float InSize, const FVector& InOrigin);
	static bool IsValidTerrain(EMapTileTerrain Terrain);
	void RebuildWallCollision();
	int32 Columns = InitialColumns;
	int32 Rows = InitialRows;
	float TileSize = InitialTileSize;
	FVector GridOrigin = FVector(-14.0f, -12.0f, -0.978149f);
	TArray<EMapTileTerrain> Tiles;
	uint64 GridRevision = 0;
};
