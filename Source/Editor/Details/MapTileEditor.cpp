#include "EnginePCH.h"
#include "MapTileEditor.h"
#include "GameFramework/Actor/MapActor.h"
#include "imgui.h"

namespace
{
	const char* TerrainNames[] = { "Floor", "Wall", "Unused" };
	ImU32 ToImColor(const FVector4& Color)
	{
		return ImGui::ColorConvertFloat4ToU32(ImVec4(Color.X, Color.Y, Color.Z, Color.W));
	}
}

FVector4 FMapTileEditor::TerrainColor(EMapTileTerrain Terrain)
{
	switch (Terrain)
	{
	case EMapTileTerrain::Floor: return FVector4(0.2f, 0.75f, 0.4f, 1.f);
	case EMapTileTerrain::Wall: return FVector4(0.95f, 0.25f, 0.2f, 1.f);
	default: return FVector4(0.32f, 0.36f, 0.45f, 1.f);
	}
}

bool FMapTileEditor::IsSelected(const AMapActor& Map, int32 Column, int32 Row) const
{
	return SelectedActor == Map.GetUUID() && Column == SelectedColumn && Row == SelectedRow;
}

int32 FMapTileEditor::GetDisplayColumns(const AMapActor& Map) const
{
	return Map.GetColumns();
}

int32 FMapTileEditor::GetDisplayRows(const AMapActor& Map) const
{
	return Map.GetRows();
}

bool FMapTileEditor::TryDisplayToTile(const AMapActor& Map, int32 DisplayColumn, int32 DisplayRow, int32& OutColumn, int32& OutRow) const
{
	const int32 Width = GetDisplayColumns(Map), Height = GetDisplayRows(Map);
	if (DisplayColumn < 0 || DisplayRow < 0 || DisplayColumn >= Width || DisplayRow >= Height) return false;
	OutColumn = Width - 1 - DisplayColumn;
	OutRow = Height - 1 - DisplayRow;
	return true;
}

