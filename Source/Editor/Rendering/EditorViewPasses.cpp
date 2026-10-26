#include "EnginePCH.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/Rendering/EditorViewPasses.h"
#include "Editor/Rendering/SceneDepthRenderer.h"
#include "Editor/Rendering/CascadeFloorRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Rendering/MapTileRenderer.h"
#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/Actor/MapActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Render/LineBatcher.h"
#include "Text/TextRenderer.h"
#include "Text/Font.h"

#include <utility>

namespace
{
	FRenderGraphRasterPassDesc MakeEditorPass(const char* Name, FRenderGraphTextureRef Color,
		FRenderGraphTextureRef Depth = {}, ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		FRenderGraphRasterPassDesc Pass{};
		Pass.Name = Name;
		Pass.ColorAttachments = { { Color, ERenderGraphLoadAction::Load } };
		if (Depth) Pass.DepthAttachment = { Depth, DepthLoad, 1.0f, 0 };
		return Pass;
	}

	bool AddOverlay(FRenderGraphBuilder& Graph, FRenderGraphTextureRef Color, FRenderGraphTextureRef Depth,
		const char* Name, const std::function<void()>& Draw,
		ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		return !Draw || Graph.AddRasterPass(MakeEditorPass(Name, Color, Depth, DepthLoad), Draw);
	}
}

void FEditorViewPasses::ConfigureViewportOverlays(const FEditorOverlayResources& Resources,
	FEditorViewportClient* ViewClient, bool bIsPIE, bool bIsActiveViewport)
{
	if (bIsPIE || !ViewClient || !Resources.Settings) return;
	ConfigureCollisionOverlay(Resources);
	if (Resources.MapTiles && Resources.MapTileEditor && Resources.EditorWorld)
		Overlays.EditorMapTiles = [this]
		{
			OverlayResources.MapTiles->OnRender(*OverlayResources.EditorWorld, View.ViewProjectionMatrix, *OverlayResources.MapTileEditor);
		};
	const FEditorSettings& Settings = *Resources.Settings;
	const FViewportSettings ViewportSetting{ 0, 0, Color->GetWidth(), Color->GetHeight(), 0.0f, 1.0f };
	const bool bDrawSelectedLight = bIsActiveViewport && Resources.Gizmo->GetTarget() &&
		(Cast<ASpotLightActor>(Resources.Gizmo->GetTarget()->GetOwner()) ||
		 Cast<APointLightActor>(Resources.Gizmo->GetTarget()->GetOwner()) ||
		 Cast<ARectLightActor>(Resources.Gizmo->GetTarget()->GetOwner()));
	const bool bDrawCascadeBounds = Settings.bDrawCascadeBounds;
	if (Settings.bDrawBatchLine || bDrawSelectedLight || bDrawCascadeBounds)
		Overlays.EditorLines = [this, ViewClient, bDrawSelectedLight, bDrawCascadeBounds]
		{
			RenderEditorLines(ViewClient, bDrawSelectedLight, bDrawCascadeBounds);
		};
	if (Settings.bDrawBatchLine)
		Overlays.EditorGrid = [this, ViewClient, ViewportSetting]
		{
			RenderEditorGrid(ViewClient, ViewportSetting);
		};
	if (bIsActiveViewport && Resources.Outline->GetTarget())
		Overlays.EditorOutline = [this, ViewportSetting]
		{
			OverlayResources.OutlineRenderer->OnRender(*OverlayResources.Outline,
				View.ViewProjectionMatrix, ViewportSetting);
		};
	if (bIsActiveViewport && Resources.Gizmo->GetTarget())
		Overlays.EditorGizmo = [this]
		{
			OverlayResources.GizmoRenderer->OnRender(*OverlayResources.Gizmo,
				View.ViewProjectionMatrix, View.ViewLocation, !View.bIsPerspective);
		};
	Overlays.bClearEditorDepth = true;
	if (Settings.bShowUUID)
		Overlays.EditorUUID = [this] { RenderActorUUIDs(); };
}

void FEditorViewPasses::ConfigureSIEOverlays(const FEditorOverlayResources& Resources)
{
	ConfigureCollisionOverlay(Resources);
	const FViewportSettings ViewportSetting{ 0, 0, Color->GetWidth(), Color->GetHeight(), 0.0f, 1.0f };
	if (Resources.Outline->GetTarget())
		Overlays.SIEOutline = [this, ViewportSetting]
		{
			OverlayResources.OutlineRenderer->OnRender(*OverlayResources.Outline,
				View.ViewProjectionMatrix, ViewportSetting);
		};
	if (Resources.Gizmo->GetTarget())
		Overlays.SIEGizmo = [this]
		{
			OverlayResources.GizmoRenderer->OnRender(*OverlayResources.Gizmo,
				View.ViewProjectionMatrix, View.ViewLocation, !View.bIsPerspective);
		};
}

void FEditorViewPasses::ConfigureCollisionOverlay(const FEditorOverlayResources& Resources)
{
	OverlayResources = Resources;
	if (Resources.bAllowCollisionDebug && Resources.Settings && Resources.Settings->bDrawAllCollision)
		Overlays.CollisionLines = [this] { RenderCollisionLines(); };
}

