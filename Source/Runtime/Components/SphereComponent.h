#pragma once
#include "ShapeComponent.h"

class USphereComponent : public UShapeComponent
{
    DECLARE_CLASS(USphereComponent, UShapeComponent)
    REFLECT_START(USphereComponent)
        PROPERTY(SphereRadius)
    REFLECT_END()
public:
    bool BuildCollisionShape(FCollisionShape& OutShape) const override;
    float GetUnscaledSphereRadius() const { return SphereRadius; }
    void SetSphereRadius(float Radius) { SphereRadius = Radius; ShapeChanged(); }
protected:
    void SanitizeDimensions() override;
private:
    float SphereRadius = 0.5f;
};
