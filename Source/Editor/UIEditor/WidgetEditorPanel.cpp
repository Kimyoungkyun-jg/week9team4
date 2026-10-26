#include "EnginePCH.h"
#include "Editor/UIEditor/WidgetEditorPanel.h"
#include "UI/WidgetAsset.h"
#include "Asset/AssetManager.h"
#include "Render/Texture2D.h"
#include "UObject/UObjectIterator.h"
#include "Core/EngineLog.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace
{
	ImU32 ToColor(const FLinearColor& Color)
	{
		return ImGui::ColorConvertFloat4ToU32({Color.R, Color.G, Color.B, Color.A});
	}

	bool Contains(const FRect& Rect, ImVec2 Point)
	{
		return Point.x >= Rect.X && Point.y >= Rect.Y &&
			Point.x <= Rect.X + Rect.Width && Point.y <= Rect.Y + Rect.Height;
	}
}

bool FWidgetEditorPanel::Init()
{
	SetOpen(false);
	return true;
}

void FWidgetEditorPanel::ReleaseAsset()
{
	Selected = nullptr;
	Root.reset();
	History.clear();
	PendingDragged = nullptr;
	PendingParent = nullptr;
	PendingAction = EPendingAction::None;
	PendingPath.clear();
}

void FWidgetEditorPanel::NewAsset()
{
	Root.reset(NewObject<UCanvasWidget>());
	Root->SetWidgetName("Root");
	Selected = Root.get();
	AssetPath = "Assets/UI/NewWidget.ui.json";
	for (int32 Index = 1; std::filesystem::exists(AssetPath); ++Index)
		AssetPath = "Assets/UI/NewWidget" + std::to_string(Index) + ".ui.json";
	std::snprintf(PathBuffer, sizeof(PathBuffer), "%s", AssetPath.c_str());
	NewWidgetIndex = 1;
	bDirty = false;
	Select(Root.get());
	ResetHistory();
	ActionStatus = "New UI asset created";
}

bool FWidgetEditorPanel::RequestOpenAsset(const FString& Path)
{
	if (!bDirty) return OpenAsset(Path);
	PendingAction = EPendingAction::Open;
	PendingPath = Path;
	SetOpen(true);
	return true;
}

void FWidgetEditorPanel::RequestNewAsset()
{
	if (!bDirty) { NewAsset(); return; }
	PendingAction = EPendingAction::New;
}

void FWidgetEditorPanel::ApplyPendingAction()
{
	if (PendingAction == EPendingAction::New) NewAsset();
	else if (PendingAction == EPendingAction::Open && !OpenAsset(PendingPath))
		HTR_LOG(Error, "Failed to open UI asset: {}", PendingPath);
	PendingAction = EPendingAction::None;
	PendingPath.clear();
}

bool FWidgetEditorPanel::OpenAsset(const FString& Path)
{
	TUniquePtr<UCanvasWidget> Loaded = FWidgetAsset::Load(Path);
	if (!Loaded) { ActionStatus = "Failed to open: " + Path; return false; }
	Root = std::move(Loaded);
	AssetPath = Path;
	std::snprintf(PathBuffer, sizeof(PathBuffer), "%s", AssetPath.c_str());
	bDirty = false;
	Select(Root.get());
	ResetHistory();
	SetOpen(true);
	ActionStatus = "Opened: " + Path;
	return true;
}

void FWidgetEditorPanel::SaveAsset()
{
	if (!Root || PathBuffer[0] == '\0') { ActionStatus = "Enter a UI asset path before saving"; return; }
	const FString Path = PathBuffer;
	if (!Path.ends_with(".ui.json") || !FWidgetAsset::Save(*Root, Path))
	{
		HTR_LOG(Error, "Failed to save UI asset: {}", Path);
		ActionStatus = "Failed to save: " + Path;
		return;
	}
	AssetPath = Path;
	bDirty = false;
	SavedSnapshot = FWidgetAsset::Serialize(*Root);
	ActionStatus = "Saved: " + Path;
}

