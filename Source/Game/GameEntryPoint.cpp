#include "EnginePCH.h"
#include "Launch/EntryPoint.h"
#include "GameEngine.h"

namespace
{
	void SetGameContentDirectory()
	{
		if (std::filesystem::exists("Scenes/MainMenu.scene") &&
			std::filesystem::exists("Assets") &&
			std::filesystem::exists("Resources"))
		{
			return;
		}

		wchar_t ModulePath[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, ModulePath, MAX_PATH))
		{
			return;
		}

		for (std::filesystem::path Dir = std::filesystem::path(ModulePath).parent_path();
			!Dir.empty();
			Dir = Dir.parent_path())
		{
			if (std::filesystem::exists(Dir / "Scenes/MainMenu.scene") &&
				std::filesystem::exists(Dir / "Assets") &&
				std::filesystem::exists(Dir / "Resources"))
			{
				std::error_code Error;
				std::filesystem::current_path(Dir, Error);
				return;
			}
			if (Dir == Dir.root_path())
			{
				break;
			}
		}
	}
}

UClass* GetEngineClass()
{
	SetGameContentDirectory();
	return UGameEngine::StaticClass();
}
