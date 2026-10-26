#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "UI/Widget.h"
#include <vector>

class FViewportsPanel;

class FWidgetEditorPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "UI Editor"; }
	bool OpenAsset(const FString& Path);
	bool RequestOpenAsset(const FString& Path);
	const FString& GetAssetPath() const { return AssetPath; }
	void ReleaseAsset();
	void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }

private:
	void NewAsset();
	void SaveAsset();
	void AddWidget(int Type);
	void DeleteSelected();
	void DuplicateSelected();
	void Undo();
	void Redo();
	void ResetHistory();
	void RestoreHistory();
	void DrawHierarchy(UWidget* Widget);
	void ApplyReparent();
	void DrawCanvas();
	void DrawPreview(UWidget* Widget, ImDrawList* DrawList, ImVec2 Origin, float Scale);
	void DrawDetails();
	UWidget* HitTest(UWidget* Widget, ImVec2 Point) const;
	void Select(UWidget* Widget);
	void MarkDirty();
	void RequestNewAsset();
	void ApplyPendingAction();
	enum class EPendingAction { None, New, Open };

	TUniquePtr<UCanvasWidget> Root;
	UWidget* Selected = nullptr;
	FString AssetPath;
	char PathBuffer[512]{};
	char NameBuffer[256]{};
	char ValueBuffer[512]{};
	char FontBuffer[512]{};
	bool bDirty = false;
	bool bResizing = false;
	bool bPreviewHover = false;
	EPendingAction PendingAction = EPendingAction::None;
	FString PendingPath;
	FString ActionStatus;
	int32 NewWidgetIndex = 1;
	float PreviewWidth = 1280.0f;
	float PreviewHeight = 720.0f;
	FViewportsPanel* ViewportsPanel = nullptr;
	bool bPreviewSizeInitialized = false;
	std::vector<FString> History;
	FString SavedSnapshot;
	size_t HistoryIndex = 0;
	bool bHistoryTransactionOpen = false;
	ImGuiID LastHistoryActiveID = 0;
	UWidget* PendingDragged = nullptr;
	UCanvasWidget* PendingParent = nullptr;
};
