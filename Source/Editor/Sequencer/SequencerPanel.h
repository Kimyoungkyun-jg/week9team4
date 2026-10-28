#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Sequencer/SequenceAsset.h"
#include "Camera/MinimalViewInfo.h"

class UWorld;
class FOutlinerPanel;
class FViewportsPanel;
class FLineBatcher;
class FGizmo;

// 언리얼 스타일 컷신 제작을 위한 시퀀서 에디터 패널
class FSequencerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Sequencer"; }

	void SetWorld(UWorld* InWorld) { if (World != InWorld) StopPilotingCamera(); World = InWorld; }
	void SetOutlinerPanel(FOutlinerPanel* InOutliner) { OutlinerPanel = InOutliner; }
	void SetViewportsPanel(FViewportsPanel* InViewports) { ViewportsPanel = InViewports; }
	void SetGizmo(FGizmo* InGizmo) { Gizmo = InGizmo; }

	bool OpenSequenceAsset(const FString& Path);
	bool SaveSequenceAsset(const FString& Path);
	void CreateNewSequence();

	void DrawTrajectoryLines(FLineBatcher* LineBatcher) const;
	bool IsCameraLockActive() const { return bCameraLock && bHasEvaluatedCameraCut; }
	bool IsPlaying() const { return bIsPlaying; }
	FSequenceCameraEffects GetCameraEffects() const
	{ return FLevelSequenceAsset::EvaluateCameraEffects(SequenceData, bIsPlaying ? PlaybackFrameFloat : static_cast<float>(CurrentFrame)); }

	FLevelSequenceData& GetSequenceData() { return SequenceData; }
	const FLevelSequenceData& GetSequenceData() const { return SequenceData; }

	int32 GetCurrentFrame() const { return CurrentFrame; }
	void SetCurrentFrameAndEvaluate(int32 InFrame, bool bTriggerAudio);

	int32 GetSelectedTrackIndex() const { return SelectedTrackIndex; }
	void SetSelectedTrackIndex(int32 InIndex) { SelectedTrackIndex = InIndex; }

	int32 GetSelectedKeyIndex() const { return SelectedKeyIndex; }
	void SetSelectedKeyIndex(int32 InIndex) { SelectedKeyIndex = InIndex; }

	void AddTrackByType(ESequenceTrackType TrackType);
	void DeleteTrackAt(int32 TrackIndex);
	void DuplicateTrackAt(int32 TrackIndex);
	void ToggleTrackExpandedAt(int32 TrackIndex);

	void AddKeyframeAtCurrentFrame(int32 TrackIndex);
	void DeleteKeyAt(int32 TrackIndex, int32 KeyIndex);
	void CopySelectedKey();
	void PasteCopiedKeyAtCurrentFrame();

private:
	void DrawToolbar();
	void DrawTrackInspector();
	void EvaluateAtCurrentFrame(bool bTriggerAudio);
	void UpdateAutoKey();
	void SyncCameraToActiveViewport();
	void SpawnCutsceneCamera();
	void SpawnSequenceActorInLevel();
	void StartPilotingCamera();
	void StopPilotingCamera();
	void SyncPilotedCameraToViewport();
	int32 FindPilotedTransformTrackIndex() const;

	UWorld* World = nullptr;
	FOutlinerPanel* OutlinerPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FGizmo* Gizmo = nullptr;

	FLevelSequenceData SequenceData;
	FString CurrentAssetPath = "Assets/Sequences/NewSequence.seq.json";
	char AssetPathBuffer[512] = "Assets/Sequences/NewSequence.seq.json";
	FString StatusMessage;

	int32 CurrentFrame = 0;
	float PlaybackFrameFloat = 0.0f;
	float PreviousEvalFrame = -1.0f;
	int32 FirstFrame = 0;
	int32 SelectedTrackIndex = -1;
	int32 SelectedKeyIndex = -1;
	FSequenceTrack CopiedKeyTrack;
	bool bHasCopiedKey = false;
	bool bSequencerExpanded = true;

	bool bIsPlaying = false;
	bool bCameraLock = true;
	bool bAutoKey = false;
	bool bShowTrajectory = true;
	bool bPilotingCamera = false;
	FString PilotedCameraName;
	int32 PilotedViewportIndex = -1;
	int32 LastPilotFrame = -1;
	FVector LastPilotViewLocation{};
	FRotator LastPilotViewRotation{};

	FMinimalViewInfo EvaluatedCameraPOV{};
	bool bHasEvaluatedCameraCut = false;

	FTransform LastGizmoTransform{};
	AActor* LastGizmoActor = nullptr;
	bool bWasGizmoDragging = false;
};
