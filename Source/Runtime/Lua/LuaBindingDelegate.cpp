#include "EnginePCH.h"
#include "LuaBinding.h"
#include "LuaBindingDelegate.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Collision/HitResult.h"

namespace LuaBindings
{
	void BindDelegates(sol::state& Lua)
	{
		// 1. 기본 시그니처들
		BindMulticastDelegateType<>(Lua, "FMulticastDelegate");
		BindMulticastDelegateType<float>(Lua, "FMulticastDelegate_Float");
		BindMulticastDelegateType<int32>(Lua, "FMulticastDelegate_Int");
		BindMulticastDelegateType<bool>(Lua, "FMulticastDelegate_Bool");
		BindMulticastDelegateType<const FString&>(Lua, "FMulticastDelegate_String");

		// 2. 엔진 오브젝트/피격 관련 시그니처들
		BindMulticastDelegateType<AActor*>(Lua, "FMulticastDelegate_Actor");
		BindMulticastDelegateType<float, AActor*>(Lua, "FMulticastDelegate_Float_Actor");
		BindMulticastDelegateType<UPrimitiveComponent*, UPrimitiveComponent*>(Lua, "FMulticastDelegate_Primitive_Primitive");
		BindMulticastDelegateType<UPrimitiveComponent*, UPrimitiveComponent*, const FHitResult&>(Lua, "FMulticastDelegate_Primitive_Primitive_Hit");
	}
}
