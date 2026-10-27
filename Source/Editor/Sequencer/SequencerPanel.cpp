#include "EnginePCH.h"
#include "Editor/Sequencer/SequencerPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Render/Texture2D.h"
#include "Serialization/JsonArchive.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{
constexpr float HeaderWidth = 230.0f;
constexpr float RulerHeight = 30.0f;
constexpr float RowHeight = 44.0f;

bool LiveActor(UWorld* World, AActor* Actor)
{
	return Actor && GUObjectArray.Find(Actor) != INDEX_NONE && IsValid(Actor) &&
		!Actor->IsActorBeingDestroyed() && Actor->GetWorld() == World;
}

const char* TrackLabel(ESequenceTrackType Type)
{
	return Type == ESequenceTrackType::Transform ? "Transform" : "Field of View";
}
}

bool FSequencerPanel::Init()
{
	SetOpen(false);
	return true;
}

void FSequencerPanel::SetWorld(UWorld* InWorld)
{
	if (World == InWorld) return;
	StopPreview();
	World = InWorld;
}

void FSequencerPanel::ReleaseAsset()
{
	StopPreview();
	Player.reset();
	Sequence.reset();
	History.clear();
	AssetPath.clear();
	bDirty = false;
}

void FSequencerPanel::RestoreViewportCamera()
{
	if (bSavedViewportCamera && ViewportsPanel)
		if (FEditorViewportClient* View = ViewportsPanel->GetViewportClient(SavedViewportIndex))
		{
			View->SetViewLocation(SavedViewLocation);
			View->SetViewRotation(SavedViewRotation);
			View->SetViewFOV(SavedViewFov);
		}
	bSavedViewportCamera = false;
	SavedViewportIndex = -1;
}

void FSequencerPanel::StopPreview()
{
	if (PreviewSelectedActor && ActorSelectionCallback && SelectedActorProvider &&
		SelectedActorProvider() == PreviewSelectedActor)
		ActorSelectionCallback(nullptr);
	PreviewSelectedActor = nullptr;
	if (Player) Player->Stop();
	RestoreViewportCamera();
}

void FSequencerPanel::Tick(float DeltaTime)
{
	if (!IsOpen() || (PreviewAllowedQuery && !PreviewAllowedQuery()))
	{
		StopPreview();
		return;
	}
	if (Player && Player->IsPlaying())
	{
		Player->Tick(DeltaTime);
		CurrentFrame = Player->GetCurrentFrame();
		if (!Player->IsActive()) RestoreViewportCamera();
	}
	FollowSelectedCamera();
}

void FSequencerPanel::NewAsset()
{
	StopPreview();
	Sequence.reset(NewObject<ULevelSequence>());
	Sequence->EndFrame = 300;
	CurrentFrame = 0;
	SelectedBinding = SelectedTrack = SelectedSection = SelectedKey = -1;
	AssetPath = "Assets/Sequences/NewSequence.sequence.json";
	for (int32 Index = 1; std::filesystem::exists(AssetPath); ++Index)
		AssetPath = "Assets/Sequences/NewSequence" + std::to_string(Index) + ".sequence.json";
	std::snprintf(PathBuffer, sizeof(PathBuffer), "%s", AssetPath.c_str());
	ResetHistory();
	SavedSnapshot.clear();
	bDirty = true;
	Status = "New sequence";
}

void FSequencerPanel::RequestNewAsset()
{
	if (!bDirty) { NewAsset(); return; }
	PendingAction = EPendingAction::New;
}

bool FSequencerPanel::OpenAsset(const FString& Path)
{
	TUniquePtr<ULevelSequence> Loaded(NewObject<ULevelSequence>());
	if (!Loaded->LoadFromFile(Path)) { Status = "Could not open " + Path; return false; }
	StopPreview();
	Sequence = std::move(Loaded);
	AssetPath = Path;
	std::snprintf(PathBuffer, sizeof(PathBuffer), "%s", Path.c_str());
	CurrentFrame = Sequence->StartFrame;
	SelectedBinding = SelectedTrack = SelectedSection = SelectedKey = -1;
	ResetHistory();
	Status = "Opened " + Path;
	return true;
}

void FSequencerPanel::RequestOpenAsset(const FString& Path)
{
	if (!bDirty) { OpenAsset(Path); return; }
	PendingAction = EPendingAction::Open;
	PendingOpenPath = Path;
}

void FSequencerPanel::SaveAsset()
{
	if (!Sequence || PathBuffer[0] == '\0') { Status = "Choose a sequence path"; return; }
	const FString Path = PathBuffer;
	if (!Path.ends_with(".sequence.json")) { Status = "Use a .sequence.json filename"; return; }
	std::error_code Error;
	const auto Parent = std::filesystem::path(Path).parent_path();
	if (!Parent.empty()) std::filesystem::create_directories(Parent, Error);
	if (Error || !Sequence->SaveToFile(Path)) { Status = "Could not save " + Path; return; }
	AssetPath = Path;
	SavedSnapshot = Sequence->SerializeToString();
	bDirty = false;
	Status = "Saved " + Path;
}

void FSequencerPanel::ResetHistory()
{
	History.clear();
	SavedSnapshot = Sequence ? Sequence->SerializeToString() : FString{};
	if (!SavedSnapshot.empty()) History.push_back(SavedSnapshot);
	HistoryIndex = 0;
	bDirty = false;
}

void FSequencerPanel::CommitEdit()
{
	if (!Sequence) return;
	const FString Snapshot = Sequence->SerializeToString();
	if (Snapshot.empty()) { Status = "Sequence has invalid track or key data"; return; }
	if (History.empty()) { History.push_back(Snapshot); HistoryIndex = 0; }
	else if (History[HistoryIndex] != Snapshot)
	{
		History.erase(History.begin() + HistoryIndex + 1, History.end());
		History.push_back(Snapshot);
		if (History.size() > 64) History.erase(History.begin());
		HistoryIndex = History.size() - 1;
	}
	bDirty = Snapshot != SavedSnapshot;
}

void FSequencerPanel::Undo()
{
	if (!Sequence || HistoryIndex == 0) return;
	StopPreview();
	--HistoryIndex;
	Sequence->DeserializeFromString(History[HistoryIndex]);
	SelectedBinding = SelectedTrack = SelectedSection = SelectedKey = -1;
	bDirty = History[HistoryIndex] != SavedSnapshot;
}

