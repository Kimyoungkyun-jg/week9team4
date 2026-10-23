#pragma once

#include "ActorComponent.h"
#include "Core/EngineLog.h"
#include <sol/sol.hpp>
#include <utility>

struct FScriptRuntime;

class UScriptComponent : public UActorComponent
{
	DECLARE_CLASS(UScriptComponent, UActorComponent)
	REFLECT_START(ClassName)
		PROPERTY(ScriptPath)
	REFLECT_END()

public:
	UScriptComponent();
	~UScriptComponent() override;

	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;
	void EndPlay() override;

	void SetScriptPath(FString InPath) { ScriptPath = std::move(InPath); }
	const FString& GetScriptPath() const { return ScriptPath; }

	template <typename... Args>
	void CallEvent(const char* Name, Args&&... Arguments)
	{
		sol::protected_function Function = FindScriptFunction(Name);
		CallScript(ScriptPath, Name, Function, std::forward<Args>(Arguments)...);
	}

private:
	sol::protected_function FindScriptFunction(const char* Name);

	template <typename... Args>
	static void CallScript(const FString& Path, const char* Name, sol::protected_function& Function, Args&&... Arguments)
	{
		if (!Function.valid()) return;
		sol::protected_function_result Result = Function(std::forward<Args>(Arguments)...);
		if (!Result.valid())
		{
			sol::error Error = Result;
			HTR_LOG(Error, "Lua {} ({}): {}", Name, Path, Error.what());
		}
	}

	FString ScriptPath;
	TUniquePtr<FScriptRuntime> Runtime;
};
