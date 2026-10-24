#pragma once

#include "Core/Types.h"
#include <sol.hpp>

class FLuaManager
{
public:
	FLuaManager() = default;
	~FLuaManager() = default;
	FLuaManager(const FLuaManager&) = delete;
	FLuaManager& operator=(const FLuaManager&) = delete;
	void Init();
	void Shutdown();

	sol::state& GetState() { return Lua; }
private:
	sol::state Lua;
};