void FWidgetEditorPanel::ResetHistory()
{
	History.clear();
	SavedSnapshot = Root ? FWidgetAsset::Serialize(*Root) : FString{};
	History.push_back(SavedSnapshot);
	HistoryIndex = 0;
	bHistoryTransactionOpen = false;
	LastHistoryActiveID = 0;
}

void FWidgetEditorPanel::MarkDirty()
{
	if (!Root) return;
	ActionStatus.clear();
	FString Snapshot = FWidgetAsset::Serialize(*Root);
	if (Snapshot.empty()) return;
	const ImGuiID ActiveID = ImGui::GetActiveID();
	if (bHistoryTransactionOpen && ActiveID != 0 && ActiveID == LastHistoryActiveID && HistoryIndex > 0)
		History[HistoryIndex] = std::move(Snapshot);
	else if (History.empty() || History[HistoryIndex] != Snapshot)
	{
		History.erase(History.begin() + HistoryIndex + 1, History.end());
		History.push_back(std::move(Snapshot));
		if (History.size() > 64) { History.erase(History.begin()); }
		HistoryIndex = History.size() - 1;
	}
	bHistoryTransactionOpen = ActiveID != 0;
	LastHistoryActiveID = ActiveID;
	bDirty = History[HistoryIndex] != SavedSnapshot;
}

void FWidgetEditorPanel::RestoreHistory()
{
	if (History.empty()) return;
	TUniquePtr<UCanvasWidget> Restored = FWidgetAsset::Deserialize(History[HistoryIndex]);
	if (!Restored) return;
	Root = std::move(Restored);
	Select(Root.get());
	bDirty = History[HistoryIndex] != SavedSnapshot;
	bHistoryTransactionOpen = false;
	LastHistoryActiveID = 0;
}

void FWidgetEditorPanel::Undo()
{
	if (HistoryIndex > 0) { --HistoryIndex; RestoreHistory(); ActionStatus = "Undo applied"; }
}

void FWidgetEditorPanel::Redo()
{
	if (HistoryIndex + 1 < History.size()) { ++HistoryIndex; RestoreHistory(); ActionStatus = "Redo applied"; }
}

void FWidgetEditorPanel::DuplicateSelected()
{
	if (!Selected || Selected == Root.get() || !Selected->GetParent()) return;
	TUniquePtr<UWidget> Copy = FWidgetAsset::Clone(*Selected);
	if (!Copy) return;
	Copy->SetWidgetName(Selected->GetWidgetName() + "Copy");
	const FVector2 Position = Copy->GetPosition();
	Copy->SetPosition({Position.X + 20.0f, Position.Y + 20.0f});
	UCanvasWidget* Parent = Selected->GetParent();
	UWidget* NewSelected = Copy.get();
	if (!Parent->AddChild(NewSelected)) return;
	Copy.release();
	Select(NewSelected);
	MarkDirty();
}

void FWidgetEditorPanel::Select(UWidget* Widget)
{
	Selected = Widget;
	if (!Widget) return;
	std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", Widget->GetWidgetName().c_str());
	ValueBuffer[0] = FontBuffer[0] = '\0';
	if (UImageWidget* Image = Cast<UImageWidget>(Widget))
		std::snprintf(ValueBuffer, sizeof(ValueBuffer), "%s", Image->GetTexturePath().c_str());
	else if (UTextWidget* Text = Cast<UTextWidget>(Widget))
	{
		std::snprintf(ValueBuffer, sizeof(ValueBuffer), "%s", Text->GetText().c_str());
		std::snprintf(FontBuffer, sizeof(FontBuffer), "%s", Text->GetFontPath().c_str());
	}
	else if (UButtonWidget* Button = Cast<UButtonWidget>(Widget))
	{
		std::snprintf(ValueBuffer, sizeof(ValueBuffer), "%s", Button->GetText().c_str());
		std::snprintf(FontBuffer, sizeof(FontBuffer), "%s", Button->GetFontPath().c_str());
	}
}

