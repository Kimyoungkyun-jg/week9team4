#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
 
FString FEditorFileUtils::CurrentScenePath = "";

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
		return false;

	World->ClearWorld();

	// 라인 트래커도 삭제
	World->GetPathTracker().ClearPath();

	CurrentScenePath.clear();

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
		return false;

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);

		return false;
	}

	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
		return false;

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
		return false;

	FilePath = std::filesystem::absolute(FilePath).lexically_normal().string();

	if (!FJsonArchive::LoadWorld(World, FilePath))
		return false;

	// 이제 이 파일이 현재 Scene
	CurrentScenePath = FilePath;

	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::ChooseBoxColliderJsonPath(bool bSave)
{
	wchar_t FilePath[1024] = {};
	OPENFILENAMEW Dialog = {};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = 1024;
	Dialog.lpstrFilter = L"BoxCollider JSON (*.json)\0*.json\0";
	Dialog.lpstrDefExt = L"json";
	Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (bSave ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
	if (!(bSave ? GetSaveFileNameW(&Dialog) : GetOpenFileNameW(&Dialog))) return {};
	const auto Utf8 = std::filesystem::path(FilePath).u8string();
	return FString(Utf8.begin(), Utf8.end());
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	wchar_t FilePath[4096] = {};
	const std::wstring SceneDirectory = std::filesystem::absolute("Scenes").wstring();

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = _countof(FilePath);

	ofn.lpstrFilter = L"Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = L"scene";
	ofn.lpstrInitialDir = SceneDirectory.c_str();

	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_OVERWRITEPROMPT |
		OFN_NOCHANGEDIR;

	if (GetSaveFileNameW(&ofn))
	{
		const auto Utf8 = std::filesystem::path(FilePath).u8string();
		return FString(Utf8.begin(), Utf8.end());
	}

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	wchar_t FilePath[4096] = {};
	const std::wstring SceneDirectory = std::filesystem::absolute("Scenes").wstring();

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = _countof(FilePath);

	ofn.lpstrFilter =
		L"Scene Files (*.scene)\0*.scene\0"
		L"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = L"scene";
	ofn.lpstrInitialDir = SceneDirectory.c_str();

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameW(&ofn))
	{
		const auto Utf8 = std::filesystem::path(FilePath).u8string();
		return FString(Utf8.begin(), Utf8.end());
	}

	return FString();
}


