#pragma once
#include "ShapeComponent.h"

class UCapsuleComponent : public UShapeComponent
{
    DECLARE_CLASS(UCapsuleComponent, UShapeComponent)
    REFLECT_START(UCapsuleComponent)
        PROPERTY(CapsuleRadius)
        PROPERTY(CapsuleHalfHeight)
    REFLECT_END()
public:
    bool BuildCollisionShape(FCollisionShape& OutShape) const override;
    float GetUnscaledCapsuleRadius() const { return CapsuleRadius; }
    float GetUnscaledCapsuleHalfHeight() const { return CapsuleHalfHeight; }
    void SetCapsuleSize(float Radius, float HalfHeight, bool bUpdateOverlaps = true) { CapsuleRadius = Radius; CapsuleHalfHeight = HalfHeight; ShapeChanged(bUpdateOverlaps); }
protected:
    void SanitizeDimensions() override;
private:
    float CapsuleRadius = 0.5f;
    float CapsuleHalfHeight = 1.f;
};
