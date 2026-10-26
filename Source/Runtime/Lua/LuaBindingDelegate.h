#pragma once
#include <sol/sol.hpp>
#include "Core/Delegates/Delegate.h"
#include "Core/EngineLog.h"

inline void BindDelegateHandle(sol::state& Lua)
{
	if (Lua["FDelegateHandle"].valid()) return;

	Lua.new_usertype<FDelegateHandle>("FDelegateHandle",
		sol::constructors<FDelegateHandle()>(),
		"IsValid", &FDelegateHandle::IsValid,
		"Reset", &FDelegateHandle::Reset
	);
}

template<typename... Args>
void BindMulticastDelegateType(sol::state& Lua, const char* TypeName)
{
	BindDelegateHandle(Lua);

	using DelegateType = TMulticastDelegate<Args...>;

	Lua.new_usertype<DelegateType>(TypeName,
		sol::no_constructor,

		"Add", sol::overload(
			// 1. Add(func, [owner])
			[](DelegateType& Self, sol::protected_function LuaFunc, sol::object OwnerObj) -> FDelegateHandle
			{
				if (!LuaFunc.valid()) return {};
				const void* OwnerPtr = OwnerObj.valid() ? OwnerObj.pointer() : &Self;

				return Self.Add([LuaFunc](Args... CallbackArgs) {
					sol::protected_function_result Result = LuaFunc(CallbackArgs...);
					if (!Result.valid())
					{
						sol::error Err = Result;
						HTR_LOG(Error, "[Lua Delegate] Execution error: {}", Err.what());
					}
				}, OwnerPtr);
			},
			// 2. Add(func) - Owner 생략
			[](DelegateType& Self, sol::protected_function LuaFunc) -> FDelegateHandle
			{
				if (!LuaFunc.valid()) return {};
				return Self.Add([LuaFunc](Args... CallbackArgs) {
					sol::protected_function_result Result = LuaFunc(CallbackArgs...);
					if (!Result.valid())
					{
						sol::error Err = Result;
						HTR_LOG(Error, "[Lua Delegate] Execution error: {}", Err.what());
					}
				}, &Self);
			},
			// 3. Add(self, self.Func) - Lua 메서드 스타일
			[](DelegateType& Self, sol::table SelfTable, sol::protected_function LuaFunc) -> FDelegateHandle
			{
				if (!LuaFunc.valid() || !SelfTable.valid()) return {};
				const void* OwnerPtr = SelfTable.pointer();

				return Self.Add([LuaFunc, SelfTable](Args... CallbackArgs) {
					sol::protected_function_result Result = LuaFunc(SelfTable, CallbackArgs...);
					if (!Result.valid())
					{
						sol::error Err = Result;
						HTR_LOG(Error, "[Lua Delegate] Execution error: {}", Err.what());
					}
				}, OwnerPtr);
			}
		),

		"Remove", &DelegateType::Remove,
		"Clear", &DelegateType::Clear,
		"IsBound", &DelegateType::IsBound,
		"RemoveAll", [](DelegateType& Self, sol::object OwnerObj) -> int32 {
			if (!OwnerObj.valid()) return 0;
			return Self.RemoveAll(OwnerObj.pointer());
		},
		"Broadcast", &DelegateType::Broadcast
	);
}