void FWidgetEditorPanel::AddWidget(int Type)
{
	if (!Root) return;
	UCanvasWidget* Parent = Selected ? Cast<UCanvasWidget>(Selected) : nullptr;
	if (!Parent && Selected) Parent = Selected->GetParent();
	if (!Parent) Parent = Root.get();
	UWidget* Widget = Type == 0 ? static_cast<UWidget*>(NewObject<UCanvasWidget>())
		: Type == 1 ? static_cast<UWidget*>(NewObject<UImageWidget>())
		: Type == 2 ? static_cast<UWidget*>(NewObject<UTextWidget>())
		: Type == 3 ? static_cast<UWidget*>(NewObject<UButtonWidget>())
		: Type == 4 ? static_cast<UWidget*>(NewObject<UTimerWidget>())
		: static_cast<UWidget*>(NewObject<UMinimapWidget>());
	const FString Name = (Type == 0 ? "Canvas" : Type == 1 ? "Image" : Type == 2 ? "Text" : Type == 3 ? "Button" : Type == 4 ? "Timer" : "Minimap") + std::to_string(NewWidgetIndex++);
	Widget->SetWidgetName(Name);
	Widget->SetPosition({20.0f, 20.0f});
	Widget->SetSize(Type == 0 ? FVector2{240.0f, 120.0f} : Type == 5 ? FVector2{192.0f, 192.0f} : FVector2{160.0f, 48.0f});
	if (UImageWidget* Image = Cast<UImageWidget>(Widget))
		Image->SetTexturePath("Assets/Icons/Toolbar/Play.png");
	if (UTextWidget* Text = Cast<UTextWidget>(Widget))
	{
		if (!Cast<UTimerWidget>(Widget)) Text->SetText("Text");
		Text->SetFontPath("Assets/Fonts/Pretendard.json");
		Text->SetFontSize(24.0f);
	}
	if (UButtonWidget* Button = Cast<UButtonWidget>(Widget)) Button->SetText("Button");
	if (!Parent->AddChild(Widget)) { delete Widget; return; }
	Select(Widget);
	MarkDirty();
}

void FWidgetEditorPanel::DeleteSelected()
{
	if (!Selected || Selected == Root.get()) return;
	UCanvasWidget* Parent = Selected->GetParent();
	if (!Parent || !Parent->RemoveChild(Selected)) return;
	delete Selected;
	Select(Parent);
	MarkDirty();
}

void FWidgetEditorPanel::DrawHierarchy(UWidget* Widget)
{
	if (!Widget) return;
	UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget);
	const bool HasChildren = Canvas && !Canvas->GetChildren().IsEmpty();
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
		(Widget == Selected ? ImGuiTreeNodeFlags_Selected : 0) |
		(HasChildren ? 0 : ImGuiTreeNodeFlags_Leaf);
	const FString Label = Widget->GetWidgetName().empty() ? Widget->GetClass()->Name : Widget->GetWidgetName();
	const bool Open = ImGui::TreeNodeEx(Widget, Flags, "%s (%s)", Label.c_str(), Widget->GetClass()->Name.c_str());
	if (ImGui::IsItemClicked()) Select(Widget);
	if (Widget != Root.get() && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("UI_WIDGET", &Widget, sizeof(Widget));
		ImGui::TextUnformatted(Label.c_str());
		ImGui::EndDragDropSource();
	}
	if (Canvas && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("UI_WIDGET"))
		{
			UWidget* Dragged = *static_cast<UWidget* const*>(Payload->Data);
			bool bCycle = false;
			for (const UWidget* Ancestor = Canvas; Ancestor; Ancestor = Ancestor->GetParent())
				if (Ancestor == Dragged) { bCycle = true; break; }
			if (Dragged && Dragged->GetParent() && Dragged->GetParent() != Canvas && !bCycle)
			{ PendingDragged = Dragged; PendingParent = Canvas; }
		}
		ImGui::EndDragDropTarget();
	}
	if (Open)
	{
		if (Canvas)
			for (UWidget* Child : Canvas->GetChildren()) DrawHierarchy(Child);
		ImGui::TreePop();
	}
}

