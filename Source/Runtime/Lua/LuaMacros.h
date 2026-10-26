#pragma once

#include <sol/sol.hpp>
#include <vector>
#include <functional>

using FLuaBindingFunc = std::function<void(sol::state&)>;
inline std::vector<FLuaBindingFunc>& GetLuaAutoRegistry()
{
	static std::vector<FLuaBindingFunc>Registry;
	return Registry;
}

// 클래스 바인딩 시작
#define LUA_BIND_START(ClassName)									\
public:																\
	static void RegisterLua(sol::state& lua)						\
{																	\
	using CurrentClass = ClassName;									\
	auto UserType = lua.new_usertype<CurrentClass>(#ClassName); 
// 멤버 함수 바인딩
#define LUA_FUNC(FuncName)											\
	UserType[#FuncName] = &CurrentClass::FuncName;
// 오버로딩 함수 바인딩
#define LUA_FUNC_OVERLOAD(FuncName, Signature)						\
	UserType[#FuncName] = sol::resolve<Signature>(&CurrentClass::FuncName);
// 멤버 변수 바인딩
#define LUA_PROP(PropName)											\
	UserType[#PropName] = &CurrentClass::PropName;
// Delegate 멤버 변수 바인딩 (Lua에서 Obj.OnEvent:Add(...) 형태로 사용 가능)
#define LUA_DELEGATE(PropName)										\
	UserType[#PropName] = &CurrentClass::PropName;

// 클래스 바인딩 종료 및 전역 대기열에 등록
#define LUA_BIND_END()												\
}																	\
struct FAutoLuaRegister {											\
	FAutoLuaRegister() {											\
		GetLuaAutoRegistry().push_back(&RegisterLua);				\
	}																\
};																	\
inline static FAutoLuaRegister FAutoLuaRegisterInstance;

