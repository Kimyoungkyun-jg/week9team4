#pragma once
#include "PrimitiveComponent.h"
class FLineBatcher;

class UShapeComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)
public:
    UShapeComponent();
    bool BuildCollisionShape(FCollisionShape& OutShape) const override = 0;
    FBox CalcLocalBounds() const override;
    bool LineTraceComponentLocal(const FRay& Ray, float& OutT) override;
    bool LineTraceComponent(const FRay& Ray, FHitResult& OutHit) override;
    void PostEditChangeProperty(const FProperty& Property) override;
    void Serialize(FStructuredArchive::FRecord Record) override;
protected:
    virtual void SanitizeDimensions() {}
    void ShapeChanged();
};
