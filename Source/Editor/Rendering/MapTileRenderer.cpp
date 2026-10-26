#include "EnginePCH.h"
#include "MapTileRenderer.h"
#include "Editor/Details/MapTileEditor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor/MapActor.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

void FMapTileRenderer::Init()
{
	Pipeline.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/MapTileOverlay.hlsl");
	Pipeline.RasterizerState = ERasterizerState::SolidNone;
	Pipeline.BlendState = EBlendState::AlphaBlend;
	Pipeline.DepthStencilState = EDepthStencilState::ReadOnly;
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BatchSize * sizeof(FLineBatchVertex), sizeof(FLineBatchVertex));
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
	Vertices.Reserve(BatchSize);
}

void FMapTileRenderer::AddVertex(const FVector& Position, const FVector4& Color)
{
	Vertices.Add({ Position, FVector2(), Color });
	if (Vertices.Num() == BatchSize) Flush();
}

void FMapTileRenderer::Flush()
{
	if (Vertices.IsEmpty()) return;
	RenderCommand::BindPipelineState(Pipeline);
	RenderCommand::UpdateBufferData(VertexBuffer.get(), Vertices.GetData(), Vertices.Num() * sizeof(FLineBatchVertex));
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex);
	RenderCommand::Draw(Vertices.Num(), 0);
	Vertices.Reset();
}

void FMapTileRenderer::OnRender(const UWorld& World, const FMatrix& VP, const FMapTileEditor& Editor)
{
	if (!Editor.bShowOverlay || World.GetWorldType() != EWorldType::Editor || !World.GetPersistentLevel() ||
		!Pipeline.Shader || !Pipeline.Shader->VertexShader || !Pipeline.Shader->PixelShader || !VertexBuffer || !ConstantBuffer) return;
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &VP, sizeof(VP));
	for (AActor* Actor : World.GetPersistentLevel()->GetActors())
	{
		AMapActor* Map = Cast<AMapActor>(Actor);
		FMatrix Matrix;
		if (!Map || !Map->TryGetGridToWorld(Matrix)) continue;
		const float Size = Map->GetTileSize();
		const FVector Origin = Map->GetGridOrigin() + FVector(0, 0, Editor.HeightOffset);
		auto Point = [&](float Column, float Row, float Lift = 0.f)
		{
			return Matrix.TransformPosition(Origin + FVector(Column * Size, Row * Size, Lift));
		};
		Pipeline.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		for (int32 Row = 0; Row < Map->GetRows(); ++Row)
			for (int32 Column = 0; Column < Map->GetColumns(); ++Column)
			{
				EMapTileTerrain Terrain;
				Map->TryGetTile(Column, Row, Terrain);
				FVector4 Color = FMapTileEditor::TerrainColor(Terrain);
				const float Shade = ((Column + Row) & 1) ? .68f : 1.f;
				Color.X *= Shade; Color.Y *= Shade; Color.Z *= Shade; Color.W = .38f;
				if (Editor.IsSelected(*Map, Column, Row)) Color = FVector4(1.f, .85f, .05f, .65f);
				const FVector A = Point(float(Column), float(Row)), B = Point(float(Column + 1), float(Row));
				const FVector C = Point(float(Column + 1), float(Row + 1)), D = Point(float(Column), float(Row + 1));
				for (const FVector& V : { A, B, C, A, C, D }) AddVertex(V, Color);
			}
		Flush();
		Pipeline.Topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		const FVector4 LineColor(.8f, .9f, 1.f, .85f);
		for (int32 C = 0; C <= Map->GetColumns(); ++C)
		{
			AddVertex(Point(float(C), 0, Size * .001f), LineColor);
			AddVertex(Point(float(C), float(Map->GetRows()), Size * .001f), LineColor);
		}
		for (int32 R = 0; R <= Map->GetRows(); ++R)
		{
			AddVertex(Point(0, float(R), Size * .001f), LineColor);
			AddVertex(Point(float(Map->GetColumns()), float(R), Size * .001f), LineColor);
		}
		// Selected outline is drawn after the grid and slightly above its fill.
		for (int32 R = 0; R < Map->GetRows(); ++R)
			for (int32 C = 0; C < Map->GetColumns(); ++C)
				if (Editor.IsSelected(*Map, C, R))
				{
					const FVector P[] = { Point(float(C), float(R), Size*.002f), Point(float(C+1), float(R), Size*.002f),
						Point(float(C+1), float(R+1), Size*.002f), Point(float(C), float(R+1), Size*.002f) };
					for (int I = 0; I < 4; ++I) { AddVertex(P[I], FVector4(1,1,0,1)); AddVertex(P[(I+1)%4], FVector4(1,1,0,1)); }
				}
		Flush();
	}
}
