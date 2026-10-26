#pragma once

#include "Render/SceneRenderer.h"

class FSceneDepthRenderer;
class FCascadeFloorRenderer;
class FEditorViewportClient;
class FGridRenderer;
class FMapTileRenderer;
class FMapTileEditor;
class FGizmoRenderer;
class FLineBatcher;
class FOutline;
class FOutlineRenderer;
class FGizmo;
class FTextRenderer;
class UFont;
class UWorld;
struct FEditorSettings;
struct FViewportSettings;

struct FEditorOverlayResources
{
	FGridRenderer* Grid = nullptr;
	FGizmoRenderer* GizmoRenderer = nullptr;
	FLineBatcher* Lines = nullptr;
	FOutline* Outline = nullptr;
	FOutlineRenderer* OutlineRenderer = nullptr;
	FGizmo* Gizmo = nullptr;
	FTextRenderer* Text = nullptr;
	UFont* Font = nullptr;
	UWorld* EditorWorld = nullptr;
	UWorld* ActiveWorld = nullptr;
	const FEditorSettings* Settings = nullptr;
	FMapTileRenderer* MapTiles = nullptr;
	const FMapTileEditor* MapTileEditor = nullptr;
	bool bAllowCollisionDebug = true;
};

struct FSceneOverlayCallbacks
{
	std::function<void()> EditorLines;
	std::function<void()> CollisionLines;
	std::function<void()> EditorGrid;
	std::function<void()> EditorMapTiles;
	std::function<void()> EditorOutline;
	std::function<void()> EditorGizmo;
	std::function<void()> SIEOutline;
	std::function<void()> SIEGizmo;
	bool bClearEditorDepth = false;
	std::function<void()> EditorUUID;
};

class FEditorViewPasses
{
public:
	FEditorViewPasses(const FSceneRenderOptions& InOptions, const FSceneView& InView,
		FTexture2D* InColor, FTexture2D* InDepth, FSceneDepthRenderer* InSceneDepth,
		FCascadeFloorRenderer* InCascadeFloor)
		: Options(InOptions), View(InView), Color(InColor), Depth(InDepth),
		SceneDepth(InSceneDepth), CascadeFloor(InCascadeFloor) {}

	void ConfigureViewportOverlays(const FEditorOverlayResources& Resources,
		FEditorViewportClient* ViewClient, bool bIsPIE, bool bIsActiveViewport);
	void ConfigureSIEOverlays(const FEditorOverlayResources& Resources);

	bool AddPasses(FRenderGraphBuilder& Graph, FSceneRenderer& SceneRenderer, FRenderer* Renderer,
		FRenderGraphTextureRef FinalColor, FRenderGraphTextureRef DepthRef);

private:
	FSceneOverlayCallbacks Overlays;
	void ConfigureCollisionOverlay(const FEditorOverlayResources& Resources);
	void RenderCollisionLines();
	void RenderEditorLines(FEditorViewportClient* ViewClient, bool bDrawSelectedLight, bool bDrawCascadeBounds);
	void RenderEditorGrid(FEditorViewportClient* ViewClient, const FViewportSettings& ViewportSetting);
	void RenderActorUUIDs();
	FEditorOverlayResources OverlayResources;
	FSceneRenderOptions Options;
	const FSceneView& View;
	FTexture2D* Color;
	FTexture2D* Depth;
	FSceneDepthRenderer* SceneDepth;
	FCascadeFloorRenderer* CascadeFloor;
};
