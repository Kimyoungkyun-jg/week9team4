#include "EnginePCH.h"
#include "Editor/Sequencer/SequencerPanel.h"
#include "Editor/Sequencer/ImSequencer.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Sequencer/LevelSequenceActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Render/LineBatcher.h"
#include "Input/InputSystem.h"
#include "Core/EngineLog.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
	// 파일 열기 대화상자를 통해 애셋 경로 선택
	FString BrowseFileDialog(const wchar_t* Filter, const wchar_t* DefExt)
	{
		wchar_t FilePath[1024] = {};
		OPENFILENAMEW Dialog = {};
		Dialog.lStructSize = sizeof(Dialog);
		Dialog.lpstrFile = FilePath;
		Dialog.nMaxFile = 1024;
		Dialog.lpstrFilter = Filter;
		Dialog.lpstrDefExt = DefExt;
		Dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameW(&Dialog))
		{
			return {};
		}
		const auto Utf8 = std::filesystem::path(FilePath).u8string();
		return FString(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
	}
	// 다이아몬드 형태의 키프레임 마커 그리기
	void DrawDiamondMarker(ImDrawList* DrawList, ImVec2 Center, float Radius, unsigned int FillColor, unsigned int BorderColor)
	{
		ImVec2 Points[4] = {
			ImVec2(Center.x, Center.y - Radius),
			ImVec2(Center.x + Radius, Center.y),
			ImVec2(Center.x, Center.y + Radius),
			ImVec2(Center.x - Radius, Center.y)
		};
		DrawList->AddConvexPolyFilled(Points, 4, FillColor);
		DrawList->AddPolyline(Points, 4, BorderColor, ImDrawFlags_Closed, 1.5f);
	}

	// 트랙 타입별 기본 색상 반환
	unsigned int GetTrackDisplayColor(ESequenceTrackType TrackType)
	{
		switch (TrackType)
		{
		case ESequenceTrackType::CameraCut:
			return 0xFF3A6EA5;
		case ESequenceTrackType::ActorTransform:
			return 0xFF3E8E41;
		case ESequenceTrackType::CameraFOV:
			return 0xFF9B59B6;
		case ESequenceTrackType::Audio:
			return 0xFFD35400;
		default:
			return 0xFF555555;
		}
	}

	const char* GetTrackTypeLabel(ESequenceTrackType TrackType)
	{
		switch (TrackType)
		{
		case ESequenceTrackType::CameraCut:
			return "Camera Cut Track";
		case ESequenceTrackType::ActorTransform:
			return "Actor Transform Track";
		case ESequenceTrackType::CameraFOV:
			return "Camera FOV Track";
		case ESequenceTrackType::Audio:
			return "Audio Track";
		default:
			return "Track";
		}
	}

	// 시퀀서 위젯과 패널 데이터를 연결하는 어댑터
	struct FSequencerBridge : public ImSequencer::SequenceInterface
	{
		FSequencerPanel* Panel = nullptr;
		TArray<int32> CachedStartFrames;
		TArray<int32> CachedEndFrames;
		TArray<FString> CachedLabels;

		int32 DraggingTrackIdx = -1;
		int32 DraggingKeyIdx = -1;

		explicit FSequencerBridge(FSequencerPanel* InPanel)
			: Panel(InPanel)
		{
			RebuildCache();
		}

		void RebuildCache()
		{
			FLevelSequenceData& Data = Panel->GetSequenceData();
			const int32 Count = Data.Tracks.Num();
			CachedStartFrames.SetNum(Count);
			CachedEndFrames.SetNum(Count);
			CachedLabels.SetNum(Count);

			for (int32 i = 0; i < Count; ++i)
			{
				const FSequenceTrack& Track = Data.Tracks[i];
				FString Prefix;
				if (Track.TrackType == ESequenceTrackType::CameraCut) Prefix = "[Cut] ";
				else if (Track.TrackType == ESequenceTrackType::ActorTransform) Prefix = "[Xform] ";
				else if (Track.TrackType == ESequenceTrackType::CameraFOV) Prefix = "[FOV] ";
				else if (Track.TrackType == ESequenceTrackType::Audio) Prefix = "[Audio] ";

				FString NamePart = !Track.TrackName.empty() ? Track.TrackName : Track.TargetActorName;
				if (NamePart.empty()) NamePart = GetTrackTypeLabel(Track.TrackType);
				if (Track.bMuted) NamePart += " (Muted)";
				CachedLabels[i] = Prefix + NamePart;

				int32 MinF = Data.StartFrame;
				int32 MaxF = Data.StartFrame;
				if (Track.TrackType == ESequenceTrackType::CameraCut && !Track.CameraCuts.IsEmpty())
				{
					MinF = Track.CameraCuts[0].StartFrame;
					MaxF = Track.CameraCuts.Last().EndFrame;
				}
				else if (Track.TrackType == ESequenceTrackType::ActorTransform && !Track.TransformKeys.IsEmpty())
				{
					MinF = Track.TransformKeys[0].Frame;
					MaxF = Track.TransformKeys.Last().Frame;
				}
				else if (Track.TrackType == ESequenceTrackType::CameraFOV && !Track.FOVKeys.IsEmpty())
				{
					MinF = Track.FOVKeys[0].Frame;
					MaxF = Track.FOVKeys.Last().Frame;
				}
				else if (Track.TrackType == ESequenceTrackType::Audio && !Track.AudioClips.IsEmpty())
				{
					MinF = Track.AudioClips[0].StartFrame;
					MaxF = Track.AudioClips.Last().EndFrame;
				}
				CachedStartFrames[i] = MinF;
				CachedEndFrames[i] = std::max(MinF, MaxF);
			}
		}

		int GetFrameMin() const override
		{
			return Panel->GetSequenceData().StartFrame;
		}

		int GetFrameMax() const override
		{
			return std::max(Panel->GetSequenceData().EndFrame, Panel->GetSequenceData().StartFrame + 1);
		}

		int GetItemCount() const override
		{
			return Panel->GetSequenceData().Tracks.Num();
		}

		int GetItemTypeCount() const override
		{
			return 4;
		}

		const char* GetItemTypeName(int TypeIndex) const override
		{
			return GetTrackTypeLabel(static_cast<ESequenceTrackType>(TypeIndex));
		}

		const char* GetItemLabel(int Index) const override
		{
			if (Index >= 0 && Index < CachedLabels.Num())
			{
				return CachedLabels[Index].c_str();
			}
			return "";
		}

		void Get(int Index, int** Start, int** End, int* Type, unsigned int* Color) override
		{
			FLevelSequenceData& Data = Panel->GetSequenceData();
			if (Index >= CachedStartFrames.Num())
			{
				RebuildCache();
			}
			FSequenceTrack& Track = Data.Tracks[Index];
			if (Type)
			{
				*Type = static_cast<int>(Track.TrackType);
			}
			if (Color)
			{
				*Color = Track.bMuted ? 0xFF444444 : GetTrackDisplayColor(Track.TrackType);
			}

			if (Track.TrackType == ESequenceTrackType::CameraCut && Track.CameraCuts.Num() == 1)
			{
				if (Start) *Start = &Track.CameraCuts[0].StartFrame;
				if (End) *End = &Track.CameraCuts[0].EndFrame;
				return;
			}
			if (Track.TrackType == ESequenceTrackType::Audio && Track.AudioClips.Num() == 1)
			{
				if (Start) *Start = &Track.AudioClips[0].StartFrame;
				if (End) *End = &Track.AudioClips[0].EndFrame;
				return;
			}

			if (Start) *Start = &CachedStartFrames[Index];
			if (End) *End = &CachedEndFrames[Index];
		}

		void Add(int Type) override
		{
			Panel->AddTrackByType(static_cast<ESequenceTrackType>(Type));
			RebuildCache();
		}

		void Del(int Index) override
		{
			Panel->DeleteTrackAt(Index);
			RebuildCache();
		}

		void Duplicate(int Index) override
		{
			Panel->DuplicateTrackAt(Index);
			RebuildCache();
		}

		size_t GetCustomHeight(int Index) override
		{
			const FLevelSequenceData& Data = Panel->GetSequenceData();
			if (Index < 0 || Index >= Data.Tracks.Num())
			{
				return 0;
			}
			return Data.Tracks[Index].bExpanded ? 46 : 0;
		}

		void DoubleClick(int Index) override
		{
			Panel->ToggleTrackExpandedAt(Index);
		}

		void DrawTrackContent(int Index, ImDrawList* DrawList, const ImRect& Rc, const ImRect& ClippingRect, bool bDetailed)
		{
			FLevelSequenceData& Data = Panel->GetSequenceData();
			if (Index < 0 || Index >= Data.Tracks.Num())
			{
				return;
			}

			FSequenceTrack& Track = Data.Tracks[Index];
			const float FrameSpan = static_cast<float>(std::max(GetFrameMax() - GetFrameMin() + 2, 1));
			const float Width = Rc.Max.x - Rc.Min.x;
			const float PixelPerFrame = Width / FrameSpan;
			const float BaseX = Rc.Min.x;
			const float MidY = (Rc.Min.y + Rc.Max.y) * 0.5f;

			ImGuiIO& Io = ImGui::GetIO();
			DrawList->PushClipRect(ClippingRect.Min, ClippingRect.Max, true);

			if (bDetailed)
			{
				DrawList->AddRectFilled(Rc.Min, Rc.Max, 0xFF1B1B1B, 0.0f);
				DrawList->AddLine(ImVec2(Rc.Min.x, MidY), ImVec2(Rc.Max.x, MidY), 0x44666666, 1.0f);
			}

			// 카메라 컷 클립 블록 및 블렌드 구간 표시
			if (Track.TrackType == ESequenceTrackType::CameraCut)
			{
				for (int32 CutIdx = 0; CutIdx < Track.CameraCuts.Num(); ++CutIdx)
				{
					FCameraCutClip& Cut = Track.CameraCuts[CutIdx];
					const float X1 = BaseX + (Cut.StartFrame - GetFrameMin()) * PixelPerFrame;
					const float X2 = BaseX + (Cut.EndFrame - GetFrameMin() + 1) * PixelPerFrame;
					const ImRect CutRect(ImVec2(X1, Rc.Min.y + 3.0f), ImVec2(X2, Rc.Max.y - 3.0f));
					const bool bSelected = (Panel->GetSelectedTrackIndex() == Index && Panel->GetSelectedKeyIndex() == CutIdx);

					DrawList->AddRectFilled(CutRect.Min, CutRect.Max, bSelected ? 0xFF5A9BD5 : 0xFF2E5B88, 3.0f);
					DrawList->AddRect(CutRect.Min, CutRect.Max, bSelected ? 0xFFFFD700 : 0xFF88Bbee, 3.0f);

					if (Cut.BlendTime > 0.0f)
					{
						const float BlendW = std::min(Cut.BlendTime * Data.FrameRate * PixelPerFrame, X2 - X1);
						DrawList->AddTriangleFilled(
							ImVec2(X1, CutRect.Max.y),
							ImVec2(X1 + BlendW, CutRect.Min.y),
							ImVec2(X1, CutRect.Min.y),
							0x66FFFFFF);
					}

					const char* CamName = Cut.CameraActorName.empty() ? "(No Camera)" : Cut.CameraActorName.c_str();
					DrawList->AddText(ImVec2(X1 + 6.0f, MidY - 7.0f), 0xFFFFFFFF, CamName);

					if (ClippingRect.Contains(Io.MousePos) && CutRect.Contains(Io.MousePos) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						Panel->SetSelectedTrackIndex(Index);
						Panel->SetSelectedKeyIndex(CutIdx);
					}
				}
			}
			// 오디오 클립 블록 표시
			else if (Track.TrackType == ESequenceTrackType::Audio)
			{
				for (int32 ClipIdx = 0; ClipIdx < Track.AudioClips.Num(); ++ClipIdx)
				{
					FAudioClip& Clip = Track.AudioClips[ClipIdx];
					const float X1 = BaseX + (Clip.StartFrame - GetFrameMin()) * PixelPerFrame;
					const float X2 = BaseX + (Clip.EndFrame - GetFrameMin() + 1) * PixelPerFrame;
					const ImRect ClipRect(ImVec2(X1, Rc.Min.y + 3.0f), ImVec2(X2, Rc.Max.y - 3.0f));
					const bool bSelected = (Panel->GetSelectedTrackIndex() == Index && Panel->GetSelectedKeyIndex() == ClipIdx);

					DrawList->AddRectFilled(ClipRect.Min, ClipRect.Max, bSelected ? 0xFFE67E22 : 0xFFA04000, 3.0f);
					DrawList->AddRect(ClipRect.Min, ClipRect.Max, bSelected ? 0xFFFFD700 : 0xFFF39C12, 3.0f);

					FString ShortName = Clip.SoundPath.empty() ? "(No Sound)" : std::filesystem::path(Clip.SoundPath).filename().string();
					DrawList->AddText(ImVec2(X1 + 6.0f, MidY - 7.0f), 0xFFFFFFFF, ShortName.c_str());

					if (ClippingRect.Contains(Io.MousePos) && ClipRect.Contains(Io.MousePos) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						Panel->SetSelectedTrackIndex(Index);
						Panel->SetSelectedKeyIndex(ClipIdx);
					}
				}
			}
			// 액터 트랜스폼 다이아몬드 키프레임 표시 및 드래그
			else if (Track.TrackType == ESequenceTrackType::ActorTransform)
			{
				for (int32 KeyIdx = 0; KeyIdx < Track.TransformKeys.Num(); ++KeyIdx)
				{
					FTransformKeyframe& Key = Track.TransformKeys[KeyIdx];
					const float KeyX = BaseX + (Key.Frame - GetFrameMin()) * PixelPerFrame;
					const ImVec2 MarkerPos(KeyX, MidY);

					if (KeyIdx + 1 < Track.TransformKeys.Num())
					{
						const float NextX = BaseX + (Track.TransformKeys[KeyIdx + 1].Frame - GetFrameMin()) * PixelPerFrame;
						DrawList->AddLine(ImVec2(KeyX, MidY), ImVec2(NextX, MidY), 0x8866CC66, 2.0f);
					}

					const bool bSelected = (Panel->GetSelectedTrackIndex() == Index && Panel->GetSelectedKeyIndex() == KeyIdx);
					const ImRect HitBox(ImVec2(KeyX - 7.0f, MidY - 7.0f), ImVec2(KeyX + 7.0f, MidY + 7.0f));
					const bool bHovered = ClippingRect.Contains(Io.MousePos) && HitBox.Contains(Io.MousePos);

					DrawDiamondMarker(
						DrawList,
						MarkerPos,
						bSelected ? 6.5f : 5.5f,
						bSelected ? 0xFFFFCC00 : (bHovered ? 0xFF88FF88 : 0xFF44CC44),
						0xFF111111);

					if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						Panel->SetSelectedTrackIndex(Index);
						Panel->SetSelectedKeyIndex(KeyIdx);
						DraggingTrackIdx = Index;
						DraggingKeyIdx = KeyIdx;
					}
					if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						Track.TransformKeys.RemoveAt(KeyIdx);
						if (Panel->GetSelectedKeyIndex() >= Track.TransformKeys.Num())
						{
							Panel->SetSelectedKeyIndex(Track.TransformKeys.Num() - 1);
						}
						Panel->SetCurrentFrameAndEvaluate(Panel->GetCurrentFrame(), false);
						break;
					}
				}
			}
			// 카메라 시야각 다이아몬드 키프레임 및 곡선 표시
			else if (Track.TrackType == ESequenceTrackType::CameraFOV)
			{
				for (int32 KeyIdx = 0; KeyIdx < Track.FOVKeys.Num(); ++KeyIdx)
				{
					FFOVKeyframe& Key = Track.FOVKeys[KeyIdx];
					const float KeyX = BaseX + (Key.Frame - GetFrameMin()) * PixelPerFrame;
					const float NormFOV = FMath::Clamp((Key.FieldOfView - 20.0f) / 120.0f, 0.0f, 1.0f);
					const float KeyY = bDetailed ? (Rc.Max.y - 6.0f - NormFOV * (Rc.Max.y - Rc.Min.y - 12.0f)) : MidY;
					const ImVec2 MarkerPos(KeyX, KeyY);

					if (KeyIdx + 1 < Track.FOVKeys.Num())
					{
						const FFOVKeyframe& NextKey = Track.FOVKeys[KeyIdx + 1];
						const float NextX = BaseX + (NextKey.Frame - GetFrameMin()) * PixelPerFrame;
						const float NextNorm = FMath::Clamp((NextKey.FieldOfView - 20.0f) / 120.0f, 0.0f, 1.0f);
						const float NextY = bDetailed ? (Rc.Max.y - 6.0f - NextNorm * (Rc.Max.y - Rc.Min.y - 12.0f)) : MidY;
						DrawList->AddLine(MarkerPos, ImVec2(NextX, NextY), 0xAA9B59B6, 2.0f);
					}

					const bool bSelected = (Panel->GetSelectedTrackIndex() == Index && Panel->GetSelectedKeyIndex() == KeyIdx);
					const ImRect HitBox(ImVec2(KeyX - 7.0f, KeyY - 7.0f), ImVec2(KeyX + 7.0f, KeyY + 7.0f));
					const bool bHovered = ClippingRect.Contains(Io.MousePos) && HitBox.Contains(Io.MousePos);

					DrawDiamondMarker(
						DrawList,
						MarkerPos,
						bSelected ? 6.5f : 5.5f,
						bSelected ? 0xFFFFCC00 : (bHovered ? 0xFFD2B4DE : 0xFF9B59B6),
						0xFF111111);

					if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						Panel->SetSelectedTrackIndex(Index);
						Panel->SetSelectedKeyIndex(KeyIdx);
						DraggingTrackIdx = Index;
						DraggingKeyIdx = KeyIdx;
					}
					if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						Track.FOVKeys.RemoveAt(KeyIdx);
						if (Panel->GetSelectedKeyIndex() >= Track.FOVKeys.Num())
						{
							Panel->SetSelectedKeyIndex(Track.FOVKeys.Num() - 1);
						}
						Panel->SetCurrentFrameAndEvaluate(Panel->GetCurrentFrame(), false);
						break;
					}
				}
			}

			// 키프레임 마우스 드래그 이동 처리
			if (DraggingTrackIdx == Index && DraggingKeyIdx >= 0)
			{
				if (Io.MouseDown[0] && PixelPerFrame > 0.001f)
				{
					const int32 NewFrame = FMath::Clamp(
						static_cast<int32>(std::round((Io.MousePos.x - BaseX) / PixelPerFrame)) + GetFrameMin(),
						GetFrameMin(),
						GetFrameMax());

					if (Track.TrackType == ESequenceTrackType::ActorTransform && DraggingKeyIdx < Track.TransformKeys.Num())
					{
						Track.TransformKeys[DraggingKeyIdx].Frame = NewFrame;
						Panel->SetCurrentFrameAndEvaluate(Panel->GetCurrentFrame(), false);
					}
					else if (Track.TrackType == ESequenceTrackType::CameraFOV && DraggingKeyIdx < Track.FOVKeys.Num())
					{
						Track.FOVKeys[DraggingKeyIdx].Frame = NewFrame;
						Panel->SetCurrentFrameAndEvaluate(Panel->GetCurrentFrame(), false);
					}
				}
				else if (!Io.MouseDown[0])
				{
					FLevelSequenceAsset::SortTrackKeys(Track);
					DraggingTrackIdx = -1;
					DraggingKeyIdx = -1;
				}
			}

			DrawList->PopClipRect();
		}

		void CustomDraw(
			int Index,
			ImDrawList* DrawList,
			const ImRect& Rc,
			const ImRect& LegendRect,
			const ImRect& ClippingRect,
			const ImRect& LegendClippingRect) override
		{
			FLevelSequenceData& Data = Panel->GetSequenceData();
			if (Index >= 0 && Index < Data.Tracks.Num())
			{
				const FSequenceTrack& Track = Data.Tracks[Index];
				DrawList->PushClipRect(LegendClippingRect.Min, LegendClippingRect.Max, true);
				FString SubInfo = Track.TargetActorName.empty() ? GetTrackTypeLabel(Track.TrackType) : ("Target: " + Track.TargetActorName);
				DrawList->AddText(ImVec2(LegendRect.Min.x + 12.0f, LegendRect.Min.y + 6.0f), 0xFFAAAAAA, SubInfo.c_str());
				DrawList->AddText(ImVec2(LegendRect.Min.x + 12.0f, LegendRect.Min.y + 24.0f), 0xFF888888, "L-Drag: Move / R-Click: Del");
				DrawList->PopClipRect();
			}
			DrawTrackContent(Index, DrawList, Rc, ClippingRect, true);
		}

		void CustomDrawCompact(
			int Index,
			ImDrawList* DrawList,
			const ImRect& Rc,
			const ImRect& ClippingRect) override
		{
			DrawTrackContent(Index, DrawList, Rc, ClippingRect, false);
		}
	};
}

