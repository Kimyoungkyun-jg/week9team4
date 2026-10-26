#pragma once
#include <sol/sol.hpp>

class UGameInstance;

namespace LuaBindings
{
	void BindMath(sol::state& Lua);
	void BindCore(sol::state& Lua, UGameInstance& GameInstance);
	void BindInput(sol::state& Lua);
	void BindDelegates(sol::state& Lua);
}
