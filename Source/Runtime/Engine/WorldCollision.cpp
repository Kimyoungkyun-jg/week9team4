#include "EnginePCH.h"
#include "World.h"
#include "Engine/Level.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Collision/Ray.h"

void UWorld::RegisterCollisionComponent(UPrimitiveComponent* Component)
{
    if (!IsValid(Component) || Component->IsBeingDestroyed()) return;
    if (CollisionComponents.Find(Component) == INDEX_NONE) CollisionComponents.Add(Component);
    bCollisionTopologyDirty = true;
    MarkCollisionDirty(Component);
}

void UWorld::UnregisterCollisionComponent(UPrimitiveComponent* Component)
{
    int32 Index = CollisionComponents.Find(Component);
    if (Index != INDEX_NONE) CollisionComponents.RemoveAtSwap(Index);
    Index = DirtyCollisionComponents.Find(Component);
    if (Index != INDEX_NONE) DirtyCollisionComponents.RemoveAtSwap(Index);
    Component->bCollisionUpdateQueued = false;
    Component->CollisionEntryIndex = INDEX_NONE;
    bCollisionTopologyDirty = true;
}

void UWorld::MarkCollisionDirty(UPrimitiveComponent* Component)
{
    if (!IsValid(Component) || Component->IsBeingDestroyed() || !Component->IsRegistered() || Component->GetWorld() != this) return;
    if ((Component->CollisionEntryIndex != INDEX_NONE || Component->IsCollisionEnabled()) && !Component->bCollisionUpdateQueued)
    {
        Component->bCollisionUpdateQueued = true;
        DirtyCollisionComponents.Add(Component);
    }
}

void UWorld::UpdateCollisionIndex()
{
    bool bRefit = false;
    if (!bCollisionTopologyDirty)
    {
        for (UPrimitiveComponent* Component : DirtyCollisionComponents)
        {
            Component->bCollisionUpdateQueued = false;
            FCollisionGeometry Geometry;
            const bool bHasGeometry = Component->IsCollisionEnabled() && Component->GetCollisionGeometry(Geometry);
            const int32 Index = Component->CollisionEntryIndex;
            if (Index != INDEX_NONE && bHasGeometry)
            {
                CollisionEntries[Index] = { Component, Geometry, Geometry.GetBounds() };
                ++CollisionQueryStats.GeometryUpdates;
                bRefit = true;
            }
            else if ((Index != INDEX_NONE) != bHasGeometry) bCollisionTopologyDirty = true;
        }
        DirtyCollisionComponents.Reset();
        if (!bCollisionTopologyDirty)
        {
            if (bRefit) { CollisionBVH.Refit(); ++CollisionQueryStats.IndexRefits; }
            return;
        }
    }
    CollisionEntries.Reset();
    TArray<uint32> Indices;
    for (UPrimitiveComponent* Component : CollisionComponents)
    {
        Component->CollisionEntryIndex = INDEX_NONE;
        Component->bCollisionUpdateQueued = false;
        FCollisionGeometry Geometry;
        if (Component->IsCollisionEnabled() && Component->GetCollisionGeometry(Geometry))
        {
            Component->CollisionEntryIndex = static_cast<int32>(CollisionEntries.Num());
            Indices.Add(static_cast<uint32>(CollisionEntries.Num()));
            CollisionEntries.Add({ Component, Geometry, Geometry.GetBounds() });
            ++CollisionQueryStats.GeometryUpdates;
        }
    }
    DirtyCollisionComponents.Reset();
    CollisionBVH.Build(std::span<const uint32>(Indices.GetData(), Indices.Num()));
    ++CollisionQueryStats.IndexBuilds;
    bCollisionTopologyDirty = false;
}