void FSequencerPanel::Redo()
{
	if (!Sequence || HistoryIndex + 1 >= History.size()) return;
	StopPreview();
	++HistoryIndex;
	Sequence->DeserializeFromString(History[HistoryIndex]);
	SelectedBinding = SelectedTrack = SelectedSection = SelectedKey = -1;
	bDirty = History[HistoryIndex] != SavedSnapshot;
}

FString FSequencerPanel::NextBindingId() const
{
	int32 Index = 1;
	for (;; ++Index)
	{
		const FString Candidate = "Binding" + std::to_string(Index);
		if (std::none_of(Sequence->Bindings.begin(), Sequence->Bindings.end(),
			[&](const FSequenceBinding& B) { return B.Id == Candidate; })) return Candidate;
	}
}

void FSequencerPanel::AddBinding(ESequenceBindingType Type, UClass* SpawnClass, AActor* SceneActor)
{
	if (!Sequence || !World || (PreviewAllowedQuery && !PreviewAllowedQuery())) return;
	AActor* Selected = SceneActor ? SceneActor : SelectedActorProvider ? SelectedActorProvider() : nullptr;
	if (SceneActor && (!LiveActor(World, SceneActor) || SceneActor->HasAnyFlags(EObjectFlags::RF_Transient)))
	{
		Status = "Choose a non-preview scene Actor";
		return;
	}
	if (Type == ESequenceBindingType::Possessable && !LiveActor(World, Selected))
	{
		Status = "Choose a valid scene actor";
		return;
	}
	if (Type == ESequenceBindingType::Possessable && Selected->HasAnyFlags(EObjectFlags::RF_Transient))
	{
		Status = "Preview actors cannot be bound as scene actors";
		return;
	}
	const bool bResumePreview = Player && Player->IsActive();
	StopPreview();
	FSequenceBinding Binding;
	Binding.Id = NextBindingId();
	Binding.Type = Type;
	if (Type == ESequenceBindingType::Possessable)
		Binding.ActorId = Selected->GetPersistentActorId();
	else
	{
		Binding.ActorClass = SceneActor ? SceneActor->GetClass()->Name :
			(SpawnClass ? SpawnClass : ACameraActor::StaticClass())->Name;
		if (SceneActor)
		{
			Binding.SourceActorName = SceneActor->GetName();
			Binding.SpawnTransform = SceneActor->GetActorTransform();
			if (!FJsonArchive::SaveActorToString(SceneActor, Binding.ActorTemplateJson))
			{
				Status = "Could not capture the selected Actor";
				return;
			}
		}
		if (ViewportsPanel)
			if (FEditorViewportClient* View = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()))
			{
				Binding.SpawnTransform.Location = View->GetViewLocation() + View->GetViewForward() * 300.0f;
				if (SpawnClass == ACameraActor::StaticClass()) Binding.SpawnTransform.Rotation = View->GetViewRotation();
			}
	}
	Sequence->Bindings.push_back(std::move(Binding));
	SelectedBinding = static_cast<int32>(Sequence->Bindings.size()) - 1;
	SelectedTrack = SelectedSection = SelectedKey = -1;
	CommitEdit();
	if (Type == ESequenceBindingType::Possessable && ActorSelectionCallback) ActorSelectionCallback(Selected);
	if (Type != ESequenceBindingType::Possessable || bResumePreview)
	{
		if (EnsurePreview()) SelectPreviewActor();
		else Status = "Could not spawn preview actor; check sequence bindings";
	}
}

void FSequencerPanel::SelectPreviewActor()
{
	if (!Player || !Player->IsActive() || !Sequence || !ActorSelectionCallback ||
		SelectedBinding < 0 || SelectedBinding >= static_cast<int32>(Sequence->Bindings.size())) return;
	const FSequenceBinding& Binding = Sequence->Bindings[SelectedBinding];
	if (Binding.Type == ESequenceBindingType::Possessable) return;
	PreviewSelectedActor = Player->GetBoundActor(Binding.Id);
	if (PreviewSelectedActor) ActorSelectionCallback(PreviewSelectedActor);
}

void FSequencerPanel::FocusSelectedBinding()
{
	if (!Sequence || !ViewportsPanel || SelectedBinding < 0 ||
		SelectedBinding >= static_cast<int32>(Sequence->Bindings.size()) || !EnsurePreview()) return;
	AActor* Actor = Player->GetBoundActor(Sequence->Bindings[SelectedBinding].Id);
	if (!Actor) return;
	bFollowCamera = false;
	RestoreViewportCamera();
	if (FEditorViewportClient* View = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()))
		View->SetViewLocation(Actor->GetActorLocation() - View->GetViewForward() * 300.0f);
	if (ActorSelectionCallback)
	{
		ActorSelectionCallback(Actor);
		PreviewSelectedActor = Sequence->Bindings[SelectedBinding].Type == ESequenceBindingType::Possessable ? nullptr : Actor;
	}
}

void FSequencerPanel::AddTrack(ESequenceTrackType Type)
{
	if (!Sequence || SelectedBinding < 0 || SelectedBinding >= static_cast<int32>(Sequence->Bindings.size())) return;
	const bool bResumePreview = Player && Player->IsActive();
	StopPreview();
	FSequenceTrack Track;
	Track.BindingId = Sequence->Bindings[SelectedBinding].Id;
	Track.Type = Type;
	FSequenceSection Section;
	Section.StartFrame = Sequence->StartFrame;
	Section.EndFrame = Sequence->EndFrame;
	Track.Sections.push_back(Section);
	Sequence->Tracks.push_back(std::move(Track));
	SelectedTrack = static_cast<int32>(Sequence->Tracks.size()) - 1;
	SelectedSection = 0;
	SelectedKey = -1;
	CommitEdit();
	if (bResumePreview && EnsurePreview()) SelectPreviewActor();
}

void FSequencerPanel::AddSection()
{
	if (!Sequence || SelectedTrack < 0 || SelectedTrack >= static_cast<int32>(Sequence->Tracks.size())) return;
	const bool bResumePreview = Player && Player->IsActive();
	StopPreview();
	FSequenceSection Section;
	Section.StartFrame = CurrentFrame;
	Section.EndFrame = Sequence->EndFrame;
	Sequence->Tracks[SelectedTrack].Sections.push_back(Section);
	SelectedSection = static_cast<int32>(Sequence->Tracks[SelectedTrack].Sections.size()) - 1;
	SelectedKey = -1;
	CommitEdit();
	if (bResumePreview && EnsurePreview()) SelectPreviewActor();
}