void FWidgetEditorPanel::ApplyReparent()
{
	UWidget* Dragged = PendingDragged;
	UCanvasWidget* Parent = PendingParent;
	PendingDragged = nullptr;
	PendingParent = nullptr;
	if (!Dragged || !Parent || !Dragged->GetParent() || !Root) return;
	Root->Layout({0, 0, PreviewWidth, PreviewHeight});
	const FRect OldRect = Dragged->GetLayoutRect();
	const FRect ParentRect = Parent->GetLayoutRect();
	UCanvasWidget* OldParent = Dragged->GetParent();
	if (!OldParent->RemoveChild(Dragged)) return;
	if (!Parent->AddChild(Dragged)) { OldParent->AddChild(Dragged); return; }
	const FVector2 Anchor = Dragged->GetAnchor();
	const FVector2 Pivot = Dragged->GetPivot();
	const FVector2 Size = Dragged->GetSize();
	Dragged->SetPosition({OldRect.X - ParentRect.X - ParentRect.Width * Anchor.X + Size.X * Pivot.X,
		OldRect.Y - ParentRect.Y - ParentRect.Height * Anchor.Y + Size.Y * Pivot.Y});
	MarkDirty();
}

UWidget* FWidgetEditorPanel::HitTest(UWidget* Widget, ImVec2 Point) const
{
	if (!Widget || !Widget->IsVisible()) return nullptr;
	if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
	{
		TArray<UWidget*> Ordered = Canvas->GetChildrenInPaintOrder();
		for (int32 Index = static_cast<int32>(Ordered.Num()) - 1; Index >= 0; --Index)
			if (UWidget* Hit = HitTest(Ordered[Index], Point)) return Hit;
	}
	return Contains(Widget->GetLayoutRect(), Point) ? Widget : nullptr;
}

void FWidgetEditorPanel::DrawPreview(UWidget* Widget, ImDrawList* DrawList, ImVec2 Origin, float Scale)
{
	if (!Widget || !Widget->IsVisible()) return;
	const FRect& Rect = Widget->GetLayoutRect();
	const ImVec2 Min{Origin.x + Rect.X * Scale, Origin.y + Rect.Y * Scale};
	const ImVec2 Max{Min.x + Rect.Width * Scale, Min.y + Rect.Height * Scale};
	if (Cast<UMinimapWidget>(Widget))
	{
		const ImVec2 Center{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f};
		const float Radius = std::min(Max.x - Min.x, Max.y - Min.y) * 0.5f;
		DrawList->AddCircleFilled(Center, Radius, IM_COL32(20, 23, 29, 255));
		DrawList->AddCircle(Center, Radius, IM_COL32(90, 100, 112, 255));
		DrawList->AddCircleFilled(Center, std::max(2.0f, 3.0f * Scale), IM_COL32(64, 220, 255, 255));
	}
	else if (UImageWidget* Image = Cast<UImageWidget>(Widget))
	{
		UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Image->GetTexturePath());
		if (Texture && Texture->GetResource() && Texture->GetResource()->GetSRV())
			DrawList->AddImage((ImTextureID)Texture->GetResource()->GetSRV(), Min, Max,
				{0, 0}, {1, 1}, ToColor(Image->GetTint()));
		else
		{
			DrawList->AddRectFilled(Min, Max, IM_COL32(85, 55, 90, 160));
			DrawList->AddText(Min, IM_COL32_WHITE, "Missing image");
		}
	}
	else if (UTextWidget* Text = Cast<UTextWidget>(Widget))
		DrawList->AddText(nullptr, std::max(8.0f, Text->GetFontSize() * Scale), Min,
			ToColor(Text->GetColor()), Text->GetDisplayText().c_str());
	else if (UButtonWidget* Button = Cast<UButtonWidget>(Widget))
	{
		DrawList->AddRectFilled(Min, Max, ToColor(Button->GetBackgroundColor()));
		DrawList->AddText(nullptr, std::max(8.0f, Button->GetFontSize() * Scale),
			{Min.x + 6.0f, Min.y + 4.0f}, ToColor(Button->GetTextColor()), Button->GetText().c_str());
	}
	if (UCanvasWidget* Canvas = Cast<UCanvasWidget>(Widget))
		for (UWidget* Child : Canvas->GetChildrenInPaintOrder()) DrawPreview(Child, DrawList, Origin, Scale);
	if (Widget == Selected && Widget != Root.get())
	{
		DrawList->AddRect(Min, Max, IM_COL32(255, 190, 50, 255), 0.0f, 0, 2.0f);
		DrawList->AddRectFilled({Max.x - 5.0f, Max.y - 5.0f}, {Max.x + 5.0f, Max.y + 5.0f}, IM_COL32(255, 190, 50, 255));
	}
}