bool FSequencerPanel::Init()
{
	CreateNewSequence();
	return true;
}

void FSequencerPanel::CreateNewSequence()
{
	SequenceData = FLevelSequenceData{};
	SequenceData.FrameRate = 30;
	SequenceData.StartFrame = 0;
	SequenceData.EndFrame = 150;
	SequenceData.bLoop = false;

	FSequenceTrack CutTrack;
	CutTrack.TrackName = "Camera Cuts";
	CutTrack.TrackType = ESequenceTrackType::CameraCut;
	CutTrack.bExpanded = true;
	SequenceData.Tracks.Add(CutTrack);

	CurrentFrame = 0;
	PlaybackFrameFloat = 0.0f;
	PreviousEvalFrame = -1.0f;
	FirstFrame = 0;
	SelectedTrackIndex = 0;
	SelectedKeyIndex = -1;
	bIsPlaying = false;
	bHasEvaluatedCameraCut = false;
	StatusMessage = "Created new sequence.";
}

bool FSequencerPanel::OpenSequenceAsset(const FString& Path)
{
	FLevelSequenceData Loaded;
	if (!FLevelSequenceAsset::LoadFromFile(Path, Loaded))
	{
		StatusMessage = "Failed to load: " + Path;
		return false;
	}

	SequenceData = std::move(Loaded);
	CurrentAssetPath = Path;
	strncpy_s(AssetPathBuffer, sizeof(AssetPathBuffer), CurrentAssetPath.c_str(), _TRUNCATE);
	CurrentFrame = SequenceData.StartFrame;
	PlaybackFrameFloat = static_cast<float>(CurrentFrame);
	PreviousEvalFrame = -1.0f;
	FirstFrame = SequenceData.StartFrame;
	SelectedTrackIndex = SequenceData.Tracks.IsEmpty() ? -1 : 0;
	SelectedKeyIndex = -1;
	bIsPlaying = false;
	SetOpen(true);
	EvaluateAtCurrentFrame(false);
	StatusMessage = "Loaded: " + Path;
	return true;
}

