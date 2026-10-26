#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Details/MapTileEditor.h"

#include "Engine/World.h"

struct FTransform;
struct FEditorSettings;
class FImGuiRenderer;
using FShadowPreviewQuery = std::function<bool(const ULightComponent*, FTexture2DArray*&, TArray<FLightShadowRenderData>&)>;

using DetailsSelectionCallback = std::function<void(UActorComponent*)>;
using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void OpenScriptEditor(const FString& Path);

	void SelectComponent(UActorComponent* Component);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }

	void SetTarget(AActor* InTargetActor) 
	{ 
		if (TargetActor != InTargetActor) MapTileEditor.ClearSelection();
		TargetActor = InTargetActor;
		TargetComponent = InTargetActor ? InTargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent) { TargetComponent = InTargetComponent; }

	void SetSelectionCallback(DetailsSelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }
	const ULightComponent* GetSelectedLight() const;
	void SetShadowPreviewQuery(FShadowPreviewQuery InQuery) { QueryShadowPreview = std::move(InQuery); }
	void SetImGuiRenderer(FImGuiRenderer* InRenderer) { ImGuiRenderer = InRenderer; }
	void SetEditorSettings(const FEditorSettings* InSettings) { EditorSettings = InSettings; }

	ImFont* GetCustomFont() { return CustomFont; }
	const FMapTileEditor& GetMapTileEditor() const { return MapTileEditor; }

private:
	FMapTileEditor MapTileEditor;
	void DrawShadowPreview();
	void DrawScriptEditor();
	FShadowPreviewQuery QueryShadowPreview;
	FImGuiRenderer* ImGuiRenderer = nullptr;
	const FEditorSettings* EditorSettings = nullptr;
	float ShadowPreviewContrast = 32.0f;
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent);

	UWorld* World = nullptr;
	ImFont* CustomFont = nullptr;

	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	DetailsSelectionCallback Callback;
	DetailsDeleteComponentCallback DeleteCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;
	bool bIsEditingName = false;

	USceneComponent* PendingDroppedComponent = nullptr;
	USceneComponent* PendingDragComponent = nullptr;
	FString EditingScriptPath;
	std::string EditingScriptText;
	FString ScriptEditorStatus;
	bool bScriptEditorOpen = false;
	bool bScriptEditorDirty = false;
};

