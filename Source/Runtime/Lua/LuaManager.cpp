#include "EnginePCH.h"
#include "LuaManager.h"

#include "LuaMacros.h"
#include "Math/Vector.h"
#include "Engine/GameInstance.h"
#include "Collision/HitResult.h"
#include "UObject/Object.h"

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

	Lua.set_function("Log", [](const FString& Message) { HTR_LOG(Info, "[Lua] {}", Message); });
	Lua.set_function("EndGame", [&GameInstance]() { GameInstance.RequestEndSession(); });
	Lua.new_usertype<UObject>("UObject", sol::no_constructor,
		"UUID", &UObject::GetUUID,
		"GetName", &UObject::GetName);
	Lua.new_usertype<FHitResult>("FHitResult", sol::no_constructor,
		"bBlockingHit", &FHitResult::bBlockingHit,
		"Distance", &FHitResult::Distance,
		"ImpactPoint", &FHitResult::ImpactPoint,
		"ImpactNormal", &FHitResult::ImpactNormal);

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