void FWidgetEditorPanel::DrawCanvas()
{
	ImGui::BeginChild("Canvas", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const ImVec2 Region{std::max(1.0f, Available.x), std::max(1.0f, Available.y)};
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	const float Scale = std::min({1.0f, (Region.x - 32.0f) / PreviewWidth,
		(Region.y - 32.0f) / PreviewHeight});
	const float SafeScale = std::max(0.05f, Scale);
	const ImVec2 Origin{Start.x + std::max(16.0f, (Region.x - PreviewWidth * SafeScale) * 0.5f),
		Start.y + std::max(16.0f, (Region.y - PreviewHeight * SafeScale) * 0.5f)};
	const ImVec2 End{Origin.x + PreviewWidth * SafeScale, Origin.y + PreviewHeight * SafeScale};
	ImGui::InvisibleButton("##CanvasInput", Region);
	if (Root) Root->Layout({0, 0, PreviewWidth, PreviewHeight});
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, End, IM_COL32(25, 28, 34, 255));
	DrawList->AddRect(Origin, End, IM_COL32(110, 115, 125, 255));
	DrawList->PushClipRect(Origin, End, true);
	if (Root) DrawPreview(Root.get(), DrawList, Origin, SafeScale);
	DrawList->PopClipRect();
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		const ImVec2 Mouse = ImGui::GetIO().MousePos;
		const ImVec2 Local{(Mouse.x - Origin.x) / SafeScale, (Mouse.y - Origin.y) / SafeScale};
		if (Contains({0, 0, PreviewWidth, PreviewHeight}, Local))
		{
			const FRect SelectedRect = Selected ? Selected->GetLayoutRect() : FRect{};
			const ImVec2 Handle{SelectedRect.X + SelectedRect.Width, SelectedRect.Y + SelectedRect.Height};
			bResizing = Selected && Selected != Root.get() &&
				std::abs(Local.x - Handle.x) < 10.0f / SafeScale &&
				std::abs(Local.y - Handle.y) < 10.0f / SafeScale;
			if (!bResizing) Select(HitTest(Root.get(), Local));
		}
	}
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && Selected && Selected != Root.get())
	{
		const ImVec2 Delta = ImGui::GetIO().MouseDelta;
		if (Delta.x != 0.0f || Delta.y != 0.0f)
		{
			if (bResizing)
			{
				const FVector2 Size = Selected->GetSize();
				const FVector2 NewSize{std::max(4.0f, Size.X + Delta.x / SafeScale),
					std::max(4.0f, Size.Y + Delta.y / SafeScale)};
				const FVector2 Position = Selected->GetPosition();
				const FVector2 Pivot = Selected->GetPivot();
				Selected->SetPosition({Position.X + (NewSize.X - Size.X) * Pivot.X,
					Position.Y + (NewSize.Y - Size.Y) * Pivot.Y});
				Selected->SetSize(NewSize);
			}
			else
			{
				const FVector2 Position = Selected->GetPosition();
				Selected->SetPosition({Position.X + Delta.x / SafeScale, Position.Y + Delta.y / SafeScale});
			}
			MarkDirty();
		}
	}
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) bResizing = false;
	ImGui::EndChild();
}