void FMapTileEditor::DrawDetails(AMapActor& Map)
{
	if (SelectedActor != Map.GetUUID()) { ClearSelection(); SelectedActor = Map.GetUUID(); }
	if (!ImGui::CollapsingHeader("Map Tiles", ImGuiTreeNodeFlags_DefaultOpen)) return;
	ImGui::PushID(&Map);
	ImGui::TextWrapped("Authored terrain only. Agent clearance, occupancy and collision are separate.");
	ImGui::Checkbox("Show Map Overlay", &bShowOverlay);
	ImGui::DragFloat("Overlay Local Z Offset", &HeightOffset, 0.005f, 0.f, 10.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
	if (const char* Warning = Map.GetGridTransformWarning())
		ImGui::TextColored(ImVec4(1.f, .5f, .1f, 1.f), "Grid disabled: %s", Warning);
	ImGui::TextWrapped("Transform the Actor root. Map mesh relative transform must stay identity. New components attach to the root.");
	int Counts[] = { Map.GetColumns(), Map.GetRows() };
	float Size = Map.GetTileSize();
	FVector Origin = Map.GetGridOrigin();
	bool Changed = ImGui::InputInt2("Columns / Rows", Counts);
	Changed |= ImGui::InputFloat("Local Tile Size", &Size, 0.f, 0.f, "%.6f");
	Changed |= ImGui::InputFloat3("Local Grid Origin", Origin.V, "%.6f");
	if (Changed && !Map.ConfigureGrid(Counts[0], Counts[1], Size, Origin))
		ImGui::TextColored(ImVec4(1.f, .5f, .1f, 1.f), "Rejected: positive finite size, 1..256 columns/rows required.");
	ImGui::TextDisabled("Resize preserves overlapping coordinates; new tiles = Floor.");
	if (ImGui::Button("Reset to Initial Grid"))
	{
		if (Map.ResetGridToInitial()) SelectedColumn = SelectedRow = -1;
	}
	if (ImGui::Button("Generate Wall Collision Boxes"))
	{
		Map.GenerateWallCollisionComponents();
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Wall Collision Boxes"))
	{
		Map.ClearWallCollisionComponents();
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Restore 56 x 48 tiles, size 0.5, the current mesh's floor height and mesh-derived terrain with initial Wall overrides.\nMesh and Actor transform are preserved. Tile edits will be replaced.");
	for (int I = 0; I < 3; ++I)
	{
		if (I) ImGui::SameLine();
		ImGui::PushID(I);
		if (ImGui::Button((FString("Fill ") + TerrainNames[I]).c_str())) Map.FillTiles(static_cast<EMapTileTerrain>(I));
		ImGui::PopID();
	}
	ImGui::TextUnformatted("Fixed layout: +X / columns left, +Y / rows up; (0,0) bottom right");
	const int32 DisplayColumns = GetDisplayColumns(Map), DisplayRows = GetDisplayRows(Map);
	// Clip display slots, then convert each one to the actual (column,row) for drawing AND clicks.
	constexpr float Cell = 22.f, LabelWidth = 36.f;
	if (ImGui::BeginChild("Tile Array", ImVec2(0, 290), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		const float StartX = ImGui::GetCursorPosX();
		const float StartY = ImGui::GetCursorPosY();
		const float ScrollX = ImGui::GetScrollX();
		const int FirstColumn = std::max(0, static_cast<int>((ScrollX - LabelWidth) / Cell));
		const int LastColumn = std::min(DisplayColumns, static_cast<int>((ScrollX + ImGui::GetWindowWidth()) / Cell) + 1);
		ImGuiListClipper Clipper;
		Clipper.Begin(DisplayRows + 1, Cell);
		while (Clipper.Step())
			for (int DisplayRow = Clipper.DisplayStart; DisplayRow < Clipper.DisplayEnd; ++DisplayRow)
			{
				ImGui::SetCursorPos(ImVec2(StartX, StartY + DisplayRow * Cell));
				if (DisplayRow < DisplayRows)
					ImGui::Text("%d", DisplayRows - 1 - DisplayRow);
				for (int DisplayColumn = FirstColumn; DisplayColumn < LastColumn; ++DisplayColumn)
				{
					ImGui::SetCursorPos(ImVec2(StartX + LabelWidth + DisplayColumn * Cell, StartY + DisplayRow * Cell));
					if (DisplayRow == DisplayRows)
					{ ImGui::Text("%d", DisplayColumns - 1 - DisplayColumn); continue; }
					int32 Column, Row;
					if (!TryDisplayToTile(Map, DisplayColumn, DisplayRow, Column, Row)) continue;
					EMapTileTerrain Terrain;
					Map.TryGetTile(Column, Row, Terrain);
					ImGui::PushID(Row * Map.GetColumns() + Column);
					ImGui::InvisibleButton("tile", ImVec2(Cell, Cell));
					const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
					FVector4 Color = TerrainColor(Terrain);
					const float Shade = ((Column + Row) & 1) ? .85f : 1.f;
					Color.X *= Shade; Color.Y *= Shade; Color.Z *= Shade;
					ImGui::GetWindowDrawList()->AddRectFilled(Min, Max, ToImColor(Color));
					ImGui::GetWindowDrawList()->AddRect(Min, Max,
						IsSelected(Map, Column, Row) ? IM_COL32(255, 230, 30, 255) : IM_COL32(20, 20, 25, 220), 0.f, 0, IsSelected(Map, Column, Row) ? 3.f : 1.f);
					if (ImGui::IsItemClicked()) { SelectedColumn = Column; SelectedRow = Row; }
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("(%d, %d) %s", Column, Row, TerrainNames[static_cast<int>(Terrain)]);
					ImGui::PopID();
				}
			}
		ImGui::SetCursorPos(ImVec2(StartX, StartY + (DisplayRows + 1) * Cell));
		ImGui::Dummy(ImVec2(LabelWidth + DisplayColumns * Cell, 1));
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();
	if (Map.IsValidTile(SelectedColumn, SelectedRow))
	{
		EMapTileTerrain Terrain;
		Map.TryGetTile(SelectedColumn, SelectedRow, Terrain);
		ImGui::Text("Selected (column, row): (%d, %d)", SelectedColumn, SelectedRow);
		int Type = static_cast<int>(Terrain);
		if (ImGui::Combo("Terrain", &Type, TerrainNames, 3)) Map.SetTile(SelectedColumn, SelectedRow, static_cast<EMapTileTerrain>(Type));
		FVector Center;
		if (Map.TryTileToWorldCenter(SelectedColumn, SelectedRow, Center))
			ImGui::Text("World center: (%.4f, %.4f, %.4f)", Center.X, Center.Y, Center.Z);
		else ImGui::TextDisabled("World center unavailable: unsupported map transform.");
	}
	else { SelectedColumn = SelectedRow = -1; ImGui::TextDisabled("Select a tile to edit its terrain."); }
	ImGui::PopID();
}
