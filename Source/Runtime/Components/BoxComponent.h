#pragma once
#include "ShapeComponent.h"

class UBoxComponent : public UShapeComponent
{
    DECLARE_CLASS(UBoxComponent, UShapeComponent)
    REFLECT_START(UBoxComponent)
        PROPERTY(BoxExtent)
    REFLECT_END()
public:
    bool BuildCollisionShape(FCollisionShape& OutShape) const override;
    const FVector& GetUnscaledBoxExtent() const { return BoxExtent; }
    void SetBoxExtent(const FVector& Extent) { BoxExtent = Extent; ShapeChanged(); }
protected:
    void SanitizeDimensions() override;
private:
    FVector BoxExtent = FVector(0.5f);
};
