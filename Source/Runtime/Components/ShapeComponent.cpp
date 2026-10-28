#include "EnginePCH.h"
#include "ShapeComponent.h"
#include "BoxComponent.h"
#include "SphereComponent.h"
#include "CapsuleComponent.h"
#include "Engine/World.h"
#include "Collision/Ray.h"

namespace
{
float Dimension(float Value) { return std::isfinite(Value) ? std::max(0.f, Value) : 0.f; }
}

UShapeComponent::UShapeComponent()
{
    CollisionSettings.bEnabled = true;
    bCastShadow = bReceiveShadow = false;
}
void UShapeComponent::ShapeChanged(bool bUpdateOverlaps)
{
    if (!IsValid(this) || IsBeingDestroyed()) return;
    SanitizeDimensions();
    UpdateCollisionShape(bUpdateOverlaps);
}
void UShapeComponent::PostEditChangeProperty(const FProperty& Property)
{
    Super::PostEditChangeProperty(Property);
    ShapeChanged();
}
void UShapeComponent::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);
    if (Record.GetUnderlyingArchive().IsLoading()) ShapeChanged();
}
FBox UShapeComponent::CalcLocalBounds() const
{
    FCollisionShape Shape;
    return BuildCollisionShape(Shape) ? MakeCollisionGeometry(Shape, FTransform::Identity).GetBounds() : Super::CalcLocalBounds();
}
bool UShapeComponent::LineTraceComponentLocal(const FRay& Ray, float& OutT)
{
    FCollisionShape Shape;
    if (!BuildCollisionShape(Shape)) return false;
    const auto Point = MakeCollisionGeometry(FCollisionShape::MakeSphere(0), FTransform(FRotator(), Ray.Origin));
    FHitResult Hit;
    if (!SweepCollisionGeometry(Point, Ray.Direction * 1.e6f, MakeCollisionGeometry(Shape, FTransform::Identity), Hit)) return false;
    OutT = Hit.Time * 1.e6f;
    return true;
}
bool UShapeComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHit)
{
    FCollisionGeometry Shape;
    if (!GetCollisionGeometry(Shape)) return false;
    const auto Point = MakeCollisionGeometry(FCollisionShape::MakeSphere(0), FTransform(FRotator(), Ray.Origin));
    if (!SweepCollisionGeometry(Point, Ray.Direction * 1.e6f, Shape, OutHit)) return false;
    OutHit.HitComponent = this;
    return true;
}
bool UBoxComponent::BuildCollisionShape(FCollisionShape& OutShape) const
{ OutShape = FCollisionShape::MakeBox(BoxExtent); return true; }
void UBoxComponent::SanitizeDimensions()
{ for (int I = 0; I < 3; ++I) BoxExtent[I] = Dimension(BoxExtent[I]); }
bool USphereComponent::BuildCollisionShape(FCollisionShape& OutShape) const
{ OutShape = FCollisionShape::MakeSphere(SphereRadius); return true; }
void USphereComponent::SanitizeDimensions() { SphereRadius = Dimension(SphereRadius); }
bool UCapsuleComponent::BuildCollisionShape(FCollisionShape& OutShape) const
{ OutShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight); return true; }
void UCapsuleComponent::SanitizeDimensions()
{ CapsuleRadius = Dimension(CapsuleRadius); CapsuleHalfHeight = std::max(CapsuleRadius, Dimension(CapsuleHalfHeight)); }

