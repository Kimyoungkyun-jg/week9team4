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

	if (LuaScriptPath.empty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance) return;

	FLuaManager* LuaManager = GameInstance->GetLuaManager();
	if (!LuaManager) return;

	namespace fs = std::filesystem;
	fs::path FilePath(LuaScriptPath.c_str());
	if (!fs::exists(FilePath))
	{
		FilePath = fs::absolute(FilePath);
		if (!fs::exists(FilePath))
		{
			HTR_LOG(Error, "[Lua] Script file not found: {}", LuaScriptPath);
			return;
		}
	}

	sol::state& Lua = LuaManager->GetState();

	// 스크립트 파일 실행 및 클래스 테이블 로드
	sol::protected_function_result ScriptResult = Lua.safe_script_file(FilePath.generic_string());
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	if (!ScriptResult.valid())
	{
		sol::error Err = ScriptResult;
		HTR_LOG(Error, "[Lua] Failed to load script '{}': {}", LuaScriptPath, Err.what());
		return;
	}

	sol::object ReturnObj = ScriptResult;
	if (!ReturnObj.is<sol::table>())
	{
		HTR_LOG(Error, "[Lua] Script '{}' must return a table.", LuaScriptPath);
		return;
	}

	sol::table ClassTable = ReturnObj.as<sol::table>();

	// 컴포넌트 전용 인스턴스 테이블 생성
	LuaSelfTable = Lua.create_table();
	LuaSelfTable["Owner"] = GetOwner();

	// 메타테이블 연결 (ClassTable 함수 탐색용)
	sol::table MetaTable = Lua.create_table();
	MetaTable[sol::meta_function::index] = ClassTable;
	LuaSelfTable[sol::metatable_key] = MetaTable;

	LuaBeginPlayFunc = LuaSelfTable["BeginPlay"];
	LuaTickFunc = LuaSelfTable["Tick"];

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

void ULuaScriptComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;

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
	LuaBeginPlayFunc = {};
	LuaTickFunc = {};
	LuaSelfTable = {};
	Super::EndPlay();
}