AActor* FSequencerPanel::GetSelectedBoundActor()
{
	if (!Sequence || SelectedTrack < 0 || SelectedTrack >= static_cast<int32>(Sequence->Tracks.size())) return nullptr;
	if (!EnsurePreview()) return nullptr;
	return Player->GetBoundActor(Sequence->Tracks[SelectedTrack].BindingId);
}

void FSequencerPanel::AddKey()
{
	if (!Sequence || SelectedTrack < 0 || SelectedTrack >= static_cast<int32>(Sequence->Tracks.size())) return;
	FSequenceTrack& Track = Sequence->Tracks[SelectedTrack];
	if (SelectedSection < 0 || SelectedSection >= static_cast<int32>(Track.Sections.size())) return;
	FSequenceSection& Section = Track.Sections[SelectedSection];
	if (CurrentFrame < Section.StartFrame || CurrentFrame > Section.EndFrame)
	{
		Status = "Move playhead inside the selected section";
		return;
	}
	AActor* Actor = GetSelectedBoundActor();
	if (!Actor) { Status = "Binding target is unavailable"; return; }
	if (Track.Type == ESequenceTrackType::Transform)
	{
		auto& Keys = Section.TransformChannel.Keys;
		const FSequenceTransformKey Key{CurrentFrame, Actor->GetActorTransform()};
		auto It = std::lower_bound(Keys.begin(), Keys.end(), CurrentFrame,
			[](const FSequenceTransformKey& K, int32 Frame) { return K.Frame < Frame; });
		if (It != Keys.end() && It->Frame == CurrentFrame) *It = Key;
		else Keys.insert(It, Key);
		SelectedKey = static_cast<int32>(std::lower_bound(Keys.begin(), Keys.end(), CurrentFrame,
			[](const FSequenceTransformKey& K, int32 Frame) { return K.Frame < Frame; }) - Keys.begin());
	}
	else
	{
		UCameraComponent* Camera = Actor->GetComponentByClass<UCameraComponent>();
		if (!Camera) { Status = "Selected binding has no camera component"; return; }
		auto& Keys = Section.FloatChannel.Keys;
		const FSequenceFloatKey Key{CurrentFrame, Camera->GetFieldOfView()};
		auto It = std::lower_bound(Keys.begin(), Keys.end(), CurrentFrame,
			[](const FSequenceFloatKey& K, int32 Frame) { return K.Frame < Frame; });
		if (It != Keys.end() && It->Frame == CurrentFrame) *It = Key;
		else Keys.insert(It, Key);
		SelectedKey = static_cast<int32>(std::lower_bound(Keys.begin(), Keys.end(), CurrentFrame,
			[](const FSequenceFloatKey& K, int32 Frame) { return K.Frame < Frame; }) - Keys.begin());
	}
	CommitEdit();
	RefreshPreview();
}

bool FSequencerPanel::EnsurePreview()
{
	if (!World || !Sequence || (PreviewAllowedQuery && !PreviewAllowedQuery())) return false;
	if (!Sequence->IsValidSequence()) return false;
	if (!Player) Player.reset(NewObject<ULevelSequencePlayer>());
	if (!Player->IsActive() && !Player->Initialize(World, Sequence.get(), true)) return false;
	Player->SetLooping(bLooping);
	Player->SetPlayRate(PlayRate);
	if (!Player->IsActive() && !Player->Seek(CurrentFrame))
	{
		Status = "Preview could not resolve a binding";
		return false;
	}
	return true;
}

void FSequencerPanel::RefreshPreview()
{
	if (Player && Player->IsActive()) Player->Seek(CurrentFrame);
	FollowSelectedCamera();
}

void FSequencerPanel::FollowSelectedCamera()
{
	if (!bFollowCamera || !Player || !Player->IsActive() || !ViewportsPanel || !Sequence) return;
	if (SelectedBinding < 0 || SelectedBinding >= static_cast<int32>(Sequence->Bindings.size()))
	{
		RestoreViewportCamera();
		return;
	}
	AActor* Actor = Player->GetBoundActor(Sequence->Bindings[SelectedBinding].Id);
	if (!Actor) { RestoreViewportCamera(); return; }
	UCameraComponent* Camera = Actor->GetComponentByClass<UCameraComponent>();
	if (!Camera) { RestoreViewportCamera(); return; }
	const int32 Index = bSavedViewportCamera ? SavedViewportIndex : ViewportsPanel->GetActiveViewIndex();
	FEditorViewportClient* View = ViewportsPanel->GetViewportClient(Index);
	if (!View) return;
	if (!bSavedViewportCamera)
	{
		SavedViewportIndex = Index;
		SavedViewLocation = View->GetViewLocation();
		SavedViewRotation = View->GetViewRotation();
		SavedViewFov = View->GetViewFOV();
		bSavedViewportCamera = true;
	}
	View->SetViewLocation(Camera->GetWorldLocation());
	View->SetViewRotation(Camera->GetWorldRotation());
	View->SetViewFOV(Camera->GetFieldOfView());
}

void FSequencerPanel::DrawPreviewImage()
{
	if (!ViewportsPanel) return;
	FEditorViewportClient* View = ViewportsPanel->GetViewportClient(
		bSavedViewportCamera ? SavedViewportIndex : ViewportsPanel->GetActiveViewIndex());
	FTexture2D* Texture = View ? View->GetColorTarget() : nullptr;
	if (!Texture || !Texture->GetSRV()) return;
	const float Width = std::min(480.0f, ImGui::GetContentRegionAvail().x);
	if (Width > 10.0f) ImGui::Image((ImTextureID)Texture->GetSRV(), {Width, Width * 9.0f / 16.0f});
}