bool FSequencerPanel::SaveSequenceAsset(const FString& Path)
{
	for (FSequenceTrack& Track : SequenceData.Tracks)
	{
		FLevelSequenceAsset::SortTrackKeys(Track);
	}
	if (!FLevelSequenceAsset::SaveToFile(SequenceData, Path))
	{
		StatusMessage = "Failed to save: " + Path;
		return false;
	}
	CurrentAssetPath = Path;
	strncpy_s(AssetPathBuffer, sizeof(AssetPathBuffer), CurrentAssetPath.c_str(), _TRUNCATE);
	StatusMessage = "Saved: " + Path;
	return true;
}

void FSequencerPanel::Tick(float DeltaTime)
{
	if (!World)
	{
		return;
	}

	// 에디터 단축키로 현재 프레임에 키프레임 찍기
	if (IsOpen() && !ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::K))
	{
		if (SelectedTrackIndex >= 0 && SelectedTrackIndex < SequenceData.Tracks.Num())
		{
			AddKeyframeAtCurrentFrame(SelectedTrackIndex);
		}
	}

	if (bIsPlaying)
	{
		const float Fps = static_cast<float>(std::max(SequenceData.FrameRate, 1));
		PlaybackFrameFloat += DeltaTime * Fps;

		if (PlaybackFrameFloat >= static_cast<float>(SequenceData.EndFrame))
		{
			if (SequenceData.bLoop)
			{
				CurrentFrame = SequenceData.EndFrame;
				EvaluateAtCurrentFrame(true);
				PlaybackFrameFloat = static_cast<float>(SequenceData.StartFrame);
				PreviousEvalFrame = -1.0f;
			}
			else
			{
				PlaybackFrameFloat = static_cast<float>(SequenceData.EndFrame);
				CurrentFrame = SequenceData.EndFrame;
				EvaluateAtCurrentFrame(true);
				bIsPlaying = false;
				return;
			}
		}

		CurrentFrame = static_cast<int32>(std::round(PlaybackFrameFloat));
		EvaluateAtCurrentFrame(true);
	}
	else
	{
		UpdateAutoKey();
	}
}

