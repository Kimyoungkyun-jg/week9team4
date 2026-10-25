#pragma once

#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/StaticMeshData.h"

class UWorld;
class FMapTileEditor;

// Finite local-space colored geometry. No mesh UVs, world-grid settings, or runtime hooks.
class FMapTileRenderer
{
public:
	void Init();
	void OnRender(const UWorld& World, const FMatrix& ViewProjection, const FMapTileEditor& Editor);
private:
	void Flush();
	void AddVertex(const FVector& Position, const FVector4& Color);
	static constexpr uint32 BatchSize = 8190; // Divisible by 2 and 3.
	FPipelineState Pipeline;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TArray<FLineBatchVertex> Vertices;
};
