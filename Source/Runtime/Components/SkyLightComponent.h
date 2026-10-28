#pragma once

#include "LightComponent.h"
#include "Lua/LuaMacros.h"

class USkyLightComponent : public ULightComponent
{
	DECLARE_CLASS(USkyLightComponent, ULightComponent)
	REFLECT_START(USkyLightComponent)
	REFLECT_END()
	LUA_BIND_START(USkyLightComponent)
		UserType["GetIntensity"] = [](const USkyLightComponent& Self) { return Self.GetIntensity(); };
		UserType["SetIntensity"] = [](USkyLightComponent& Self, float Intensity) { Self.SetIntensity(Intensity); };
	LUA_BIND_END()
public:
	USkyLightComponent();
	virtual ~USkyLightComponent() override;

	ELightType GetLightType() const override { return ELightType::Sky; }
	
protected:
private:
};