void FWidgetEditorPanel::DrawDetails()
{
	if (!Selected) return;
	ImGui::TextUnformatted(Selected->GetClass()->Name.c_str());
	if (ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer))) { Selected->SetWidgetName(NameBuffer); MarkDirty(); }
	bool bVisible = Selected->IsVisible();
	if (ImGui::Checkbox("Visible", &bVisible)) { Selected->SetVisible(bVisible); MarkDirty(); }
	if (Selected != Root.get())
	{
		float Position[2] = {Selected->GetPosition().X, Selected->GetPosition().Y};
		if (ImGui::DragFloat2("Position", Position)) { Selected->SetPosition({Position[0], Position[1]}); MarkDirty(); }
		float Size[2] = {Selected->GetSize().X, Selected->GetSize().Y};
		if (ImGui::DragFloat2("Size", Size, 1.0f, 0.0f))
		{
			Selected->SetSize({std::max(0.0f, Size[0]), std::max(0.0f, Size[1])}); MarkDirty();
		}
		float Anchor[2] = {Selected->GetAnchor().X, Selected->GetAnchor().Y};
		if (ImGui::SliderFloat2("Anchor", Anchor, 0.0f, 1.0f)) { Selected->SetAnchor({Anchor[0], Anchor[1]}); MarkDirty(); }
		float Pivot[2] = {Selected->GetPivot().X, Selected->GetPivot().Y};
		if (ImGui::SliderFloat2("Pivot", Pivot, 0.0f, 1.0f)) { Selected->SetPivot({Pivot[0], Pivot[1]}); MarkDirty(); }
		int ZOrder = Selected->GetZOrder();
		if (ImGui::DragInt("Z Order", &ZOrder)) { Selected->SetZOrder(ZOrder); MarkDirty(); }
	}
	if (UMinimapWidget* Minimap = Cast<UMinimapWidget>(Selected))
	{
		float Radius = Minimap->GetRadiusTiles();
		if (ImGui::DragFloat("Radius (tiles)", &Radius, 0.25f, 1.0f, 128.0f))
		{
			Minimap->SetRadiusTiles(Radius);
			MarkDirty();
		}
	}
	else if (UImageWidget* Image = Cast<UImageWidget>(Selected))
	{
		const FString CurrentPath = Image->GetTexturePath();
		UTexture2D* CurrentTexture = UAssetManager::GetAssetByPath<UTexture2D>(CurrentPath);
		if (CurrentTexture && CurrentTexture->GetResource() && CurrentTexture->GetResource()->GetSRV())
		{
			ImGui::Image((ImTextureID)CurrentTexture->GetResource()->GetSRV(), {64.0f, 64.0f});
			ImGui::SameLine();
		}
		const FString Preview = CurrentPath.empty() ? "None" : std::filesystem::path(CurrentPath).filename().string();
		if (ImGui::BeginCombo("Texture", Preview.c_str()))
		{
			if (ImGui::Selectable("None", CurrentPath.empty()))
			{
				Image->SetTexturePath("");
				Select(Image);
				MarkDirty();
			}
			for (TObjectIterator<UTexture2D> It{}; It; ++It)
			{
				UTexture2D* Texture = *It;
				if (!Texture || !Texture->GetPath().starts_with("Assets/Icons/") || !Texture->GetResource() ||
					!Texture->GetResource()->GetSRV() ||
					UAssetManager::GetAssetByPath<UTexture2D>(Texture->GetPath()) != Texture) continue;
				const FString Path = Texture->GetPath();
				ImGui::PushID(Texture);
				const bool ChosenByThumbnail = ImGui::ImageButton("##Thumbnail", (ImTextureID)Texture->GetResource()->GetSRV(), {28.0f, 28.0f});
				ImGui::SameLine();
				const bool ChosenByName = ImGui::Selectable(Path.c_str(), Path == CurrentPath, ImGuiSelectableFlags_None, {0.0f, 28.0f});
				if (ChosenByThumbnail || ChosenByName)
				{
					Image->SetTexturePath(Path);
					Select(Image);
					MarkDirty();
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered() && !CurrentPath.empty()) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* Texture = *static_cast<UTexture2D* const*>(Payload->Data);
				if (Texture) { Image->SetTexturePath(Texture->GetPath()); Select(Image); MarkDirty(); }
			}
			ImGui::EndDragDropTarget();
		}
		float Color[4] = {Image->GetTint().R, Image->GetTint().G, Image->GetTint().B, Image->GetTint().A};
		if (ImGui::ColorEdit4("Tint", Color)) { Image->SetTint({Color[0], Color[1], Color[2], Color[3]}); MarkDirty(); }
	}
	else if (UTextWidget* Text = Cast<UTextWidget>(Selected))
	{
		if (UTimerWidget* Timer = Cast<UTimerWidget>(Selected))
		{
			ImGui::TextUnformatted("Preview: 00:00.00");
			bool bAutoStart = Timer->GetAutoStart();
			if (ImGui::Checkbox("Auto Start", &bAutoStart)) { Timer->SetAutoStart(bAutoStart); MarkDirty(); }
		}
		else if (ImGui::InputText("Text", ValueBuffer, sizeof(ValueBuffer))) { Text->SetText(ValueBuffer); MarkDirty(); }
		if (ImGui::InputText("Font", FontBuffer, sizeof(FontBuffer))) { Text->SetFontPath(FontBuffer); MarkDirty(); }
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Font))
			{
				UFont* Font = *static_cast<UFont* const*>(Payload->Data);
				if (Font) { Text->SetFontPath(Font->GetPath()); Select(Text); MarkDirty(); }
			}
			ImGui::EndDragDropTarget();
		}
		float FontSize = Text->GetFontSize();
		if (ImGui::DragFloat("Font Size", &FontSize, 1.0f, 1.0f, 256.0f))
		{ Text->SetFontSize(std::max(1.0f, FontSize)); MarkDirty(); }
		float Color[4] = {Text->GetColor().R, Text->GetColor().G, Text->GetColor().B, Text->GetColor().A};
		if (ImGui::ColorEdit4("Color", Color)) { Text->SetColor({Color[0], Color[1], Color[2], Color[3]}); MarkDirty(); }
	}
	else if (UButtonWidget* Button = Cast<UButtonWidget>(Selected))
	{
		if (ImGui::InputText("Text", ValueBuffer, sizeof(ValueBuffer))) { Button->SetText(ValueBuffer); MarkDirty(); }
		if (ImGui::InputText("Font", FontBuffer, sizeof(FontBuffer))) { Button->SetFontPath(FontBuffer); MarkDirty(); }
		float FontSize = Button->GetFontSize();
		if (ImGui::DragFloat("Font Size", &FontSize, 1.0f, 1.0f, 256.0f))
		{ Button->SetFontSize(std::max(1.0f, FontSize)); MarkDirty(); }
		float Background[4] = {Button->GetBackgroundColor().R, Button->GetBackgroundColor().G,
			Button->GetBackgroundColor().B, Button->GetBackgroundColor().A};
		if (ImGui::ColorEdit4("Background", Background))
		{ Button->SetBackgroundColor({Background[0], Background[1], Background[2], Background[3]}); MarkDirty(); }
		float TextColor[4] = {Button->GetTextColor().R, Button->GetTextColor().G,
			Button->GetTextColor().B, Button->GetTextColor().A};
		if (ImGui::ColorEdit4("Text Color", TextColor))
		{ Button->SetTextColor({TextColor[0], TextColor[1], TextColor[2], TextColor[3]}); MarkDirty(); }
	}
}

