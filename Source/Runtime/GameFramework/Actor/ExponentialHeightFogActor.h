#pragma once

#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LuaScriptComponent.h"
#include "Lua/LuaMacros.h"

class AExponentialHeightFogActor final : public AActor
{
	DECLARE_CLASS(AExponentialHeightFogActor, AActor)

	LUA_BIND_START(AExponentialHeightFogActor)
		LUA_FUNC(GetExponentialHeightFogComponent)
		UserType["GetFogComponent"] = &AExponentialHeightFogActor::GetExponentialHeightFogComponent;
		LUA_FUNC(GetLuaScriptComponent)
		sol::table ActorTable = lua["AActor"];
		if (ActorTable.valid())
		{
			ActorTable["GetExponentialHeightFogComponent"] = [](AActor& Self) -> UExponentialHeightFogComponent* {
				if (AExponentialHeightFogActor* FogActor = Cast<AExponentialHeightFogActor>(&Self))
					return FogActor->GetExponentialHeightFogComponent();
				return Cast<UExponentialHeightFogComponent>(Self.GetComponentByClass(UExponentialHeightFogComponent::StaticClass()));
			};
			ActorTable["GetFogComponent"] = [](AActor& Self) -> UExponentialHeightFogComponent* {
				if (AExponentialHeightFogActor* FogActor = Cast<AExponentialHeightFogActor>(&Self))
					return FogActor->GetExponentialHeightFogComponent();
				return Cast<UExponentialHeightFogComponent>(Self.GetComponentByClass(UExponentialHeightFogComponent::StaticClass()));
			};
		}
	LUA_BIND_END()

public:
	AExponentialHeightFogActor();
	virtual ~AExponentialHeightFogActor() override = default;

	AExponentialHeightFogActor(const AExponentialHeightFogActor&) = delete;
	AExponentialHeightFogActor operator=(const AExponentialHeightFogActor&) = delete;

	UBillboardComponent* GetBillboardComponent() const { return BillboardComponent; }
	UExponentialHeightFogComponent* GetExponentialHeightFogComponent() const { return ExponentialHeightFogComponent; }
	ULuaScriptComponent* GetLuaScriptComponent() const { return LuaScriptComponent; }

private:
	UExponentialHeightFogComponent* ExponentialHeightFogComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
	ULuaScriptComponent* LuaScriptComponent = nullptr;
};