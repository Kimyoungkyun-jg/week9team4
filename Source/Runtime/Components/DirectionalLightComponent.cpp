#include "EnginePCH.h"
#include "DirectionalLightComponent.h"

#include "Engine/LightSceneProxy.h"

UDirectionalLightComponent::UDirectionalLightComponent()
{

}

UDirectionalLightComponent::~UDirectionalLightComponent()
{
}

void UDirectionalLightComponent::SetAngularRadiusDegrees(float InDegrees)
{
    AngularRadiusDegrees = FMath::Clamp(InDegrees, 0.0f, 10.0f);
}

void UDirectionalLightComponent::PostEditChangeProperty(const FProperty& Property)
{
    if (Property.Name == "AngularRadiusDegrees")
        SetAngularRadiusDegrees(AngularRadiusDegrees);
    Super::PostEditChangeProperty(Property);
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy() const
{
    return new FDirectionalLightSceneProxy(this);
}
