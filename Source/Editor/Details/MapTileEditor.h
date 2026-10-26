#pragma once

#include "Core/Types.h"
#include "Math/Vector4.h"
#include "GameFramework/Actor/MapActor.h"

class AMapActor;
enum class EMapTileTerrain : uint8;

// Session-only state. No reference to this type is needed by the runtime Actor.
class FMapTileEditor
{
public:
	void DrawDetails(AMapActor& Map, const std::function<void()>& BeforeBoxesRemoved = {});
	void ClearSelection() { SelectedActor = 0; SelectedColumn = SelectedRow = -1; }
	bool IsSelected(const AMapActor& Map, int32 Column, int32 Row) const;
	// Fixed editor layout: columns increase leftward, rows upward. No camera dependence.
	int32 GetDisplayColumns(const AMapActor& Map) const;
	int32 GetDisplayRows(const AMapActor& Map) const;
	bool TryDisplayToTile(const AMapActor& Map, int32 DisplayColumn, int32 DisplayRow, int32& OutColumn, int32& OutRow) const;
	static FVector4 TerrainColor(EMapTileTerrain Terrain);
	bool bShowOverlay = true;
	float HeightOffset = 0.025f; // Local Z, scales with the map.
private:
	void DrawBoxColliderDetails(AMapActor& Map, const std::function<void()>& BeforeBoxesRemoved);
	enum class EBoxAction { None, SaveEmpty, GenerateTiles, GenerateJson, Clear };
	EBoxAction PendingBoxAction = EBoxAction::None;
	TArray<FMapBoxColliderData> PendingBoxes;
	uint32 BoxActionActor = 0;
	char ColliderJsonPath[1024] = "MapBoxColliders.json";
	FString ColliderStatus;
	FString PendingJsonPath;
	uint32 SelectedActor = 0;
	int32 SelectedColumn = -1;
	int32 SelectedRow = -1;
};
