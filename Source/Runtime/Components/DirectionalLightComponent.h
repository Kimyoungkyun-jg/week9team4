#pragma once

#include "LightComponent.h"

class UDirectionalLightComponent :public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(AngularRadiusDegrees)
	REFLECT_END()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }
	float GetAngularRadiusDegrees() const { return AngularRadiusDegrees; }
	void SetAngularRadiusDegrees(float InDegrees);
	void PostEditChangeProperty(const FProperty& Property) override;


private:
	float AngularRadiusDegrees = 3.0f;
};
