#pragma once

#include "GameFramework/Actor/StaticMeshActor.h"
#include "Math/Vector2.h"
#include "Serialization/StructuredArchive.h"

// Authored terrain only: this is NOT an occupancy or agent-clearance test.
enum class EMapTileTerrain : uint8 { Floor, Wall, Unused };

class AMapActor : public AStaticMeshActor
{
	DECLARE_CLASS(AMapActor, AStaticMeshActor)
public:
	AMapActor();
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
	bool IsValidTile(int32 Column, int32 Row) const;
	bool TryGetTile(int32 Column, int32 Row, EMapTileTerrain& OutTerrain) const;
	bool SetTile(int32 Column, int32 Row, EMapTileTerrain Terrain);
	bool FillTiles(EMapTileTerrain Terrain);
	// Restore the creation grid and mesh-seeded terrain. Keeps mesh and Actor transform.
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
	// Seed at creation or explicit reset. Resize/load/edit never regenerates it automatically.
	void InitializeDefaultTerrainFromMesh();
	static bool IsValidConfiguration(int32 InColumns, int32 InRows, float InSize, const FVector& InOrigin);
	static bool IsValidTerrain(EMapTileTerrain Terrain);
	int32 Columns = InitialColumns;
	int32 Rows = InitialRows;
	float TileSize = InitialTileSize;
	FVector GridOrigin = FVector(-14.0f, -12.0f, -0.978149f);
	TArray<EMapTileTerrain> Tiles;
	uint64 GridRevision = 0;
};
