#pragma once
#include <sol/sol.hpp>
#include "Core/Delegates/Delegate.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"

inline const void* GetLuaDelegateOwnerPointer(const sol::object& OwnerObj, const void* FallbackPtr = nullptr)
{
	if (!OwnerObj.valid() || OwnerObj.is<sol::nil_t>())
	{
		return FallbackPtr;
	}

	// 1. UObject 기반 클래스 (AActor, UActorComponent 등)의 실제 C++ 인스턴스 포인터 추출
	if (OwnerObj.is<UObject*>())
	{
		return static_cast<const void*>(OwnerObj.as<UObject*>());
	}

	// 2. 일반 C++ 포인터
	if (OwnerObj.is<void*>())
	{
		return OwnerObj.as<void*>();
	}

	// 3. Fallback: 순수 Lua 테이블 등인 경우 Lua 포인터
	return OwnerObj.pointer();
}

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
				const void* OwnerPtr = OwnerObj.valid() ? GetLuaDelegateOwnerPointer(OwnerObj, &Self) : &Self;

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
				const void* OwnerPtr = nullptr;
				sol::object ActorField = SelfTable["Actor"];
				if (ActorField.valid() && !ActorField.is<sol::nil_t>())
				{
					OwnerPtr = GetLuaDelegateOwnerPointer(ActorField);
				}
				else
				{
					sol::object OwnerField = SelfTable["Owner"];
					if (OwnerField.valid() && !OwnerField.is<sol::nil_t>())
					{
						OwnerPtr = GetLuaDelegateOwnerPointer(OwnerField);
					}
				}
				if (!OwnerPtr)
				{
					OwnerPtr = SelfTable.pointer();
				}

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
			const void* OwnerPtr = GetLuaDelegateOwnerPointer(OwnerObj);
			return OwnerPtr ? Self.RemoveAll(OwnerPtr) : 0;
		},
		"Broadcast", &DelegateType::Broadcast
	);
}