void FEditorViewPasses::RenderCollisionLines()
{
	FLineBatcher* Lines = OverlayResources.Lines;
	UWorld* World = OverlayResources.ActiveWorld;
	if (!Lines || !World || !World->GetPersistentLevel()) return;
	Lines->BeginFrame();
	const FVector4 Color(0.0f, 1.0f, 1.0f, 1.0f);
	const FVector4 SelectedColor(1.0f, 0.75f, 0.1f, 1.0f);
	const USceneComponent* Selected = OverlayResources.Gizmo ? OverlayResources.Gizmo->GetTarget() : nullptr;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		if (!Actor || !Actor->GetActorEnableCollision()) continue;
		for (UActorComponent* Component : Actor->GetComponents())
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
				if (Primitive->IsCollisionEnabled())
				{
					const bool bSelectedMapBox = Primitive == Selected && Cast<UBoxComponent>(Primitive) && Cast<AMapActor>(Actor);
					Primitive->DrawCollision(*Lines, bSelectedMapBox ? SelectedColor : Color);
				}
		if (!Actor->HasBegunPlay())
			if (AMapActor* Map = Cast<AMapActor>(Actor))
				if (!Map->HasExistingWallCollision())
				for (const FBox& Bounds : Map->GetWallCollisionLocalBoxes())
					Lines->AddOrientedBox(Bounds, Map->GetRootComponent()->GetWorldMatrix(), Color);
	}
	Lines->OnRender(View.ViewProjectionMatrix);
}

void FEditorViewPasses::RenderEditorLines(FEditorViewportClient* ViewClient,
	bool bDrawSelectedLight, bool bDrawCascadeBounds)
{
	FLineBatcher* Lines = OverlayResources.Lines;
	Lines->BeginFrame();
	if (bDrawCascadeBounds)
	{
		FSceneRenderData& Data = ViewClient->GetRenderData();
		const FVector4 Colors[4] = {
			FVector4(1.f, 0.2f, 0.2f, 1.f), FVector4(1.f, 0.8f, 0.2f, 1.f),
			FVector4(0.2f, 1.f, 0.4f, 1.f), FVector4(0.2f, 0.6f, 1.f, 1.f)
		};
		const FBox ClipBox{ FVector(-1.f, -1.f, 0.f), FVector(1.f, 1.f, 1.f) };
		const uint32 DirectionalOffset = Data.GetDirectionalLightOffset();
		for (uint32 LightIndex = 0; LightIndex < Data.GetDirectionalLightCount(); ++LightIndex)
		{
			const uint32 FirstCascade = Data.VisibleLights[DirectionalOffset + LightIndex].ShadowIndex;
			if (FirstCascade == FLightRenderParameters::InvalidShadowIndex) continue;
			for (uint32 CascadeIndex = 0; CascadeIndex < Data.CascadeShadowCount; ++CascadeIndex)
			{
				const uint32 ShadowIndex = FirstCascade + CascadeIndex;
				if (ShadowIndex >= static_cast<uint32>(Data.LightShadows.Num())) break;
				Lines->AddOrientedBox(ClipBox, Data.LightShadows[ShadowIndex].ViewProjection.Inverse(), Colors[CascadeIndex]);
			}
		}
	}
	const FEditorSettings& Settings = *OverlayResources.Settings;
	if (Settings.bDrawBatchLine && Settings.bDrawBoundingBox)
	{
		Lines->BuildVertexBuffer();
		OverlayResources.EditorWorld->GetPathTracker().OnRender(Lines);
	}
	if (bDrawSelectedLight)
	{
		AActor* Owner = OverlayResources.Gizmo->GetTarget()->GetOwner();
		if (ASpotLightActor* Light = Cast<ASpotLightActor>(Owner))
			Light->GetSpotLightComponent()->DrawDebug(Lines);
		else if (APointLightActor* Light = Cast<APointLightActor>(Owner))
			Light->GetPointLightComponent()->DrawDebug(Lines);
		else if (ARectLightActor* Light = Cast<ARectLightActor>(Owner))
			Light->GetRectLightComponent()->DrawDebug(Lines);
	}
	if (USceneComponent* Target = OverlayResources.Gizmo->GetTarget())
    {
        for (UActorComponent* Component : Target->GetOwner()->GetComponents())
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Primitive->IsCollisionEnabled())
                Primitive->DrawCollision(*Lines, FVector4(0.2f, 1.f, 0.3f, 1.f));
    }
    Lines->OnRender(View.ViewProjectionMatrix);
}

void FEditorViewPasses::RenderEditorGrid(FEditorViewportClient* ViewClient,
	const FViewportSettings& ViewportSetting)
{
	EGridPlane GridPlane = EGridPlane::XY;
	switch (ViewClient->GetViewportType())
	{
	case ELevelViewportType::Front: GridPlane = EGridPlane::YZ; break;
	case ELevelViewportType::Right: GridPlane = EGridPlane::XZ; break;
	default: break;
	}
	const FEditorSettings& Settings = *OverlayResources.Settings;
	if (Settings.bDrawPSGrid && View.bIsPerspective)
		OverlayResources.Grid->OnRenderPSGrid(View.ViewProjectionMatrix, View.ViewLocation,
			Settings, ViewportSetting);
	else
		OverlayResources.Grid->OnRenderBatchGrid(View.ViewProjectionMatrix, View.ViewLocation,
			View.ViewForward, GridPlane, static_cast<float>(Settings.GridSpacing),
			View.bIsPerspective, ViewportSetting);
}

