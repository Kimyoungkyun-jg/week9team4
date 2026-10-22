#include "EnginePCH.h"
#include "ScriptComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include <sol/sol.hpp>

struct FScriptRuntime
{
	explicit FScriptRuntime(sol::state& Lua) : Environment(Lua, sol::create, Lua.globals()) {}

	sol::environment Environment;
	sol::protected_function BeginPlay;
	sol::protected_function Tick;
	sol::protected_function EndPlay;
};

namespace
{
	sol::protected_function FindCallback(sol::environment& Environment, const char* Name)
	{
		sol::object Value = Environment.raw_get<sol::object>(Name);
		return Value.get_type() == sol::type::function ? Value.as<sol::protected_function>() : sol::protected_function{};
	}

	template <typename... Args>
	void CallScript(const FString& Path, const char* Name, sol::protected_function& Function, Args&&... Arguments)
	{
		if (!Function.valid()) return;
		sol::protected_function_result Result = Function(std::forward<Args>(Arguments)...);
		if (!Result.valid())
		{
			sol::error Error = Result;
			HTR_LOG(Error, "Lua {} ({}): {}", Name, Path, Error.what());
		}
	}
}

UScriptComponent::UScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UScriptComponent::~UScriptComponent() = default;

void UScriptComponent::BeginPlay()
{
	if (ScriptPath.empty()) return;
	UWorld* World = GetWorld();
	FWorldContext* Context = World && GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	sol::state* Lua = Context && Context->OwningGameInstance ? Context->OwningGameInstance->GetLuaState() : nullptr;
	if (!Lua)
	{
		HTR_LOG(Error, "Lua state unavailable for {}", ScriptPath);
		return;
	}

	Runtime = MakeUnique<FScriptRuntime>(*Lua);
	Runtime->Environment["obj"] = GetOwner();
	sol::protected_function_result Result = Lua->safe_script_file(ScriptPath, Runtime->Environment, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Error = Result;
		HTR_LOG(Error, "Lua load ({}): {}", ScriptPath, Error.what());
		Runtime.reset();
		return;
	}

	Runtime->BeginPlay = FindCallback(Runtime->Environment, "BeginPlay");
	Runtime->Tick = FindCallback(Runtime->Environment, "Tick");
	Runtime->EndPlay = FindCallback(Runtime->Environment, "EndPlay");
	CallScript(ScriptPath, "BeginPlay", Runtime->BeginPlay);
}

void UScriptComponent::TickComponent(float DeltaTime)
{
	if (Runtime) CallScript(ScriptPath, "Tick", Runtime->Tick, DeltaTime);
}

void UScriptComponent::EndPlay()
{
	if (!Runtime) return;
	CallScript(ScriptPath, "EndPlay", Runtime->EndPlay);
	Runtime.reset();
}
