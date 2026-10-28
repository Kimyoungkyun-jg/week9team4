#include "EnginePCH.h"
#include "LuaManager.h"
#include "LuaMacros.h"
#include "LuaBinding.h"
#include "Engine/GameInstance.h"

void FLuaManager::Init(UGameInstance& GameInstance)
{
	// Lua 기본 표준 라이브러리 오픈
	Lua.open_libraries(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math, 
		sol::lib::string, 
		sol::lib::table,
		sol::lib::coroutine);

	// 모듈화된 바인딩 호출
	LuaBindings::BindCore(Lua, GameInstance);
	LuaBindings::BindMath(Lua);
	LuaBindings::BindInput(Lua);
	LuaBindings::BindDelegates(Lua);

	// 매크로로 등록된 C++ 클래스를 자동으로 바인딩
	for (auto& BindFunc : GetLuaAutoRegistry())
	{
		BindFunc(Lua);
	}
}

void FLuaManager::Shutdown()
{
	if (!Lua.lua_state()) return;

	// 1. C++ 참조를 캡처한 전역 바인딩 해제 (댕글링 포인터 방지)
	Lua["EndGame"] = sol::nil;
	Lua["PlayLevelSequence"] = sol::nil;
	Lua["PauseLevelSequence"] = sol::nil;
	Lua["StopLevelSequence"] = sol::nil;
	Lua["Log"] = sol::nil;
	Lua["LogWarning"] = sol::nil;
	Lua["LogError"] = sol::nil;
	Lua["Input"] = sol::nil;

	// 2. 가비지 컬렉터 강제 실행 (남아있는 루아 객체 및 __gc 정리)
	Lua.collect_garbage();

	// 3. 루아 상태 명시적 리셋 (lua_close 호출 효과)
	Lua = sol::state();
}