void FEditorViewPasses::RenderActorUUIDs()
{
	const bool bOrtho = !View.bIsPerspective;
	for (AActor* Actor : OverlayResources.ActiveWorld->GetPersistentLevel()->GetActors())
	{
		if (!Actor) continue;
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root) continue;
		FBox Box = Root->CalcBounds();
		FVector UUIDLoc((Box.Min.X + Box.Max.X) * 0.5f,
			(Box.Min.Y + Box.Max.Y) * 0.5f, Box.Max.Z + 0.5f);
		FString Text = "UUID : " + std::to_string(Actor->GetUUID());
		OverlayResources.Text->BuildTextMesh(Text, 0.5f, *OverlayResources.Font);
		FVector Facing = bOrtho ? (View.ViewForward * -1.0f)
			: (View.ViewLocation - UUIDLoc).Normalized();
		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		FVector Right = FVector::Cross(Up, Facing).Normalized();
		FVector RealUp = FVector::Cross(Facing, Right).Normalized();
		FMatrix BillboardWorld = FMatrix::Identity;
		BillboardWorld.M[0][0] = Facing.X;
		BillboardWorld.M[0][1] = Facing.Y;
		BillboardWorld.M[0][2] = Facing.Z;
		BillboardWorld.M[1][0] = Right.X;
		BillboardWorld.M[1][1] = Right.Y;
		BillboardWorld.M[1][2] = Right.Z;
		BillboardWorld.M[2][0] = RealUp.X;
		BillboardWorld.M[2][1] = RealUp.Y;
		BillboardWorld.M[2][2] = RealUp.Z;
		BillboardWorld.M[3][0] = UUIDLoc.X;
		BillboardWorld.M[3][1] = UUIDLoc.Y;
		BillboardWorld.M[3][2] = UUIDLoc.Z;
		OverlayResources.Text->OnRender(Text, BillboardWorld, 0.5f,
			*OverlayResources.Font, View.ViewProjectionMatrix);
	}
}

bool FEditorViewPasses::AddPasses(FRenderGraphBuilder& Graph, FSceneRenderer& SceneRenderer, FRenderer* Renderer,
	FRenderGraphTextureRef FinalColor, FRenderGraphTextureRef DepthRef)
{
	if (!Options.bShowWorldNormal && Options.bShowSceneDepth && SceneDepth)
	{
		FRenderGraphRasterPassDesc Pass = MakeEditorPass("SceneDepthVisualization", FinalColor);
		Pass.ReadTextures = { DepthRef };
		if (!Graph.AddRasterPass(std::move(Pass), [this]
			{
				SceneDepth->OnRender(View, Depth, Color, Options.SceneDepthRange);
			})) return false;
	}
	if (Options.bShowLightCulling && Options.bDrawPrimitives && !Options.bShowWorldNormal &&
		!SceneRenderer.AddLightCullingDebugPass(Graph, Renderer, FinalColor, DepthRef))
		return false;
	if (Options.bShowCascade && CascadeFloor)
	{
		FRenderGraphRasterPassDesc Pass = MakeEditorPass("CascadeFloorVisualization", FinalColor);
		Pass.ReadTextures = { DepthRef };
		if (!Graph.AddRasterPass(std::move(Pass), [this, &SceneRenderer]
			{
				CascadeFloor->OnRender(View, SceneRenderer.GetRenderData(), Depth, Color);
			})) return false;
	}

	if (!AddOverlay(Graph, FinalColor, DepthRef, "EditorLines", Overlays.EditorLines) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "EditorMapTiles", Overlays.EditorMapTiles) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "EditorGrid", Overlays.EditorGrid) ||
		!AddOverlay(Graph, FinalColor, {}, "CollisionLines", Overlays.CollisionLines) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "EditorOutline", Overlays.EditorOutline) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "EditorGizmo", Overlays.EditorGizmo, ERenderGraphLoadAction::Clear) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "SIEOutline", Overlays.SIEOutline) ||
		!AddOverlay(Graph, FinalColor, DepthRef, "SIEGizmo", Overlays.SIEGizmo, ERenderGraphLoadAction::Clear))
		return false;
	if (Overlays.bClearEditorDepth)
	{
		FRenderGraphRasterPassDesc ClearDepth{};
		ClearDepth.Name = "EditorClearDepth";
		ClearDepth.DepthAttachment = { DepthRef, ERenderGraphLoadAction::Clear, 1.0f, 0 };
		if (!Graph.AddRasterPass(std::move(ClearDepth), [] {})) return false;
	}
	return AddOverlay(Graph, FinalColor, DepthRef, "EditorUUID", Overlays.EditorUUID);
}