bool UWorld::ComponentSweepMulti(TArray<FHitResult>& OutHits, UPrimitiveComponent* Primitive, const FVector& Start, const FVector& End, const FQuat& Rotation, bool bReportInitialPenetration)
{
    ++CollisionQueryStats.SweepQueries;
    OutHits.Reset();
    FScopedObjectExecution ObjectScope(this);
    FCollisionGeometry Geometry;
    if (!IsValid(Primitive) || !Primitive->IsCollisionEnabled() || !Primitive->GetCollisionGeometry(Geometry)) return false;
    if (!Primitive->IsCollisionEnabled()) return false;
    const FVector LocalCenter = Primitive->GetWorldRotation().Quaternion().Inverse().RotateVector(Geometry.Center - Primitive->GetWorldLocation());
    Geometry.Center = Start + Rotation.RotateVector(LocalCenter);
    Geometry.Rotation = Rotation;
    const FVector Delta = End - Start;
    UpdateCollisionIndex();
    FBox SweptBounds = Geometry.GetBounds();
    FCollisionGeometry EndGeometry = Geometry; EndGeometry.Center += Delta; SweptBounds.Expand(EndGeometry.GetBounds());
    float BlockingTime = 1.f;
    bool bBlockingHit = false;
    CollisionBVH.Query([&](const FBox& Bounds) { return CollisionBoundsIntersect(SweptBounds, Bounds); },
        [&](const uint32& Index)
        {
            const auto& Entry = CollisionEntries[Index];
            UPrimitiveComponent* Other = Entry.Component;
            if (Other == Primitive || Other->GetOwner() == Primitive->GetOwner() || !Other->IsCollisionEnabled()) return;
            const auto Response = ResolveCollisionResponse(Primitive->GetCollisionSettings(), Other->GetCollisionSettings());
            if (Response == ECollisionResponse::Ignore) return;
            FHitResult Hit;
            ++CollisionQueryStats.NarrowPhaseTests;
            if (!SweepCollisionGeometry(Geometry, Delta, Entry.Geometry, Hit, bReportInitialPenetration)) return;
            Hit.bBlockingHit = Response == ECollisionResponse::Block;
            Hit.HitComponent = Other;
            Hit.TraceStart = Start; Hit.TraceEnd = End;
            Hit.Location = Start + Delta * Hit.Time;
            OutHits.Add(Hit);
            if (Hit.bBlockingHit)
            {
                bBlockingHit = true;
                // MoveComponent가 초기 침투를 무시할 수 있으므로 그 뒤의 충돌을 미리 자르지 않는다.
                if (!bReportInitialPenetration || !Hit.bStartPenetrating)
                    BlockingTime = std::min(BlockingTime, Hit.Time);
            }
        });
    std::sort(OutHits.begin(), OutHits.end(), [](const FHitResult& A, const FHitResult& B)
    {
        if (A.Time != B.Time) return A.Time < B.Time;
        if (A.bBlockingHit != B.bBlockingHit) return !A.bBlockingHit;
        return A.HitComponent->GetUUID() < B.HitComponent->GetUUID();
    });
    while (!OutHits.IsEmpty() && OutHits[OutHits.Num() - 1].Time > BlockingTime) OutHits.RemoveLast();
    return bBlockingHit;
}

bool UWorld::LineTraceByChannel(const FRay& Ray, float MaxDistance, ECollisionChannel Channel, FHitResult& OutHit, const AActor* IgnoreActor)
{
    OutHit = FHitResult(1.f);
    if (!IsCollisionChannel(Channel) || MaxDistance <= 0 || !std::isfinite(MaxDistance)) return false;
    UpdateCollisionIndex();
    const FVector Delta = Ray.Direction.GetSafeNormal() * MaxDistance;
    const FCollisionGeometry Point = MakeCollisionGeometry(FCollisionShape::MakeSphere(0.f), FTransform(FRotator(), Ray.Origin));
    FBox Bounds = Point.GetBounds(); Bounds.Expand({ Ray.Origin + Delta, Ray.Origin + Delta });
    CollisionBVH.Query([&](const FBox& Box) { return CollisionBoundsIntersect(Bounds, Box); }, [&](const uint32& Index)
    {
        const auto& Entry = CollisionEntries[Index];
        if (!Entry.Component->IsCollisionEnabled() || Entry.Component->GetOwner() == IgnoreActor ||
            Entry.Component->GetCollisionSettings().GetResponse(Channel) != ECollisionResponse::Block) return;
        FHitResult Hit;
        if (SweepCollisionGeometry(Point, Delta, Entry.Geometry, Hit) && (!OutHit.bBlockingHit || Hit.Time < OutHit.Time))
        {
            OutHit = Hit; OutHit.bBlockingHit = true; OutHit.HitComponent = Entry.Component;
            OutHit.TraceStart = Ray.Origin; OutHit.TraceEnd = Ray.Origin + Delta;
        }
    });
    return OutHit.bBlockingHit;
}

bool UWorld::OverlapMulti(const UPrimitiveComponent& Query, const FCollisionGeometry& Geometry, TArray<UPrimitiveComponent*>& OutComponents)
{
    OutComponents.Reset();
    if (!Query.IsCollisionEnabled() || !Geometry.IsValid()) return false;
    UpdateCollisionIndex();
    const FBox Bounds = Geometry.GetBounds();
    CollisionBVH.Query([&](const FBox& Box) { return CollisionBoundsIntersect(Bounds, Box); }, [&](const uint32& Index)
    {
        const auto& Entry = CollisionEntries[Index];
        if (Entry.Component != &Query && Entry.Component->GetOwner() != Query.GetOwner() && Entry.Component->IsCollisionEnabled() &&
            ResolveCollisionResponse(Query.GetCollisionSettings(), Entry.Component->GetCollisionSettings()) != ECollisionResponse::Ignore &&
            OverlapCollisionGeometry(Geometry, Entry.Geometry)) OutComponents.Add(Entry.Component);
    });
    return !OutComponents.IsEmpty();
}

void UWorld::RemoveCollisionListeners(const void* Listener)
{
    for (ULevel* Level : Levels)
    {
        for (AActor* Actor : Level->GetActors())
        {
            Actor->OnActorHit.RemoveAll(Listener);
            Actor->OnActorBeginOverlap.RemoveAll(Listener);
            Actor->OnActorEndOverlap.RemoveAll(Listener);
        }
    }
    for (UPrimitiveComponent* Component : CollisionComponents)
    {
        Component->OnComponentHit.RemoveAll(Listener);
        Component->OnComponentBeginOverlap.RemoveAll(Listener);
        Component->OnComponentEndOverlap.RemoveAll(Listener);
    }
}
