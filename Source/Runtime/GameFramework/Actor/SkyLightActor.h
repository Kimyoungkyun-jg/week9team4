#pragma once

#include "GameFramework/Actor/LightActorBase.h"
#include "Components/SkyLightComponent.h"

class ASkyLightActor : public ALightActorBase
{
	DECLARE_CLASS(ASkyLightActor, ALightActorBase)

	REFLECT_START(ClassName)
	REFLECT_END()
	LUA_BIND_START(ASkyLightActor)
		UserType["Cast"] = [](AActor* Actor) { return Cast<ASkyLightActor>(Actor); };
		LUA_FUNC(GetSkyLightComponent)
	LUA_BIND_END()

public:
	ASkyLightActor();
	virtual ~ASkyLightActor() override = default;

    virtual ULightComponent* GetLightComponent() const { return SkyLightComponent; }
    USkyLightComponent* GetSkyLightComponent() const { return SkyLightComponent; }

private:
    USkyLightComponent* SkyLightComponent = nullptr;

};
