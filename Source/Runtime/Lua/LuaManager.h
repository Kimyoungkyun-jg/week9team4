#pragma once

#include "Core/Types.h"
#include "Core/EngineLog.h"
#include <sol/sol.hpp>
#include <utility>

class UGameInstance;

class FLuaManager
{
public:
	FLuaManager() = default;
	~FLuaManager() = default;
	FLuaManager(const FLuaManager&) = delete;
	FLuaManager& operator=(const FLuaManager&) = delete;
	void Init(UGameInstance& GameInstance);
	void Shutdown();

	sol::state& GetState() { return Lua; }
	template<typename... TArgs>
	void Call(sol::table Instance, const char* Name, TArgs&&... Args)
	{
		if (!Instance.valid()) return;
		sol::protected_function Function = Instance[Name];
		if (!Function.valid()) return;
		sol::protected_function_result Result = Function(Instance, std::forward<TArgs>(Args)...);
		if (!Result.valid())
		{
			sol::error Error = Result;
			HTR_LOG(Error, "[Lua] {} error: {}", Name, Error.what());
		}
	}
private:
	sol::state Lua;
};
