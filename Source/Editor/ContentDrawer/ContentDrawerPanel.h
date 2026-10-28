#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	void SetOpenUIAssetCallback(std::function<void(const FString&)> Callback) { OnOpenUIAsset = std::move(Callback); }
	void SetOpenSequenceAssetCallback(std::function<void(const FString&)> Callback) { OnOpenSequenceAsset = std::move(Callback); }

private:
	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";

	const FString FolderIconPath = "Assets/Icons/Folder.png";
	const FString FileIconPath = "Assets/Icons/File.png";
	std::function<void(const FString&)> OnOpenUIAsset;
	std::function<void(const FString&)> OnOpenSequenceAsset;
	
};