void FSequencerPanel::SetCurrentFrameAndEvaluate(int32 InFrame, bool bTriggerAudio)
{
	CurrentFrame = FMath::Clamp(InFrame, SequenceData.StartFrame, SequenceData.EndFrame);
	PlaybackFrameFloat = static_cast<float>(CurrentFrame);
	EvaluateAtCurrentFrame(bTriggerAudio);
}

void FSequencerPanel::EvaluateAtCurrentFrame(bool bTriggerAudio)
{
	if (!World)
	{
		return;
	}

	const float EvalFrame = bIsPlaying ? PlaybackFrameFloat : static_cast<float>(CurrentFrame);
	FLevelSequenceAsset::EvaluateWorldAtFrame(
		SequenceData,
		World,
		EvalFrame,
		PreviousEvalFrame,
		bTriggerAudio,
		&EvaluatedCameraPOV,
		&bHasEvaluatedCameraCut);
	PreviousEvalFrame = EvalFrame;

	if (bCameraLock && bHasEvaluatedCameraCut)
	{
		SyncCameraToActiveViewport();
	}
}

void FSequencerPanel::SyncCameraToActiveViewport()
{
	if (!ViewportsPanel || !bHasEvaluatedCameraCut)
	{
		return;
	}
	const int32 ActiveIdx = ViewportsPanel->GetActiveViewIndex();
	if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(ActiveIdx))
	{
		if (Client->IsPerspective())
		{
			Client->SetViewLocation(EvaluatedCameraPOV.Location);
			Client->SetViewRotation(EvaluatedCameraPOV.Rotation);
			Client->SetViewFOV(EvaluatedCameraPOV.FieldOfView);
		}
	}
}

void FSequencerPanel::UpdateAutoKey()
{
	if (!bAutoKey || !Gizmo || !World)
	{
		return;
	}

	USceneComponent* TargetComp = Gizmo->GetTarget();
	AActor* TargetActor = TargetComp ? TargetComp->GetOwner() : nullptr;
	if (!TargetActor)
	{
		LastGizmoActor = nullptr;
		bWasGizmoDragging = false;
		return;
	}

	const FTransform CurrentTransform = TargetActor->GetActorTransform();
	if (LastGizmoActor != TargetActor)
	{
		LastGizmoActor = TargetActor;
		LastGizmoTransform = CurrentTransform;
		return;
	}

	const FVector LocDiff = CurrentTransform.Location - LastGizmoTransform.Location;
	const bool bMoved = (LocDiff.SizeSquared() > 0.0001f) ||
		(std::abs(CurrentTransform.Rotation.Pitch - LastGizmoTransform.Rotation.Pitch) > 0.01f) ||
		(std::abs(CurrentTransform.Rotation.Yaw - LastGizmoTransform.Rotation.Yaw) > 0.01f) ||
		(std::abs(CurrentTransform.Rotation.Roll - LastGizmoTransform.Rotation.Roll) > 0.01f) ||
		((CurrentTransform.Scale - LastGizmoTransform.Scale).SizeSquared() > 0.0001f);

	if (bMoved)
	{
		LastGizmoTransform = CurrentTransform;
		for (int32 i = 0; i < SequenceData.Tracks.Num(); ++i)
		{
			FSequenceTrack& Track = SequenceData.Tracks[i];
			if (Track.TrackType == ESequenceTrackType::ActorTransform && Track.TargetActorName == TargetActor->GetName())
			{
				AddKeyframeAtCurrentFrame(i);
				break;
			}
		}
	}
}