void FSequencerPanel::DrawToolbar()
{
	if (ImGui::Button("New")) RequestNewAsset();
	ImGui::SameLine();
	if (ImGui::Button("Open")) ImGui::OpenPopup("Open sequence");
	ImGui::SameLine();
	if (ImGui::Button("Save")) SaveAsset();
	ImGui::SameLine();
	ImGui::BeginDisabled(HistoryIndex == 0);
	if (ImGui::Button("Undo")) Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(History.empty() || HistoryIndex + 1 >= History.size());
	if (ImGui::Button("Redo")) Redo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted(bDirty ? "Unsaved changes" : "Saved");

	ImGui::TextUnformatted("Sequence file");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputText("##SequencePath", PathBuffer, sizeof(PathBuffer));
	if (ImGui::BeginPopup("Open sequence"))
	{
		if (PathBuffer[0] && ImGui::Selectable("Open path field"))
		{
			RequestOpenAsset(PathBuffer);
			ImGui::CloseCurrentPopup();
		}
		ImGui::Separator();
		std::error_code Error;
		std::filesystem::directory_iterator It("Assets/Sequences", Error);
		const std::filesystem::directory_iterator End;
		for (; !Error && It != End; It.increment(Error))
		{
			if (!It->is_regular_file(Error)) continue;
			const FString Path = It->path().generic_string();
			if (Path.ends_with(".sequence.json") && ImGui::Selectable(It->path().filename().string().c_str()))
			{
				RequestOpenAsset(Path);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}
	if (PendingAction != EPendingAction::None) ImGui::OpenPopup("Unsaved sequence");
	if (ImGui::BeginPopupModal("Unsaved sequence", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Save changes to the current sequence?");
		if (ImGui::Button("Save"))
		{
			SaveAsset();
			if (!bDirty)
			{
				if (PendingAction == EPendingAction::New) NewAsset();
				else if (PendingAction == EPendingAction::Open) OpenAsset(PendingOpenPath);
				PendingAction = EPendingAction::None;
				PendingOpenPath.clear();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			if (PendingAction == EPendingAction::New) NewAsset();
			else if (PendingAction == EPendingAction::Open) OpenAsset(PendingOpenPath);
			PendingAction = EPendingAction::None;
			PendingOpenPath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			PendingAction = EPendingAction::None;
			PendingOpenPath.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::Separator();
	ImGui::BeginDisabled(!World || (PreviewAllowedQuery && !PreviewAllowedQuery()));
	if (ImGui::Button(Player && Player->IsPlaying() ? "Pause" : "Play"))
	{
		if (Player && Player->IsPlaying()) Player->Pause();
		else if (EnsurePreview() && !Player->Play()) Status = "Could not play sequence";
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) StopPreview();
	ImGui::SameLine();
	ImGui::Checkbox("Follow camera", &bFollowCamera);
	if (!bFollowCamera) RestoreViewportCamera();
	ImGui::SameLine();
	if (ImGui::Checkbox("Loop", &bLooping) && Player) Player->SetLooping(bLooping);
	ImGui::EndDisabled();
	if (Sequence && ImGui::BeginTable("##SequenceControls", 2, ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Frame");
		int32 Frame = CurrentFrame;
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::BeginDisabled(!World || (PreviewAllowedQuery && !PreviewAllowedQuery()));
		if (ImGui::InputInt("##Frame", &Frame))
		{
			CurrentFrame = std::clamp(Frame, Sequence->StartFrame, Sequence->EndFrame);
			if (EnsurePreview()) RefreshPreview();
		}
		ImGui::EndDisabled();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("FPS");
		int32 Fps = Sequence->FramesPerSecond;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputInt("##FPS", &Fps) && Fps > 0 && Fps <= 240)
		{
			Sequence->FramesPerSecond = Fps;
			CommitEdit();
		}
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("End frame");
		int32 EndFrame = Sequence->EndFrame;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputInt("##EndFrame", &EndFrame) && EndFrame >= Sequence->StartFrame)
		{
			int32 Minimum = Sequence->StartFrame;
			for (const FSequenceTrack& Track : Sequence->Tracks)
				for (const FSequenceSection& Section : Track.Sections)
					Minimum = std::max(Minimum, Section.EndFrame);
			Sequence->EndFrame = std::max(EndFrame, Minimum);
			CurrentFrame = std::min(CurrentFrame, Sequence->EndFrame);
			CommitEdit();
			RefreshPreview();
		}
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("Speed");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat("##Speed", &PlayRate, 0.05f, 0.1f, 4.0f, "%.2fx") && Player)
			Player->SetPlayRate(PlayRate);
		ImGui::EndTable();
	}
	ImGui::TextUnformatted("Timeline zoom");
	ImGui::SetNextItemWidth(std::min(300.0f, ImGui::GetContentRegionAvail().x));
	ImGui::SliderFloat("##TimelineZoom", &PixelsPerFrame, 2.0f, 24.0f, "%.1f px/frame");
	if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
}

void FSequencerPanel::DrawBindings()
{
	if (!Sequence) return;
	ImGui::TextUnformatted("Bindings");
	if (ImGui::Button("Choose scene Actor..."))
	{
		bPickerSpawns = false;
		ActorSearchBuffer[0] = '\0';
		ImGui::OpenPopup("Choose scene Actor");
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bind an Actor already placed in the scene.");
	AActor* PickedActor = nullptr;
	if (ImGui::BeginPopup("Choose scene Actor"))
	{
		ImGui::TextUnformatted(bPickerSpawns ? "Choose an Actor to spawn a copy" : "Choose an Actor to control");
		ImGui::SetNextItemWidth(330.0f);
		ImGui::InputTextWithHint("##ActorSearch", "Search name or class", ActorSearchBuffer, sizeof(ActorSearchBuffer));
		ImGui::Separator();
		ImGui::BeginChild("##SceneActors", {360.0f, 280.0f}, ImGuiChildFlags_Borders);
		bool bAnyActor = false;
		if (World)
			for (ULevel* Level : World->GetLevels())
				if (Level) for (AActor* Actor : Level->GetActors())
				{
					if (!LiveActor(World, Actor) || Actor->HasAnyFlags(EObjectFlags::RF_Transient)) continue;
					const FString Label = Actor->GetName() + " (" + Actor->GetClass()->Name + ")";
					if (ActorSearchBuffer[0] && Label.find(ActorSearchBuffer) == FString::npos) continue;
					bAnyActor = true;
					ImGui::PushID(Actor);
					if (ImGui::Selectable(Label.c_str())) PickedActor = Actor;
					const FVector Position = Actor->GetActorLocation();
					ImGui::TextDisabled("  (%.0f, %.0f, %.0f)", Position.X, Position.Y, Position.Z);
					ImGui::PopID();
				}
		if (!bAnyActor) ImGui::TextDisabled("No matching scene actors");
		ImGui::EndChild();
		if (PickedActor) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if (PickedActor) AddBinding(bPickerSpawns ? ESequenceBindingType::Spawnable : ESequenceBindingType::Possessable,
		nullptr, PickedActor);
	bool bOpenSpawnPicker = false;
	if (ImGui::TreeNode("Create a new Actor"))
	{
		if (ImGui::Button("Spawn copy of scene Actor..."))
		{
			bPickerSpawns = true;
			ActorSearchBuffer[0] = '\0';
			bOpenSpawnPicker = true;
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capture the chosen Actor and its components in this sequence asset.");
		if (ImGui::Button("New Camera")) AddBinding(ESequenceBindingType::Spawnable, ACameraActor::StaticClass());
		if (ImGui::Button("Replaceable Camera")) AddBinding(ESequenceBindingType::Replaceable, ACameraActor::StaticClass());
		ImGui::TreePop();
	}
	if (bOpenSpawnPicker) ImGui::OpenPopup("Choose scene Actor");
	ImGui::Separator();
	for (int32 Index = 0; Index < static_cast<int32>(Sequence->Bindings.size()); ++Index)
	{
		const FSequenceBinding& Binding = Sequence->Bindings[Index];
		const char* Kind = Binding.Type == ESequenceBindingType::Spawnable ? "Spawn" :
			Binding.Type == ESequenceBindingType::Replaceable ? "Replace" : "Scene";
		FString Label = Binding.Id + " [" + Kind + "]";
		AActor* SceneActor = Binding.Type == ESequenceBindingType::Possessable
			? FSequenceBindingResolver::FindActor(World, Binding.ActorId) : nullptr;
		if (Binding.Type == ESequenceBindingType::Possessable && !SceneActor) Label += " (missing)";
		ImGui::PushID(Index);
		if (ImGui::Selectable(Label.c_str(), SelectedBinding == Index))
		{
			RestoreViewportCamera();
			SelectedBinding = Index;
			SelectedTrack = SelectedSection = SelectedKey = -1;
			SelectPreviewActor();
		}
		if (SceneActor) ImGui::TextDisabled("  %s (%s)", SceneActor->GetName().c_str(), SceneActor->GetClass()->Name.c_str());
		else if (Binding.Type != ESequenceBindingType::Possessable)
			ImGui::TextDisabled("  %s%s%s", Binding.ActorClass.c_str(),
				Binding.SourceActorName.empty() ? "" : " <- ", Binding.SourceActorName.c_str());
		ImGui::PopID();
	}
	if (SelectedBinding < 0 || SelectedBinding >= static_cast<int32>(Sequence->Bindings.size())) return;
	FSequenceBinding& Binding = Sequence->Bindings[SelectedBinding];
	ImGui::Separator();
	ImGui::Text("Binding: %s", Binding.Id.c_str());
	if (Binding.Type == ESequenceBindingType::Possessable)
	{
		AActor* Actor = FSequenceBindingResolver::FindActor(World, Binding.ActorId);
		ImGui::TextWrapped("Scene actor: %s", Actor ? Actor->GetName().c_str() : "Missing");
	}
	else
	{
		ImGui::TextWrapped("Spawn class: %s", Binding.ActorClass.c_str());
		if (!Binding.SourceActorName.empty())
		{
			ImGui::TextWrapped("Captured from: %s", Binding.SourceActorName.c_str());
			ImGui::TextDisabled("Saved copy; the source Actor is unchanged.");
		}
		ImGui::TextDisabled("Preview actor is temporary; Stop removes it.");
		if (Binding.ActorClass == AActor::StaticClass()->Name)
			ImGui::TextDisabled("Empty Actor has no visible mesh. Use its selection gizmo.");
		ImGui::TextUnformatted("Spawn position");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat3("##SpawnPosition", &Binding.SpawnTransform.Location.X, 1.0f)) bDirty = true;
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			StopPreview();
			CommitEdit();
			if (EnsurePreview()) SelectPreviewActor();
		}
		if (ImGui::Button("Place in front of view") && ViewportsPanel)
			if (FEditorViewportClient* View = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex()))
			{
				StopPreview();
				Binding.SpawnTransform.Location = View->GetViewLocation() + View->GetViewForward() * 300.0f;
				CommitEdit();
				if (EnsurePreview()) SelectPreviewActor();
			}
		if (Player && Player->IsActive())
			if (AActor* Preview = Player->GetBoundActor(Binding.Id))
			{
				const FVector Position = Preview->GetActorLocation();
				ImGui::Text("Preview: %.0f, %.0f, %.0f", Position.X, Position.Y, Position.Z);
			}
	}
	if (ImGui::Button("Show / focus in viewport")) FocusSelectedBinding();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Evaluate this frame, select the bound Actor, and center the viewport on it.");
	if (ImGui::Button("Add Transform track")) AddTrack(ESequenceTrackType::Transform);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animate position, rotation, and scale. Add keys in the track inspector.");
	AActor* BoundActor = Binding.Type == ESequenceBindingType::Possessable
		? FSequenceBindingResolver::FindActor(World, Binding.ActorId)
		: Player && Player->IsActive() ? Player->GetBoundActor(Binding.Id) : nullptr;
	const bool bHasCamera = BoundActor ? BoundActor->GetComponentByClass<UCameraComponent>() != nullptr
		: Binding.ActorClass == ACameraActor::StaticClass()->Name;
	ImGui::BeginDisabled(!bHasCamera);
	if (ImGui::Button("Add FOV track")) AddTrack(ESequenceTrackType::FieldOfView);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animate a camera's field of view. Add keys in the track inspector.");
	ImGui::EndDisabled();
	if (!bHasCamera) ImGui::TextDisabled("FOV track needs a camera component");
	if (ImGui::Button("Delete binding"))
	{
		const FString Id = Sequence->Bindings[SelectedBinding].Id;
		StopPreview();
		Sequence->Tracks.erase(std::remove_if(Sequence->Tracks.begin(), Sequence->Tracks.end(),
			[&](const FSequenceTrack& Track) { return Track.BindingId == Id; }), Sequence->Tracks.end());
		Sequence->Bindings.erase(Sequence->Bindings.begin() + SelectedBinding);
		SelectedBinding = SelectedTrack = SelectedSection = SelectedKey = -1;
		CommitEdit();
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this binding and its tracks. The scene Actor is kept.");
}

void FSequencerPanel::DrawTimeline()
{
	if (!Sequence) return;
	const float Width = HeaderWidth + (Sequence->EndFrame - Sequence->StartFrame + 2) * PixelsPerFrame;
	const float Height = RulerHeight + std::max<size_t>(1, Sequence->Tracks.size()) * RowHeight;
	ImGui::BeginChild("Sequence timeline", {0.0f, 330.0f}, ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar);
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasSize{std::max(Width, ImGui::GetContentRegionAvail().x), Height};
	ImGui::InvisibleButton("##SequenceCanvas", CanvasSize, ImGuiButtonFlags_MouseButtonLeft);
	const ImVec2 Mouse = ImGui::GetMousePos();
	const auto FrameFromX = [&](float X)
	{
		return std::clamp(Sequence->StartFrame + static_cast<int32>(std::lround(
			(X - Origin.x - HeaderWidth) / PixelsPerFrame)), Sequence->StartFrame, Sequence->EndFrame);
	};
	const auto XFromFrame = [&](int32 Frame)
	{
		return Origin.x + HeaderWidth + (Frame - Sequence->StartFrame) * PixelsPerFrame;
	};
	const bool bHovered = ImGui::IsItemHovered();
	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const int32 ClickedFrame = FrameFromX(Mouse.x);
		DragStartFrame = ClickedFrame;
		DragMode = EDragMode::None;
		bDragChanged = false;
		const int32 Row = static_cast<int32>((Mouse.y - Origin.y - RulerHeight) / RowHeight);
		if (Mouse.y >= Origin.y + RulerHeight && Row >= 0 && Row < static_cast<int32>(Sequence->Tracks.size()))
		{
			SelectedTrack = Row;
			SelectedSection = SelectedKey = -1;
			FSequenceTrack& Track = Sequence->Tracks[Row];
			for (int32 I = 0; I < static_cast<int32>(Sequence->Bindings.size()); ++I)
				if (Sequence->Bindings[I].Id == Track.BindingId) { SelectedBinding = I; break; }
			if (Mouse.x >= Origin.x + HeaderWidth)
			{
				const float CenterY = Origin.y + RulerHeight + Row * RowHeight + RowHeight * 0.5f;
				for (int32 S = static_cast<int32>(Track.Sections.size()) - 1; S >= 0 && DragMode == EDragMode::None; --S)
				{
					FSequenceSection& Section = Track.Sections[S];
					const int32 KeyCount = Track.Type == ESequenceTrackType::Transform
						? static_cast<int32>(Section.TransformChannel.Keys.size())
						: static_cast<int32>(Section.FloatChannel.Keys.size());
					for (int32 K = 0; K < KeyCount; ++K)
					{
						const int32 KeyFrame = Track.Type == ESequenceTrackType::Transform
							? Section.TransformChannel.Keys[K].Frame : Section.FloatChannel.Keys[K].Frame;
						if (std::abs(Mouse.x - XFromFrame(KeyFrame)) <= 7.0f && std::abs(Mouse.y - CenterY) <= 9.0f)
						{
							SelectedSection = S;
							SelectedKey = K;
							DragOriginalFrame = KeyFrame;
							DragMode = EDragMode::Key;
							break;
						}
					}
					if (DragMode != EDragMode::None) break;
					const float StartX = XFromFrame(Section.StartFrame);
					const float EndX = XFromFrame(Section.EndFrame);
					if (Mouse.x < StartX - 6.0f || Mouse.x > EndX + 6.0f) continue;
					SelectedSection = S;
					DragOriginalStart = Section.StartFrame;
					DragOriginalEnd = Section.EndFrame;
					DragOriginalKeyFrames.clear();
					if (Track.Type == ESequenceTrackType::Transform)
						for (const auto& Key : Section.TransformChannel.Keys) DragOriginalKeyFrames.push_back(Key.Frame);
					else
						for (const auto& Key : Section.FloatChannel.Keys) DragOriginalKeyFrames.push_back(Key.Frame);
					DragMode = std::abs(Mouse.x - StartX) <= 6.0f ? EDragMode::SectionStart :
						std::abs(Mouse.x - EndX) <= 6.0f ? EDragMode::SectionEnd : EDragMode::SectionMove;
				}
			}
		}
		if (DragMode == EDragMode::None && Mouse.x >= Origin.x + HeaderWidth) DragMode = EDragMode::Scrub;
		if (DragMode == EDragMode::Scrub)
		{
			CurrentFrame = ClickedFrame;
			if (EnsurePreview()) RefreshPreview();
		}
	}
	if (DragMode != EDragMode::None && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const int32 Frame = FrameFromX(Mouse.x);
		const int32 Delta = Frame - DragStartFrame;
		if (DragMode == EDragMode::Scrub)
		{
			if (CurrentFrame != Frame)
			{
				CurrentFrame = Frame;
				if (EnsurePreview()) RefreshPreview();
			}
		}
		else if (SelectedTrack >= 0 && SelectedTrack < static_cast<int32>(Sequence->Tracks.size()))
		{
			FSequenceTrack& Track = Sequence->Tracks[SelectedTrack];
			if (SelectedSection >= 0 && SelectedSection < static_cast<int32>(Track.Sections.size()))
			{
				FSequenceSection& Section = Track.Sections[SelectedSection];
				bool bChanged = false;
				if (DragMode == EDragMode::Key && SelectedKey >= 0)
				{
					const int32 KeyCount = Track.Type == ESequenceTrackType::Transform
						? static_cast<int32>(Section.TransformChannel.Keys.size())
						: static_cast<int32>(Section.FloatChannel.Keys.size());
					if (SelectedKey < KeyCount)
					{
						const int32 Previous = SelectedKey > 0 ? (Track.Type == ESequenceTrackType::Transform
							? Section.TransformChannel.Keys[SelectedKey - 1].Frame
							: Section.FloatChannel.Keys[SelectedKey - 1].Frame) + 1 : Section.StartFrame;
						const int32 Next = SelectedKey + 1 < KeyCount ? (Track.Type == ESequenceTrackType::Transform
							? Section.TransformChannel.Keys[SelectedKey + 1].Frame
							: Section.FloatChannel.Keys[SelectedKey + 1].Frame) - 1 : Section.EndFrame;
						const int32 NewFrame = std::clamp(DragOriginalFrame + Delta, Previous, Next);
						int32& KeyFrame = Track.Type == ESequenceTrackType::Transform
							? Section.TransformChannel.Keys[SelectedKey].Frame : Section.FloatChannel.Keys[SelectedKey].Frame;
						bChanged = KeyFrame != NewFrame;
						KeyFrame = NewFrame;
					}
				}
				else if (DragMode == EDragMode::SectionMove)
				{
					const int32 Length = DragOriginalEnd - DragOriginalStart;
					const int32 NewStart = std::clamp(DragOriginalStart + Delta,
						Sequence->StartFrame, Sequence->EndFrame - Length);
					const int32 Offset = NewStart - DragOriginalStart;
					bChanged = Section.StartFrame != NewStart;
					Section.StartFrame = NewStart;
					Section.EndFrame = DragOriginalEnd + Offset;
					for (int32 K = 0; K < static_cast<int32>(DragOriginalKeyFrames.size()); ++K)
						if (Track.Type == ESequenceTrackType::Transform)
							Section.TransformChannel.Keys[K].Frame = DragOriginalKeyFrames[K] + Offset;
						else Section.FloatChannel.Keys[K].Frame = DragOriginalKeyFrames[K] + Offset;
				}
				else if (DragMode == EDragMode::SectionStart)
				{
					const int32 FirstKey = Track.Type == ESequenceTrackType::Transform && !Section.TransformChannel.Keys.empty()
						? Section.TransformChannel.Keys.front().Frame
						: Track.Type == ESequenceTrackType::FieldOfView && !Section.FloatChannel.Keys.empty()
							? Section.FloatChannel.Keys.front().Frame : DragOriginalEnd;
					const int32 NewStart = std::clamp(DragOriginalStart + Delta,
						Sequence->StartFrame, std::min(DragOriginalEnd, FirstKey));
					bChanged = Section.StartFrame != NewStart;
					Section.StartFrame = NewStart;
				}
				else if (DragMode == EDragMode::SectionEnd)
				{
					const int32 LastKey = Track.Type == ESequenceTrackType::Transform && !Section.TransformChannel.Keys.empty()
						? Section.TransformChannel.Keys.back().Frame
						: Track.Type == ESequenceTrackType::FieldOfView && !Section.FloatChannel.Keys.empty()
							? Section.FloatChannel.Keys.back().Frame : DragOriginalStart;
					const int32 NewEnd = std::clamp(DragOriginalEnd + Delta,
						std::max(DragOriginalStart, LastKey), Sequence->EndFrame);
					bChanged = Section.EndFrame != NewEnd;
					Section.EndFrame = NewEnd;
				}
				if (bChanged)
				{
					bDragChanged = true;
					RefreshPreview();
				}
			}
		}
	}
	if (DragMode != EDragMode::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		if (bDragChanged) CommitEdit();
		DragMode = EDragMode::None;
		bDragChanged = false;
	}

	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImVec2 End{Origin.x + CanvasSize.x, Origin.y + CanvasSize.y};
	Draw->AddRectFilled(Origin, End, IM_COL32(35, 39, 48, 255));
	Draw->AddRectFilled(Origin, {End.x, Origin.y + RulerHeight}, IM_COL32(48, 53, 65, 255));
	Draw->AddRectFilled(Origin, {Origin.x + HeaderWidth, End.y}, IM_COL32(45, 50, 61, 255));
	Draw->AddLine({Origin.x + HeaderWidth, Origin.y}, {Origin.x + HeaderWidth, End.y},
		IM_COL32(125, 137, 154, 255), 1.5f);
	if (Sequence->Tracks.empty())
	{
		Draw->AddText({Origin.x + 12.0f, Origin.y + RulerHeight + 14.0f},
			IM_COL32(220, 225, 232, 255), "No tracks yet");
		Draw->AddText({Origin.x + HeaderWidth + 14.0f, Origin.y + RulerHeight + 14.0f},
			IM_COL32(200, 207, 220, 255), "Select a binding, then add a track and keys");
	}
	for (int32 Frame = Sequence->StartFrame; Frame <= Sequence->EndFrame; Frame += 10)
	{
		const float X = XFromFrame(Frame);
		Draw->AddLine({X, Origin.y}, {X, End.y}, IM_COL32(60, 65, 76, 255));
		Draw->AddText({X + 3.0f, Origin.y + 5.0f}, IM_COL32(200, 200, 210, 255),
			std::to_string(Frame).c_str());
	}
	for (int32 T = 0; T < static_cast<int32>(Sequence->Tracks.size()); ++T)
	{
		const FSequenceTrack& Track = Sequence->Tracks[T];
		const float Top = Origin.y + RulerHeight + T * RowHeight;
		const float Center = Top + RowHeight * 0.5f;
		Draw->AddLine({Origin.x, Top}, {End.x, Top}, IM_COL32(55, 60, 69, 255));
		const FString Label = Track.BindingId + " / " + TrackLabel(Track.Type);
		Draw->AddText({Origin.x + 8.0f, Top + 12.0f},
			T == SelectedTrack ? IM_COL32(255, 210, 100, 255) : IM_COL32(220, 220, 225, 255), Label.c_str());
		for (int32 S = 0; S < static_cast<int32>(Track.Sections.size()); ++S)
		{
			const FSequenceSection& Section = Track.Sections[S];
			const float Left = XFromFrame(Section.StartFrame);
			const float Right = XFromFrame(Section.EndFrame);
			Draw->AddRectFilled({Left, Top + 6.0f}, {std::max(Left + 3.0f, Right), Top + RowHeight - 6.0f},
				S == SelectedSection && T == SelectedTrack ? IM_COL32(78, 112, 174, 255) : IM_COL32(59, 80, 121, 255), 3.0f);
			const int32 KeyCount = Track.Type == ESequenceTrackType::Transform
				? static_cast<int32>(Section.TransformChannel.Keys.size()) : static_cast<int32>(Section.FloatChannel.Keys.size());
			if (KeyCount == 0 && Right - Left > 70.0f)
				Draw->AddText({Left + 8.0f, Top + 14.0f}, IM_COL32(195, 208, 235, 255), "No keys");
			for (int32 K = 0; K < KeyCount; ++K)
			{
				const int32 KeyFrame = Track.Type == ESequenceTrackType::Transform
					? Section.TransformChannel.Keys[K].Frame : Section.FloatChannel.Keys[K].Frame;
				Draw->AddCircleFilled({XFromFrame(KeyFrame), Center}, K == SelectedKey && S == SelectedSection &&
					T == SelectedTrack ? 6.0f : 4.5f, IM_COL32(255, 205, 90, 255));
			}
		}
	}
	Draw->AddLine({XFromFrame(CurrentFrame), Origin.y}, {XFromFrame(CurrentFrame), End.y},
		IM_COL32(255, 110, 80, 255), 2.0f);
	ImGui::EndChild();
}

void FSequencerPanel::DrawInspector()
{
	if (!Sequence || SelectedTrack < 0 || SelectedTrack >= static_cast<int32>(Sequence->Tracks.size())) return;
	FSequenceTrack& Track = Sequence->Tracks[SelectedTrack];
	ImGui::SeparatorText("Track inspector");
	ImGui::Text("%s / %s", Track.BindingId.c_str(), TrackLabel(Track.Type));
	if (ImGui::Button("Add section")) { AddSection(); return; }
	ImGui::SameLine();
	if (ImGui::Button("Delete track"))
	{
		StopPreview();
		Sequence->Tracks.erase(Sequence->Tracks.begin() + SelectedTrack);
		SelectedTrack = SelectedSection = SelectedKey = -1;
		CommitEdit();
		return;
	}
	if (SelectedSection < 0 || SelectedSection >= static_cast<int32>(Track.Sections.size())) return;
	FSequenceSection& Section = Track.Sections[SelectedSection];
	ImGui::Text("Section %d", SelectedSection + 1);
	if (ImGui::Button("Add key at playhead")) AddKey();
	ImGui::SameLine();
	if (ImGui::Button("Delete section"))
	{
		StopPreview();
		Track.Sections.erase(Track.Sections.begin() + SelectedSection);
		SelectedSection = SelectedKey = -1;
		CommitEdit();
		return;
	}
	int32 Start = Section.StartFrame;
	int32 End = Section.EndFrame;
	const bool bStartChanged = ImGui::InputInt("Section start", &Start);
	const bool bEndChanged = ImGui::InputInt("Section end", &End);
	if (bStartChanged || bEndChanged)
	{
		const int32 FirstKey = Track.Type == ESequenceTrackType::Transform && !Section.TransformChannel.Keys.empty()
			? Section.TransformChannel.Keys.front().Frame : Track.Type == ESequenceTrackType::FieldOfView &&
			!Section.FloatChannel.Keys.empty() ? Section.FloatChannel.Keys.front().Frame : Sequence->EndFrame;
		const int32 LastKey = Track.Type == ESequenceTrackType::Transform && !Section.TransformChannel.Keys.empty()
			? Section.TransformChannel.Keys.back().Frame : Track.Type == ESequenceTrackType::FieldOfView &&
			!Section.FloatChannel.Keys.empty() ? Section.FloatChannel.Keys.back().Frame : Sequence->StartFrame;
		const int32 NewStart = std::clamp(Start, Sequence->StartFrame, std::min(Section.EndFrame, FirstKey));
		const int32 NewEnd = std::clamp(End, std::max(NewStart, LastKey), Sequence->EndFrame);
		if (NewStart != Section.StartFrame || NewEnd != Section.EndFrame)
		{
			Section.StartFrame = NewStart;
			Section.EndFrame = NewEnd;
			CommitEdit();
			RefreshPreview();
		}
	}
	const int32 KeyCount = Track.Type == ESequenceTrackType::Transform
		? static_cast<int32>(Section.TransformChannel.Keys.size()) : static_cast<int32>(Section.FloatChannel.Keys.size());
	if (SelectedKey < 0 || SelectedKey >= KeyCount) return;
	ImGui::SeparatorText("Keyframe");
	if (ImGui::Button("Delete key"))
	{
		if (Track.Type == ESequenceTrackType::Transform)
			Section.TransformChannel.Keys.erase(Section.TransformChannel.Keys.begin() + SelectedKey);
		else Section.FloatChannel.Keys.erase(Section.FloatChannel.Keys.begin() + SelectedKey);
		SelectedKey = -1;
		CommitEdit();
		RefreshPreview();
		return;
	}
	bool bChanged = false;
	bool bFinished = false;
	if (Track.Type == ESequenceTrackType::Transform)
	{
		FSequenceTransformKey& Key = Section.TransformChannel.Keys[SelectedKey];
		ImGui::Text("Frame %d", Key.Frame);
		bChanged |= ImGui::DragFloat3("Location", &Key.Value.Location.X, 0.1f);
		bFinished |= ImGui::IsItemDeactivatedAfterEdit();
		bChanged |= ImGui::DragFloat3("Rotation", &Key.Value.Rotation.Pitch, 0.1f);
		bFinished |= ImGui::IsItemDeactivatedAfterEdit();
		bChanged |= ImGui::DragFloat3("Scale", &Key.Value.Scale.X, 0.01f);
		bFinished |= ImGui::IsItemDeactivatedAfterEdit();
		int Interpolation = static_cast<int>(Key.Interpolation);
		if (ImGui::Combo("Interpolation", &Interpolation, "Constant\0Linear\0"))
		{
			Key.Interpolation = static_cast<ESequenceInterpolation>(Interpolation);
			bChanged = bFinished = true;
		}
	}
	else
	{
		FSequenceFloatKey& Key = Section.FloatChannel.Keys[SelectedKey];
		ImGui::Text("Frame %d", Key.Frame);
		bChanged |= ImGui::DragFloat("Field of view", &Key.Value, 0.1f, 1.0f, 179.0f);
		bFinished |= ImGui::IsItemDeactivatedAfterEdit();
		int Interpolation = static_cast<int>(Key.Interpolation);
		if (ImGui::Combo("Interpolation", &Interpolation, "Constant\0Linear\0"))
		{
			Key.Interpolation = static_cast<ESequenceInterpolation>(Interpolation);
			bChanged = bFinished = true;
		}
	}
	if (bChanged) RefreshPreview();
	if (bFinished) CommitEdit();
}

void FSequencerPanel::OnRender()
{
	if (!Sequence) NewAsset();
	bool bOpen = IsOpen();
	ImGui::SetNextWindowSize({1300.0f, 800.0f}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Sequencer", &bOpen))
	{
		ImGui::End();
		SetOpen(bOpen);
		return;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.19f, 0.23f, 0.29f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.25f, 0.31f, 0.39f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.29f, 0.35f, 0.44f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.47f, 0.54f, 0.64f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.22f, 0.36f, 0.53f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.43f, 0.62f, 1.0f));
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
		if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
	}
	DrawToolbar();
	DrawPreviewImage();
	const bool bPreviewUnavailable = PreviewAllowedQuery && !PreviewAllowedQuery();
	ImGui::BeginDisabled(bPreviewUnavailable);
	if (ImGui::BeginTable("##SequencerLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthFixed, 280.0f);
		ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawBindings();
		ImGui::TableSetColumnIndex(1);
		DrawTimeline();
		DrawInspector();
		ImGui::EndTable();
	}
	ImGui::EndDisabled();
	ImGui::PopStyleColor(6);
	ImGui::PopStyleVar();
	ImGui::End();
	SetOpen(bOpen);
	if (!bOpen) StopPreview();
}



