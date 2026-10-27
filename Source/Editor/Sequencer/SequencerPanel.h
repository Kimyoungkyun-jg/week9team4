#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Sequencer/LevelSequence.h"
#include "Sequencer/SequencePlayer.h"
#include <functional>
#include <utility>
#include <vector>

class AActor;
class UWorld;
class FViewportsPanel;

class FSequencerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Sequencer"; }
	void SetWorld(UWorld* InWorld);
	void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }
	void SetSelectedActorProvider(std::function<AActor*()> InProvider) { SelectedActorProvider = std::move(InProvider); }
	void SetActorSelectionCallback(std::function<void(AActor*)> InCallback) { ActorSelectionCallback = std::move(InCallback); }
	void SetPreviewAllowedQuery(std::function<bool()> InQuery) { PreviewAllowedQuery = std::move(InQuery); }
	void StopPreview();
	void ReleaseAsset();

private:
	enum class EDragMode { None, Scrub, Key, SectionMove, SectionStart, SectionEnd };
	void NewAsset();
	void RequestNewAsset();
	bool OpenAsset(const FString& Path);
	void RequestOpenAsset(const FString& Path);
	void SaveAsset();
	void ResetHistory();
	void CommitEdit();
	void Undo();
	void Redo();
	void AddBinding(ESequenceBindingType Type, UClass* SpawnClass = nullptr, AActor* SceneActor = nullptr);
	void AddTrack(ESequenceTrackType Type);
	void AddSection();
	void AddKey();
	void DrawToolbar();
	void DrawBindings();
	void DrawTimeline();
	void DrawInspector();
	void DrawPreviewImage();
	void SelectPreviewActor();
	void FocusSelectedBinding();
	bool EnsurePreview();
	void RefreshPreview();
	void FollowSelectedCamera();
	void RestoreViewportCamera();
	AActor* GetSelectedBoundActor();
	FString NextBindingId() const;

	TUniquePtr<ULevelSequence> Sequence;
	TUniquePtr<ULevelSequencePlayer> Player;
	UWorld* World = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	std::function<AActor*()> SelectedActorProvider;
	std::function<void(AActor*)> ActorSelectionCallback;
	std::function<bool()> PreviewAllowedQuery;
	AActor* PreviewSelectedActor = nullptr;
	FString AssetPath;
	char PathBuffer[512]{};
	char ActorSearchBuffer[128]{};
	bool bPickerSpawns = false;
	FString Status;
	std::vector<FString> History;
	FString SavedSnapshot;
	size_t HistoryIndex = 0;
	int32 CurrentFrame = 0;
	int32 SelectedBinding = -1;
	int32 SelectedTrack = -1;
	int32 SelectedSection = -1;
	int32 SelectedKey = -1;
	float PixelsPerFrame = 8.0f;
	float PlayRate = 1.0f;
	bool bLooping = false;
	bool bDirty = false;
	enum class EPendingAction { None, New, Open };
	EPendingAction PendingAction = EPendingAction::None;
	FString PendingOpenPath;
	bool bFollowCamera = false;
	bool bSavedViewportCamera = false;
	int32 SavedViewportIndex = -1;
	FVector SavedViewLocation{};
	FRotator SavedViewRotation{};
	float SavedViewFov = 60.0f;
	EDragMode DragMode = EDragMode::None;
	int32 DragStartFrame = 0;
	int32 DragOriginalFrame = 0;
	int32 DragOriginalStart = 0;
	int32 DragOriginalEnd = 0;
	std::vector<int32> DragOriginalKeyFrames;
	bool bDragChanged = false;
};