void FSequencerPanel::AddTrackByType(ESequenceTrackType TrackType)
{
	AActor* SelectedActor = OutlinerPanel ? OutlinerPanel->GetSelectedActor() : nullptr;
	FSequenceTrack NewTrack;
	NewTrack.TrackType = TrackType;
	NewTrack.bExpanded = true;

	if (TrackType == ESequenceTrackType::CameraCut)
	{
		NewTrack.TrackName = "Camera Cuts";
		FCameraCutClip DefaultCut;
		DefaultCut.StartFrame = SequenceData.StartFrame;
		DefaultCut.EndFrame = SequenceData.EndFrame;
		if (SelectedActor && SelectedActor->IsA<ACameraActor>())
		{
			DefaultCut.CameraActorName = SelectedActor->GetName();
		}
		NewTrack.CameraCuts.Add(DefaultCut);
	}
	else if (TrackType == ESequenceTrackType::ActorTransform)
	{
		if (SelectedActor)
		{
			NewTrack.TargetActorName = SelectedActor->GetName();
			NewTrack.TrackName = SelectedActor->GetName() + " Transform";
		}
		else
		{
			NewTrack.TrackName = "Actor Transform";
		}
	}
	else if (TrackType == ESequenceTrackType::CameraFOV)
	{
		if (SelectedActor)
		{
			NewTrack.TargetActorName = SelectedActor->GetName();
			NewTrack.TrackName = SelectedActor->GetName() + " FOV";
		}
		else
		{
			NewTrack.TrackName = "Camera FOV";
		}
	}
	else if (TrackType == ESequenceTrackType::Audio)
	{
		NewTrack.TrackName = "Audio Track";
		FAudioClip DefaultAudio;
		DefaultAudio.StartFrame = CurrentFrame;
		DefaultAudio.EndFrame = std::min(CurrentFrame + 30, SequenceData.EndFrame);
		NewTrack.AudioClips.Add(DefaultAudio);
	}

	SequenceData.Tracks.Add(std::move(NewTrack));
	SelectedTrackIndex = SequenceData.Tracks.Num() - 1;
	SelectedKeyIndex = -1;

	if (TrackType == ESequenceTrackType::ActorTransform || TrackType == ESequenceTrackType::CameraFOV)
	{
		AddKeyframeAtCurrentFrame(SelectedTrackIndex);
	}
}

void FSequencerPanel::DeleteTrackAt(int32 TrackIndex)
{
	if (TrackIndex < 0 || TrackIndex >= SequenceData.Tracks.Num())
	{
		return;
	}
	SequenceData.Tracks.RemoveAt(TrackIndex);
	if (SelectedTrackIndex >= SequenceData.Tracks.Num())
	{
		SelectedTrackIndex = SequenceData.Tracks.Num() - 1;
	}
	SelectedKeyIndex = -1;
}

void FSequencerPanel::DuplicateTrackAt(int32 TrackIndex)
{
	if (TrackIndex < 0 || TrackIndex >= SequenceData.Tracks.Num())
	{
		return;
	}
	FSequenceTrack Copy = SequenceData.Tracks[TrackIndex];
	Copy.TrackName += " Copy";
	SequenceData.Tracks.Add(std::move(Copy));
	SelectedTrackIndex = SequenceData.Tracks.Num() - 1;
	SelectedKeyIndex = -1;
}

void FSequencerPanel::ToggleTrackExpandedAt(int32 TrackIndex)
{
	if (TrackIndex < 0 || TrackIndex >= SequenceData.Tracks.Num())
	{
		return;
	}
	SequenceData.Tracks[TrackIndex].bExpanded = !SequenceData.Tracks[TrackIndex].bExpanded;
}

void FSequencerPanel::AddKeyframeAtCurrentFrame(int32 TrackIndex)
{
	if (TrackIndex < 0 || TrackIndex >= SequenceData.Tracks.Num())
	{
		return;
	}

	FSequenceTrack& Track = SequenceData.Tracks[TrackIndex];
	AActor* BoundActor = FLevelSequenceAsset::FindActorByNameInWorld(World, Track.TargetActorName);

	if (Track.TrackType == ESequenceTrackType::ActorTransform)
	{
		FTransform SourceTransform = BoundActor ? BoundActor->GetActorTransform() : FTransform::Identity;
		for (int32 i = 0; i < Track.TransformKeys.Num(); ++i)
		{
			if (Track.TransformKeys[i].Frame == CurrentFrame)
			{
				Track.TransformKeys[i].Location = SourceTransform.Location;
				Track.TransformKeys[i].Rotation = SourceTransform.Rotation;
				Track.TransformKeys[i].Scale = SourceTransform.Scale;
				SelectedKeyIndex = i;
				return;
			}
		}

		FTransformKeyframe NewKey;
		NewKey.Frame = CurrentFrame;
		NewKey.Location = SourceTransform.Location;
		NewKey.Rotation = SourceTransform.Rotation;
		NewKey.Scale = SourceTransform.Scale;
		NewKey.InterpMode = ESequenceInterpMode::Cubic;
		Track.TransformKeys.Add(NewKey);
		FLevelSequenceAsset::SortTrackKeys(Track);
		for (int32 i = 0; i < Track.TransformKeys.Num(); ++i)
		{
			if (Track.TransformKeys[i].Frame == CurrentFrame)
			{
				SelectedKeyIndex = i;
				break;
			}
		}
	}
	else if (Track.TrackType == ESequenceTrackType::CameraFOV)
	{
		float CurrentFOV = 60.0f;
		if (BoundActor)
		{
			if (UCameraComponent* CamComp = BoundActor->GetComponentByClass<UCameraComponent>())
			{
				CurrentFOV = CamComp->GetFieldOfView();
			}
		}

		for (int32 i = 0; i < Track.FOVKeys.Num(); ++i)
		{
			if (Track.FOVKeys[i].Frame == CurrentFrame)
			{
				Track.FOVKeys[i].FieldOfView = CurrentFOV;
				SelectedKeyIndex = i;
				return;
			}
		}

		FFOVKeyframe NewKey;
		NewKey.Frame = CurrentFrame;
		NewKey.FieldOfView = CurrentFOV;
		NewKey.InterpMode = ESequenceInterpMode::Cubic;
		Track.FOVKeys.Add(NewKey);
		FLevelSequenceAsset::SortTrackKeys(Track);
		for (int32 i = 0; i < Track.FOVKeys.Num(); ++i)
		{
			if (Track.FOVKeys[i].Frame == CurrentFrame)
			{
				SelectedKeyIndex = i;
				break;
			}
		}
	}
	else if (Track.TrackType == ESequenceTrackType::CameraCut)
	{
		FCameraCutClip NewCut;
		NewCut.StartFrame = CurrentFrame;
		NewCut.EndFrame = std::min(CurrentFrame + 60, SequenceData.EndFrame);
		if (AActor* Selected = OutlinerPanel ? OutlinerPanel->GetSelectedActor() : nullptr)
		{
			NewCut.CameraActorName = Selected->GetName();
		}
		Track.CameraCuts.Add(NewCut);
		FLevelSequenceAsset::SortTrackKeys(Track);
		SelectedKeyIndex = Track.CameraCuts.Num() - 1;
	}
	else if (Track.TrackType == ESequenceTrackType::Audio)
	{
		FAudioClip NewClip;
		NewClip.StartFrame = CurrentFrame;
		NewClip.EndFrame = std::min(CurrentFrame + 30, SequenceData.EndFrame);
		Track.AudioClips.Add(NewClip);
		FLevelSequenceAsset::SortTrackKeys(Track);
		SelectedKeyIndex = Track.AudioClips.Num() - 1;
	}
}

