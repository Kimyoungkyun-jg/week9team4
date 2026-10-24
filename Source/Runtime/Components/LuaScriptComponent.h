#pragma once

#include "ActorComponent.h"
#include "Lua/LuaMacros.h"

class ULuaScriptComponent : public UActorComponent
{
	DECLARE_CLASS(ULuaScriptComponent, UActorComponent)

	REFLECT_START(ULuaScriptComponent)
		PROPERTY(LuaScriptPath);
	REFLECT_END()
public:
	ULuaScriptComponent();
	~ULuaScriptComponent() = default;

	const FString& GetScriptPath() const { return LuaScriptPath; }
	void SetScriptPath(FString InScriptPath) { LuaScriptPath = InScriptPath; }

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
private:
	FString LuaScriptPath;
	sol::table LuaSelfTable;

	sol::protected_function LuaBeginPlayFunc;
	sol::protected_function LuaTickFunc;


};