void FWidgetEditorPanel::OnRender()
{
	if (!Root) NewAsset();
	if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{ bHistoryTransactionOpen = false; LastHistoryActiveID = 0; }
	bool bOpen = IsOpen();
	ImGui::SetNextWindowSize({1200, 760}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("UI Editor", &bOpen)) { SetOpen(bOpen); ImGui::End(); return; }
	SetOpen(bOpen);
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
		if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
		if (ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelected();
	}
	if (ImGui::Button("New")) RequestNewAsset();
	ImGui::SameLine();
	if (ImGui::Button("Open")) ImGui::OpenPopup("Open UI asset");
	ImGui::SameLine();
	if (ImGui::Button("Save")) SaveAsset();
	ImGui::SameLine();
	ImGui::BeginDisabled(HistoryIndex == 0);
	if (ImGui::Button("Undo")) Undo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(HistoryIndex + 1 >= History.size());
	if (ImGui::Button("Redo")) Redo();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted(bDirty ? "Unsaved changes" : std::filesystem::exists(AssetPath) ? "Saved" : "New asset");
	ImGui::SetNextItemWidth(-1);
	ImGui::InputText("##AssetPath", PathBuffer, sizeof(PathBuffer));
	if (!ActionStatus.empty()) ImGui::TextWrapped("%s", ActionStatus.c_str());
	if (ImGui::BeginTable("##WidgetEditor", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
		{0, 0}))
	{
		ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 240.0f);
		ImGui::TableSetupColumn("Canvas", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 300.0f);
		ImGui::TableNextColumn();
		ImGui::PushID("HierarchyControls");
		ImGui::TextUnformatted("Add Widget");
		if (ImGui::Button("Canvas")) AddWidget(0);
		ImGui::SameLine();
		if (ImGui::Button("Image")) AddWidget(1);
		ImGui::SameLine();
		if (ImGui::Button("Text")) AddWidget(2);
		ImGui::SameLine();
		if (ImGui::Button("Button")) AddWidget(3);
		ImGui::SameLine();
		if (ImGui::Button("Timer")) AddWidget(4);
		ImGui::SameLine();
		if (ImGui::Button("Minimap")) AddWidget(5);
		if (ImGui::Button("Delete Selected")) DeleteSelected();
		ImGui::SameLine();
		if (ImGui::Button("Duplicate")) DuplicateSelected();
		ImGui::SeparatorText("Hierarchy");
		if (Root) DrawHierarchy(Root.get());
		ApplyReparent();
		ImGui::PopID();
		ImGui::TableNextColumn();
		ImGui::PushID("PreviewControls");
		ImGui::TextUnformatted("Preview Size (W / H)");
		ImGui::SetNextItemWidth(std::min(300.0f, ImGui::GetContentRegionAvail().x));
		float Dimensions[2] = {PreviewWidth, PreviewHeight};
		if (ImGui::DragFloat2("##PreviewSize", Dimensions, 1.0f, 0.0f, 0.0f, "%.0f"))
		{ PreviewWidth = std::clamp(Dimensions[0], 320.0f, 3840.0f);
		  PreviewHeight = std::clamp(Dimensions[1], 240.0f, 2160.0f); }
		DrawCanvas();
		ImGui::PopID();
		ImGui::TableNextColumn();
		ImGui::PushID("WidgetDetails");
		ImGui::SeparatorText("Details");
		DrawDetails();
		ImGui::PopID();
		ImGui::EndTable();
	}
	if (ImGui::BeginPopupModal("Open UI asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		std::vector<FString> Paths;
		std::error_code Error;
		std::filesystem::directory_iterator It("Assets/UI", Error);
		const std::filesystem::directory_iterator End;
		for (; !Error && It != End; It.increment(Error))
			if (It->is_regular_file(Error) && It->path().generic_string().ends_with(".ui.json"))
				Paths.push_back(It->path().generic_string());
		std::sort(Paths.begin(), Paths.end());
		if (Paths.empty()) ImGui::TextUnformatted("No .ui.json files in Assets/UI");
		for (const FString& Path : Paths)
			if (ImGui::Selectable(std::filesystem::path(Path).filename().string().c_str()))
			{
				if (!RequestOpenAsset(Path)) HTR_LOG(Error, "Failed to open UI asset: {}", Path);
				ImGui::CloseCurrentPopup();
			}
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if (PendingAction != EPendingAction::None) ImGui::OpenPopup("Unsaved UI changes");
	if (ImGui::BeginPopupModal("Unsaved UI changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Save changes before switching UI assets?");
		if (ImGui::Button("Save and Continue"))
		{
			SaveAsset();
			if (!bDirty) { ApplyPendingAction(); ImGui::CloseCurrentPopup(); }
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard")) { ApplyPendingAction(); ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { PendingAction = EPendingAction::None; PendingPath.clear(); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	ImGui::End();
}