void FSequencerPanel::SpawnCutsceneCamera()
{
	if (!World)
	{
		return;
	}

	FTransform SpawnTransform = FTransform::Identity;
	float InitialFOV = 60.0f;
	if (ViewportsPanel)
	{
		const int32 ActiveIdx = ViewportsPanel->GetActiveViewIndex();
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(ActiveIdx))
		{
			SpawnTransform.Location = Client->GetViewLocation();
			SpawnTransform.Rotation = Client->GetViewRotation();
			InitialFOV = Client->GetViewFOV();
		}
	}

	ACameraActor* CamActor = World->SpawnActor<ACameraActor>(NAME_None, &SpawnTransform);
	if (!CamActor)
	{
		return;
	}

	if (UCameraComponent* CamComp = CamActor->GetCameraComponent())
	{
		CamComp->SetFieldOfView(InitialFOV);
		CamComp->SetExternalInputManaged(true);
	}

	if (OutlinerPanel)
	{
		OutlinerPanel->SelectActor(CamActor);
	}

	// 카메라 생성 시 자동으로 트랜스폼 트랙과 시야각 트랙 및 카메라 컷 등록
	AddTrackByType(ESequenceTrackType::ActorTransform);
	AddTrackByType(ESequenceTrackType::CameraFOV);

	bool bAddedToCutTrack = false;
	for (FSequenceTrack& Track : SequenceData.Tracks)
	{
		if (Track.TrackType == ESequenceTrackType::CameraCut)
		{
			if (Track.CameraCuts.IsEmpty())
			{
				FCameraCutClip Cut;
				Cut.StartFrame = CurrentFrame;
				Cut.EndFrame = SequenceData.EndFrame;
				Cut.CameraActorName = CamActor->GetName();
				Track.CameraCuts.Add(Cut);
			}
			else if (Track.CameraCuts.Last().CameraActorName.empty())
			{
				Track.CameraCuts.Last().CameraActorName = CamActor->GetName();
			}
			bAddedToCutTrack = true;
			break;
		}
	}

	if (!bAddedToCutTrack)
	{
		AddTrackByType(ESequenceTrackType::CameraCut);
	}

	EvaluateAtCurrentFrame(false);
	StatusMessage = "Spawned Cutscene Camera: " + CamActor->GetName();
}

void FSequencerPanel::SpawnSequenceActorInLevel()
{
	if (!World)
	{
		return;
	}

	ALevelSequenceActor* SeqActor = World->SpawnActor<ALevelSequenceActor>();
	if (!SeqActor)
	{
		return;
	}

	SeqActor->SetSequenceAssetPath(AssetPathBuffer);
	if (OutlinerPanel)
	{
		OutlinerPanel->SelectActor(SeqActor);
	}
	StatusMessage = "Spawned LevelSequenceActor in Level: " + SeqActor->GetName();
}

