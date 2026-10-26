#include "EnginePCH.h"
#include "LuaScriptComponent.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Lua/LuaManager.h"

#include <sol/sol.hpp>

#include <filesystem>
#include "Core/EngineLog.h"

ULuaScriptComponent::ULuaScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULuaScriptComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!LoadOrReloadScript(false))
	{
		return;
	}

	std::error_code EC;
	LastWriteTime = std::filesystem::last_write_time(LuaScriptPath.c_str(), EC);
	FileWatchTimer = 0.0f;

	if (LuaBeginPlayFunc.valid())
	{
		const sol::protected_function Function = LuaBeginPlayFunc;
		const sol::table Self = LuaSelfTable;
		sol::protected_function_result CallResult = Function(Self);
		if (!CallResult.valid())
		{
			sol::error Err = CallResult;
			HTR_LOG(Error, "[Lua] BeginPlay error: {}", Err.what());
		}
	}
}

bool ULuaScriptComponent::LoadOrReloadScript(bool bIsReload)
{
	if (LuaScriptPath.empty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World) return false;

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance) return false;

	FLuaManager* LuaManager = GameInstance->GetLuaManager();
	if (!LuaManager) return false;

	namespace fs = std::filesystem;
	fs::path FilePath(LuaScriptPath.c_str());
	if (!fs::exists(FilePath))
	{
		FilePath = fs::absolute(FilePath);
		if (!fs::exists(FilePath))
		{
			HTR_LOG(Error, "[Lua] Script file not found: {}", LuaScriptPath);
			return false;
		}
	}

	sol::state& Lua = LuaManager->GetState();

	// 스크립트 파일 실행 및 클래스 테이블 로드
	sol::protected_function_result ScriptResult = Lua.safe_script_file(FilePath.generic_string());
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return false;
	if (!ScriptResult.valid())
	{
		sol::error Err = ScriptResult;
		HTR_LOG(Error, "[Lua] Failed to load script '{}': {}", LuaScriptPath, Err.what());
		return false;
	}

	sol::object ReturnObj = ScriptResult;
	if (!ReturnObj.is<sol::table>())
	{
		HTR_LOG(Error, "[Lua] Script '{}' must return a table.", LuaScriptPath);
		return false;
	}

	sol::table ClassTable = ReturnObj.as<sol::table>();

	// 컴포넌트 전용 인스턴스 테이블 생성 (최초 실행 시에만 생성, 핫 리로드 시에는 상태 유지)
	if (!bIsReload || !LuaSelfTable.valid())
	{
		LuaSelfTable = Lua.create_table();
		LuaSelfTable["Owner"] = GetOwner();
	}

	// 메타테이블 연결 (ClassTable 함수 탐색용)
	sol::table MetaTable = Lua.create_table();
	MetaTable[sol::meta_function::index] = ClassTable;
	LuaSelfTable[sol::metatable_key] = MetaTable;

	LuaBeginPlayFunc = LuaSelfTable["BeginPlay"];
	LuaTickFunc = LuaSelfTable["Tick"];

	if (bIsReload)
	{
		HTR_LOG(Info, "[Lua] Hot-reloaded script: {}", LuaScriptPath);
		sol::protected_function OnReloadFunc = LuaSelfTable["OnReload"];
		if (OnReloadFunc.valid())
		{
			sol::protected_function_result CallResult = OnReloadFunc(LuaSelfTable);
			if (!CallResult.valid())
			{
				sol::error Err = CallResult;
				HTR_LOG(Error, "[Lua] OnReload error: {}", Err.what());
			}
		}
	}

	return true;
}

void ULuaScriptComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;

	// 0.5초마다 루아 스크립트 파일 변경 여부 확인 (핫 리로드)
	FileWatchTimer += DeltaTime;
	if (FileWatchTimer >= 0.5f)
	{
		FileWatchTimer = 0.0f;
		if (!LuaScriptPath.empty())
		{
			std::error_code EC;
			auto CurrentWriteTime = std::filesystem::last_write_time(LuaScriptPath.c_str(), EC);
			if (!EC && CurrentWriteTime != LastWriteTime)
			{
				LastWriteTime = CurrentWriteTime;
				LoadOrReloadScript(true);
				if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
			}
		}
	}

	if (LuaTickFunc.valid() && LuaSelfTable.valid())
	{
		const sol::protected_function Function = LuaTickFunc;
		const sol::table Self = LuaSelfTable;
		sol::protected_function_result Result = Function(Self, DeltaTime);
		if (!Result.valid())
		{
			sol::error Err = Result;
			HTR_LOG(Error, "[Lua] Tick error: {}", Err.what());
		}
	}
}

void ULuaScriptComponent::EndPlay()
{
	if (LuaSelfTable.valid())
	{
		sol::protected_function LuaEndPlayFunc = LuaSelfTable["EndPlay"];
		if (LuaEndPlayFunc.valid())
		{
			const sol::table Self = LuaSelfTable;
			sol::protected_function_result Result = LuaEndPlayFunc(Self);
			if (!Result.valid())
			{
				sol::error Err = Result;
				HTR_LOG(Error, "[Lua] EndPlay error: {}", Err.what());
			}
		}
	}
	LuaBeginPlayFunc = {};
	LuaTickFunc = {};
	LuaSelfTable = {};
	Super::EndPlay();
}
