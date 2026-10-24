#include "EnginePCH.h"
#include "LuaManager.h"

#include "LuaMacros.h"
#include "Math/Vector.h"

void FLuaManager::Init()
{
	// Lua 기본 표준 라이브러리 오픈
	Lua.open_libraries(
		sol::lib::base,
		sol::lib::package,
		sol::lib::math, 
		sol::lib::string, 
		sol::lib::table);

	// 기본 수학 구조체 (FVector, FMatrix), FString, FMultiplecastDelegate 등 바인딩
	// 추후 LuaBindingMathcpp, LuaBindingEngine.cpp 형태로 분리
	Lua.new_usertype<FVector>("FVector",
		sol::constructors<FVector(), FVector(float, float, float)>(),
		"X", &FVector::X,
		"Y", &FVector::Y,
		"Z", &FVector::Z
	);

	// 매크로로 등록된 C++ 클래스를 자동으로 바인딩
	for (auto& BindFunc : GetLuaAutoRegistry())
	{
		BindFunc(Lua);
	}
}

void FLuaManager::Shutdown()
{
	// 추후 전역 콜백 해제
}