void FSequencerPanel::DrawToolbar()
{
	if (ImGui::Button("New"))
	{
		CreateNewSequence();
	}
	ImGui::SameLine();
	if (ImGui::Button("Open..."))
	{
		const FString Chosen = BrowseFileDialog(
			L"Sequence Asset (*.seq.json)\0*.seq.json\0JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0",
			L"json");
		if (!Chosen.empty())
		{
			OpenSequenceAsset(Chosen);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		SaveSequenceAsset(AssetPathBuffer);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(240.0f);
	ImGui::InputText("##SeqPath", AssetPathBuffer, sizeof(AssetPathBuffer));

	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();

	// 트랜스포트 컨트롤 버튼
	if (ImGui::Button("|<"))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(SequenceData.StartFrame, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("<"))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(CurrentFrame - 1, false);
	}
	ImGui::SameLine();
	if (ImGui::Button(bIsPlaying ? "Pause" : "Play"))
	{
		bIsPlaying = !bIsPlaying;
		if (bIsPlaying && CurrentFrame >= SequenceData.EndFrame)
		{
			CurrentFrame = SequenceData.StartFrame;
			PlaybackFrameFloat = static_cast<float>(CurrentFrame);
			PreviousEvalFrame = -1.0f;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop"))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(SequenceData.StartFrame, false);
	}
	ImGui::SameLine();
	if (ImGui::Button(">"))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(CurrentFrame + 1, false);
	}
	ImGui::SameLine();
	if (ImGui::Button(">|"))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(SequenceData.EndFrame, false);
	}

	ImGui::SameLine();
	ImGui::Checkbox("Loop", &SequenceData.bLoop);
	ImGui::SameLine();
	ImGui::Checkbox("Auto-Key", &bAutoKey);
	ImGui::SameLine();
	if (ImGui::Checkbox("Camera Lock", &bCameraLock) && bCameraLock)
	{
		EvaluateAtCurrentFrame(false);
	}
	ImGui::SameLine();
	ImGui::Checkbox("3D Path", &bShowTrajectory);

	// 두 번째 줄: 프레임 설정 및 빠른 생성 도구
	ImGui::SetNextItemWidth(70.0f);
	if (ImGui::DragInt("Frame", &CurrentFrame, 1.0f, SequenceData.StartFrame, SequenceData.EndFrame))
	{
		bIsPlaying = false;
		SetCurrentFrameAndEvaluate(CurrentFrame, false);
	}
	ImGui::SameLine();
	const float CurrentTimeSec = static_cast<float>(CurrentFrame) / static_cast<float>(std::max(SequenceData.FrameRate, 1));
	ImGui::TextDisabled("(%.2fs)", CurrentTimeSec);

	ImGui::SameLine();
	ImGui::SetNextItemWidth(55.0f);
	ImGui::DragInt("Start", &SequenceData.StartFrame, 1.0f, 0, SequenceData.EndFrame - 1);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(65.0f);
	ImGui::DragInt("End", &SequenceData.EndFrame, 1.0f, SequenceData.StartFrame + 1, 36000);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(50.0f);
	ImGui::DragInt("FPS", &SequenceData.FrameRate, 1.0f, 1, 240);

	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();

	if (ImGui::Button("+ Spawn Cutscene Cam"))
	{
		SpawnCutsceneCamera();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Place Sequence Actor"))
	{
		SpawnSequenceActorInLevel();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Track"))
	{
		ImGui::OpenPopup("AddTrackToolbarPopup");
	}
	if (ImGui::BeginPopup("AddTrackToolbarPopup"))
	{
		if (ImGui::MenuItem("Camera Cut Track")) AddTrackByType(ESequenceTrackType::CameraCut);
		if (ImGui::MenuItem("Actor Transform Track (Selected Actor)")) AddTrackByType(ESequenceTrackType::ActorTransform);
		if (ImGui::MenuItem("Camera FOV Track (Selected Camera)")) AddTrackByType(ESequenceTrackType::CameraFOV);
		if (ImGui::MenuItem("Audio Track (SFX / BGM)")) AddTrackByType(ESequenceTrackType::Audio);
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Key (K)") && SelectedTrackIndex >= 0 && SelectedTrackIndex < SequenceData.Tracks.Num())
	{
		AddKeyframeAtCurrentFrame(SelectedTrackIndex);
	}

	if (!StatusMessage.empty())
	{
		ImGui::SameLine();
		ImGui::TextDisabled("| %s", StatusMessage.c_str());
	}
}

void FSequencerPanel::DrawTrackInspector()
{
	ImGui::SeparatorText("Track & Keyframe Inspector");

	if (SelectedTrackIndex < 0 || SelectedTrackIndex >= SequenceData.Tracks.Num())
	{
		ImGui::TextDisabled("Select a track or keyframe on the timeline to edit its properties.");
		return;
	}

	FSequenceTrack& Track = SequenceData.Tracks[SelectedTrackIndex];
	ImGui::PushID(SelectedTrackIndex);

	char NameBuf[256] = {};
	strncpy_s(NameBuf, sizeof(NameBuf), Track.TrackName.c_str(), _TRUNCATE);
	if (ImGui::InputText("Track Name", NameBuf, sizeof(NameBuf)))
	{
		Track.TrackName = NameBuf;
	}

	ImGui::Checkbox("Mute Track", &Track.bMuted);
	ImGui::SameLine();
	ImGui::Checkbox("Expanded", &Track.bExpanded);

	// 액터 바인딩이 필요한 트랙은 드롭다운과 선택 액터 할당 버튼 제공
	if (Track.TrackType == ESequenceTrackType::ActorTransform || Track.TrackType == ESequenceTrackType::CameraFOV)
	{
		const char* CurrentTarget = Track.TargetActorName.empty() ? "(None)" : Track.TargetActorName.c_str();
		if (ImGui::BeginCombo("Target Actor", CurrentTarget))
		{
			if (World && World->GetPersistentLevel())
			{
				for (AActor* Actor : World->GetPersistentLevel()->GetActors())
				{
					if (!Actor || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) continue;
					if (Track.TrackType == ESequenceTrackType::CameraFOV && !Actor->GetComponentByClass<UCameraComponent>())
					{
						continue;
					}
					const bool bSelected = (Track.TargetActorName == Actor->GetName());
					if (ImGui::Selectable(Actor->GetName().c_str(), bSelected))
					{
						Track.TargetActorName = Actor->GetName();
						if (Track.TrackName.empty() || Track.TrackName == "Actor Transform" || Track.TrackName == "Camera FOV")
						{
							Track.TrackName = Actor->GetName() + (Track.TrackType == ESequenceTrackType::CameraFOV ? " FOV" : " Transform");
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::Button("Use Selected Actor from Outliner"))
		{
			if (AActor* Selected = OutlinerPanel ? OutlinerPanel->GetSelectedActor() : nullptr)
			{
				Track.TargetActorName = Selected->GetName();
				Track.TrackName = Selected->GetName() + (Track.TrackType == ESequenceTrackType::CameraFOV ? " FOV" : " Transform");
			}
		}
	}

	ImGui::Separator();

	const char* InterpNames[] = { "Linear", "Cubic (Smooth)", "Constant (Step)" };

	// 카메라 컷 트랙 편집 UI
	if (Track.TrackType == ESequenceTrackType::CameraCut)
	{
		if (ImGui::Button("+ Add Camera Cut at Current Frame"))
		{
			AddKeyframeAtCurrentFrame(SelectedTrackIndex);
		}

		int32 RemoveIdx = -1;
		for (int32 i = 0; i < Track.CameraCuts.Num(); ++i)
		{
			FCameraCutClip& Cut = Track.CameraCuts[i];
			ImGui::PushID(i);
			const bool bOpen = ImGui::TreeNodeEx(
				"CutItem",
				ImGuiTreeNodeFlags_DefaultOpen,
				"Cut #%d [%d ~ %d] : %s",
				i,
				Cut.StartFrame,
				Cut.EndFrame,
				Cut.CameraActorName.empty() ? "(None)" : Cut.CameraActorName.c_str());

			if (bOpen)
			{
				const char* CurCam = Cut.CameraActorName.empty() ? "(Select Camera Actor)" : Cut.CameraActorName.c_str();
				if (ImGui::BeginCombo("Camera Actor", CurCam))
				{
					if (World && World->GetPersistentLevel())
					{
						for (AActor* Actor : World->GetPersistentLevel()->GetActors())
						{
							if (!Actor || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) continue;
							if (!Actor->GetComponentByClass<UCameraComponent>()) continue;
							const bool bIsChosen = (Cut.CameraActorName == Actor->GetName());
							if (ImGui::Selectable(Actor->GetName().c_str(), bIsChosen))
							{
								Cut.CameraActorName = Actor->GetName();
								EvaluateAtCurrentFrame(false);
							}
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::DragInt("Start Frame", &Cut.StartFrame, 1.0f, SequenceData.StartFrame, Cut.EndFrame))
				{
					EvaluateAtCurrentFrame(false);
				}
				if (ImGui::DragInt("End Frame", &Cut.EndFrame, 1.0f, Cut.StartFrame, SequenceData.EndFrame))
				{
					EvaluateAtCurrentFrame(false);
				}
				if (ImGui::DragFloat("Blend Time (s)", &Cut.BlendTime, 0.05f, 0.0f, 10.0f, "%.2f"))
				{
					EvaluateAtCurrentFrame(false);
				}
				if (ImGui::Button("Delete Cut"))
				{
					RemoveIdx = i;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (RemoveIdx >= 0)
		{
			Track.CameraCuts.RemoveAt(RemoveIdx);
			EvaluateAtCurrentFrame(false);
		}
	}
	// 액터 트랜스폼 키프레임 편집 UI
	else if (Track.TrackType == ESequenceTrackType::ActorTransform)
	{
		if (ImGui::Button("+ Key Actor Transform at Current Frame"))
		{
			AddKeyframeAtCurrentFrame(SelectedTrackIndex);
		}

		if (SelectedKeyIndex >= 0 && SelectedKeyIndex < Track.TransformKeys.Num())
		{
			FTransformKeyframe& Key = Track.TransformKeys[SelectedKeyIndex];
			ImGui::SeparatorText("Selected Transform Keyframe");
			if (ImGui::DragInt("Key Frame", &Key.Frame, 1.0f, SequenceData.StartFrame, SequenceData.EndFrame))
			{
				FLevelSequenceAsset::SortTrackKeys(Track);
				EvaluateAtCurrentFrame(false);
			}
			if (ImGui::DragFloat3("Location", Key.Location.V, 0.1f))
			{
				EvaluateAtCurrentFrame(false);
			}
			float Euler[3] = { Key.Rotation.Roll, Key.Rotation.Pitch, Key.Rotation.Yaw };
			if (ImGui::DragFloat3("Rotation (R/P/Y)", Euler, 0.5f))
			{
				Key.Rotation.Roll = Euler[0];
				Key.Rotation.Pitch = Euler[1];
				Key.Rotation.Yaw = Euler[2];
				EvaluateAtCurrentFrame(false);
			}
			if (ImGui::DragFloat3("Scale", Key.Scale.V, 0.05f))
			{
				EvaluateAtCurrentFrame(false);
			}
			int InterpIdx = static_cast<int>(Key.InterpMode);
			if (ImGui::Combo("Interpolation", &InterpIdx, InterpNames, IM_ARRAYSIZE(InterpNames)))
			{
				Key.InterpMode = static_cast<ESequenceInterpMode>(InterpIdx);
				EvaluateAtCurrentFrame(false);
			}
			if (ImGui::Button("Capture From Viewport Camera"))
			{
				if (ViewportsPanel)
				{
					if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()))
					{
						Key.Location = Client->GetViewLocation();
						Key.Rotation = Client->GetViewRotation();
						EvaluateAtCurrentFrame(false);
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete Key"))
			{
				Track.TransformKeys.RemoveAt(SelectedKeyIndex);
				SelectedKeyIndex = -1;
				EvaluateAtCurrentFrame(false);
			}
		}
		else
		{
			ImGui::TextDisabled("Click a diamond keyframe on the track to edit values.");
		}
	}
	// 카메라 시야각 키프레임 편집 UI
	else if (Track.TrackType == ESequenceTrackType::CameraFOV)
	{
		if (ImGui::Button("+ Key Camera FOV at Current Frame"))
		{
			AddKeyframeAtCurrentFrame(SelectedTrackIndex);
		}

		if (SelectedKeyIndex >= 0 && SelectedKeyIndex < Track.FOVKeys.Num())
		{
			FFOVKeyframe& Key = Track.FOVKeys[SelectedKeyIndex];
			ImGui::SeparatorText("Selected FOV Keyframe");
			if (ImGui::DragInt("Key Frame", &Key.Frame, 1.0f, SequenceData.StartFrame, SequenceData.EndFrame))
			{
				FLevelSequenceAsset::SortTrackKeys(Track);
				EvaluateAtCurrentFrame(false);
			}
			if (ImGui::SliderFloat("Field Of View", &Key.FieldOfView, 10.0f, 150.0f, "%.1f deg"))
			{
				EvaluateAtCurrentFrame(false);
			}
			int InterpIdx = static_cast<int>(Key.InterpMode);
			if (ImGui::Combo("Interpolation", &InterpIdx, InterpNames, IM_ARRAYSIZE(InterpNames)))
			{
				Key.InterpMode = static_cast<ESequenceInterpMode>(InterpIdx);
				EvaluateAtCurrentFrame(false);
			}
			if (ImGui::Button("Delete Key"))
			{
				Track.FOVKeys.RemoveAt(SelectedKeyIndex);
				SelectedKeyIndex = -1;
				EvaluateAtCurrentFrame(false);
			}
		}
		else
		{
			ImGui::TextDisabled("Click a diamond keyframe on the FOV track to edit.");
		}
	}
	// 오디오 클립 편집 UI
	else if (Track.TrackType == ESequenceTrackType::Audio)
	{
		if (ImGui::Button("+ Add Audio Clip at Current Frame"))
		{
			AddKeyframeAtCurrentFrame(SelectedTrackIndex);
		}

		int32 RemoveIdx = -1;
		for (int32 i = 0; i < Track.AudioClips.Num(); ++i)
		{
			FAudioClip& Clip = Track.AudioClips[i];
			ImGui::PushID(i);
			const bool bOpen = ImGui::TreeNodeEx(
				"AudioClipItem",
				ImGuiTreeNodeFlags_DefaultOpen,
				"Audio #%d [%d ~ %d] : %s",
				i,
				Clip.StartFrame,
				Clip.EndFrame,
				Clip.SoundPath.empty() ? "(None)" : Clip.SoundPath.c_str());

			if (bOpen)
			{
				char SoundBuf[512] = {};
				strncpy_s(SoundBuf, sizeof(SoundBuf), Clip.SoundPath.c_str(), _TRUNCATE);
				if (ImGui::InputText("Sound Path", SoundBuf, sizeof(SoundBuf)))
				{
					Clip.SoundPath = SoundBuf;
				}
				if (ImGui::Button("Browse .wav..."))
				{
					const FString ChosenWav = BrowseFileDialog(
						L"WAV Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0",
						L"wav");
					if (!ChosenWav.empty())
					{
						Clip.SoundPath = ChosenWav;
					}
				}
				ImGui::DragInt("Start Frame", &Clip.StartFrame, 1.0f, SequenceData.StartFrame, Clip.EndFrame);
				ImGui::DragInt("End Frame", &Clip.EndFrame, 1.0f, Clip.StartFrame, SequenceData.EndFrame);
				ImGui::SliderFloat("Volume", &Clip.Volume, 0.0f, 1.0f, "%.2f");
				ImGui::Checkbox("Play as BGM", &Clip.bIsBGM);
				if (Clip.bIsBGM)
				{
					ImGui::SameLine();
					ImGui::Checkbox("Loop BGM", &Clip.bLoopBGM);
				}
				if (ImGui::Button("Delete Audio Clip"))
				{
					RemoveIdx = i;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (RemoveIdx >= 0)
		{
			Track.AudioClips.RemoveAt(RemoveIdx);
		}
	}

	ImGui::PopID();
}

void FSequencerPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(1100, 360), ImGuiCond_FirstUseEver);
	bool bPanelOpen = IsOpen();
	if (!ImGui::Begin("Sequencer", &bPanelOpen))
	{
		SetOpen(bPanelOpen);
		ImGui::End();
		return;
	}
	SetOpen(bPanelOpen);

	DrawToolbar();
	ImGui::Separator();

	const float TotalWidth = ImGui::GetContentRegionAvail().x;
	const float InspectorWidth = std::min(340.0f, TotalWidth * 0.32f);
	const float TimelineWidth = std::max(200.0f, TotalWidth - InspectorWidth - 8.0f);

	ImGui::BeginChild("SequencerTimelineRegion", ImVec2(TimelineWidth, 0.0f), ImGuiChildFlags_Borders);
	{
		FSequencerBridge Bridge(this);
		const int32 PrevFrame = CurrentFrame;
		const bool bChanged = ImSequencer::Sequencer(
			&Bridge,
			&CurrentFrame,
			&bSequencerExpanded,
			&SelectedTrackIndex,
			&FirstFrame,
			ImSequencer::SEQUENCER_EDIT_ALL | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DELETE);

		if (bChanged || CurrentFrame != PrevFrame)
		{
			PlaybackFrameFloat = static_cast<float>(CurrentFrame);
			EvaluateAtCurrentFrame(false);
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("SequencerInspectorRegion", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	{
		DrawTrackInspector();
	}
	ImGui::EndChild();

	ImGui::End();
}

void FSequencerPanel::DrawTrajectoryLines(FLineBatcher* LineBatcher) const
{
	if (!bShowTrajectory || !LineBatcher)
	{
		return;
	}

	for (int32 TrackIdx = 0; TrackIdx < SequenceData.Tracks.Num(); ++TrackIdx)
	{
		const FSequenceTrack& Track = SequenceData.Tracks[TrackIdx];
		if (Track.bMuted || Track.TrackType != ESequenceTrackType::ActorTransform || Track.TransformKeys.Num() < 2)
		{
			continue;
		}

		const bool bIsSelectedTrack = (TrackIdx == SelectedTrackIndex);
		const FVector4 PathColor = bIsSelectedTrack
			? FVector4(1.0f, 0.85f, 0.15f, 1.0f)
			: FVector4(0.25f, 0.85f, 0.45f, 1.0f);
		const FVector4 KeyBoxColor(1.0f, 0.35f, 0.1f, 1.0f);

		const int32 FirstKeyFrame = Track.TransformKeys[0].Frame;
		const int32 LastKeyFrame = Track.TransformKeys.Last().Frame;
		FVector PrevPos = Track.TransformKeys[0].Location;

		for (int32 f = FirstKeyFrame + 1; f <= LastKeyFrame; ++f)
		{
			const FTransform Sample = FLevelSequenceAsset::EvaluateTransformTrack(Track, static_cast<float>(f), FTransform::Identity);
			LineBatcher->AddLine(PrevPos, Sample.Location, PathColor);
			PrevPos = Sample.Location;
		}

		for (const FTransformKeyframe& Key : Track.TransformKeys)
		{
			const FBox MarkerBox(Key.Location - FVector(0.15f, 0.15f, 0.15f), Key.Location + FVector(0.15f, 0.15f, 0.15f));
			LineBatcher->AddBox(MarkerBox, KeyBoxColor);
		}
	}
}
