#include "EnginePCH.h"
#include "World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Collision/Ray.h"

void UWorld::RegisterCollisionComponent(UPrimitiveComponent* Component)
{
    if (CollisionComponents.Find(Component) == INDEX_NONE) CollisionComponents.Add(Component);
    bCollisionTopologyDirty = true;
    MarkCollisionDirty(Component);
}

void UWorld::UnregisterCollisionComponent(UPrimitiveComponent* Component)
{
    const auto Previous = Component->OverlappingComponents;
    for (UPrimitiveComponent* Other : Previous) SetOverlapPair(Component, Other, false);
    int32 Index = CollisionComponents.Find(Component);
    if (Index != INDEX_NONE) CollisionComponents.RemoveAtSwap(Index);
    Index = PendingOverlapUpdates.Find(Component);
    if (Index != INDEX_NONE) PendingOverlapUpdates.RemoveAtSwap(Index);
    Index = DirtyCollisionComponents.Find(Component);
    if (Index != INDEX_NONE) DirtyCollisionComponents.RemoveAtSwap(Index);
    Component->bCollisionUpdateQueued = false;
    Component->CollisionEntryIndex = INDEX_NONE;
    bCollisionTopologyDirty = true;
}

void UWorld::MarkCollisionDirty(UPrimitiveComponent* Component)
{
    if (!Component || !Component->IsRegistered() || Component->GetWorld() != this) return;
    // Disabled render-only children have no collision entry to update.
    if ((Component->CollisionEntryIndex != INDEX_NONE || Component->IsCollisionEnabled()) && !Component->bCollisionUpdateQueued)
    {
        Component->bCollisionUpdateQueued = true;
        DirtyCollisionComponents.Add(Component);
    }
    if ((Component->CollisionSettings.bGenerateOverlapEvents || !Component->OverlappingComponents.IsEmpty()) &&
        PendingOverlapUpdates.Find(Component) == INDEX_NONE) PendingOverlapUpdates.Add(Component);
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

bool UWorld::SweepSingle(const UPrimitiveComponent& Moving, const FCollisionGeometry& Geometry, const FVector& Delta,
    FHitResult& OutHit, TArray<UPrimitiveComponent*>* SweptOverlaps)
{
    ++CollisionQueryStats.SweepQueries;
    OutHit = FHitResult(1.f);
    OutHit.Init(Moving.GetWorldLocation(), Moving.GetWorldLocation() + Delta);
    if (SweptOverlaps) SweptOverlaps->Reset();
    if (!Moving.IsCollisionEnabled() || !Geometry.IsValid()) return false;
    UpdateCollisionIndex();
    FBox SweptBounds = Geometry.GetBounds();
    FCollisionGeometry End = Geometry; End.Center += Delta; SweptBounds.Expand(End.GetBounds());
    TArray<std::pair<UPrimitiveComponent*, float>> Overlaps;
    CollisionBVH.Query([&](const FBox& Bounds) { return CollisionBoundsIntersect(SweptBounds, Bounds); },
        [&](const uint32& Index)
        {
            const auto& Entry = CollisionEntries[Index];
            UPrimitiveComponent* Other = Entry.Component;
            if (Other == &Moving || Other->GetOwner() == Moving.GetOwner() || !Other->IsCollisionEnabled()) return;
            const auto Response = ResolveCollisionResponse(Moving.GetCollisionSettings(), Other->GetCollisionSettings());
            if (Response == ECollisionResponse::Ignore) return;
            FHitResult Hit;
            ++CollisionQueryStats.NarrowPhaseTests;
            if (!SweepCollisionGeometry(Geometry, Delta, Entry.Geometry, Hit)) return;
            if (Response == ECollisionResponse::Overlap)
            {
                if (Moving.GetCollisionSettings().bGenerateOverlapEvents && Other->GetCollisionSettings().bGenerateOverlapEvents)
                    Overlaps.Add({ Other, Hit.Time });
                return;
            }
            if (!OutHit.bBlockingHit || Hit.Time < OutHit.Time || (Hit.Time == OutHit.Time && Other->GetUUID() < OutHit.HitComponent->GetUUID()))
            {
                OutHit = Hit; OutHit.bBlockingHit = true; OutHit.HitComponent = Other;
                OutHit.TraceStart = Moving.GetWorldLocation(); OutHit.TraceEnd = OutHit.TraceStart + Delta;
                OutHit.Location = OutHit.TraceStart + Delta * OutHit.Time;
            }
        });
    if (SweptOverlaps)
        for (const auto& [Component, Time] : Overlaps) if (Time <= OutHit.Time) SweptOverlaps->Add(Component);
    return OutHit.bBlockingHit;
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

void UWorld::SetOverlapPair(UPrimitiveComponent* A, UPrimitiveComponent* B, bool bOverlapping)
{
    if (!A || !B || A == B) return;
    const bool bWasOverlapping = A->OverlappingComponents.Find(B) != INDEX_NONE;
    if (bWasOverlapping == bOverlapping) return;
    if (bOverlapping)
    {
        A->OverlappingComponents.Add(B);
        if (B->OverlappingComponents.Find(A) == INDEX_NONE) B->OverlappingComponents.Add(A);
    }
    else
    {
        int32 I = A->OverlappingComponents.Find(B); if (I != INDEX_NONE) A->OverlappingComponents.RemoveAtSwap(I);
        I = B->OverlappingComponents.Find(A); if (I != INDEX_NONE) B->OverlappingComponents.RemoveAtSwap(I);
    }
    if (!bBegunPlay) return;
    CollisionEvents.Add({ bOverlapping ? 1 : 2, A, B, A->GetUUID(), B->GetUUID(), FHitResult() });
    CollisionEvents.Add({ bOverlapping ? 1 : 2, B, A, B->GetUUID(), A->GetUUID(), FHitResult() });
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

void UWorld::NotifySweptOverlaps(UPrimitiveComponent* Component, const TArray<UPrimitiveComponent*>& Components)
{
    for (UPrimitiveComponent* Other : Components) SetOverlapPair(Component, Other, true);
}

void UWorld::UpdateComponentOverlaps(UPrimitiveComponent* Component)
{
    if (!Component || CollisionComponents.Find(Component) == INDEX_NONE) return;
    TArray<UPrimitiveComponent*> Current;
    FCollisionGeometry Geometry;
    if (bBegunPlay && Component->IsCollisionEnabled() && Component->GetCollisionSettings().bGenerateOverlapEvents && Component->GetCollisionGeometry(Geometry))
    {
        UpdateCollisionIndex();
        const FBox Bounds = Geometry.GetBounds();
        CollisionBVH.Query([&](const FBox& Box) { return CollisionBoundsIntersect(Bounds, Box); }, [&](const uint32& Index)
        {
            const auto& Entry = CollisionEntries[Index];
            UPrimitiveComponent* Other = Entry.Component;
            if (Other != Component && Other->GetOwner() != Component->GetOwner() && Other->IsCollisionEnabled() &&
                Other->GetCollisionSettings().bGenerateOverlapEvents &&
                ResolveCollisionResponse(Component->GetCollisionSettings(), Other->GetCollisionSettings()) == ECollisionResponse::Overlap &&
                OverlapCollisionGeometry(Geometry, Entry.Geometry)) Current.Add(Other);
        });
    }
    const auto Previous = Component->OverlappingComponents;
    for (UPrimitiveComponent* Other : Previous) if (Current.Find(Other) == INDEX_NONE) SetOverlapPair(Component, Other, false);
    for (UPrimitiveComponent* Other : Current) SetOverlapPair(Component, Other, true);
}

void UWorld::QueueHit(UPrimitiveComponent* Component, const FHitResult& Hit)
{
    if (!bBegunPlay || !Hit.HitComponent) return;
    UPrimitiveComponent* Other = Hit.HitComponent;
    CollisionEvents.Add({ 0, Component, Other, Component->GetUUID(), Other->GetUUID(), Hit });
    FHitResult Reverse = Hit;
    Reverse.HitComponent = Component; Reverse.Normal = -Hit.Normal; Reverse.ImpactNormal = -Hit.ImpactNormal;
    Reverse.Location = Other->GetWorldLocation();
    CollisionEvents.Add({ 0, Other, Component, Other->GetUUID(), Component->GetUUID(), Reverse });
}

bool UWorld::DeferComponentDestruction(AActor* Owner, UActorComponent* Component)
{
    if (!bDispatchingCollisionEvents) return false;
    const auto Pair = std::make_pair(Owner, Component);
    if (DeferredComponentDestruction.Find(Pair) == INDEX_NONE) DeferredComponentDestruction.Add(Pair);
    return true;
}

void UWorld::DispatchCollisionEvents()
{
    if (bDispatchingCollisionEvents) return;
    const auto Events = CollisionEvents; CollisionEvents.Reset();
    bDispatchingCollisionEvents = true;
    auto Alive = [](UObject* Object, uint32 ID)
    { return GUObjectArray.Find(Object) != INDEX_NONE && Object->GetUUID() == ID; };
    for (const auto& Event : Events)
    {
        if (!Alive(Event.Component, Event.ComponentID) || !Event.Component->IsRegistered()) continue;
        UPrimitiveComponent* Other = Alive(Event.Other, Event.OtherID) ? Event.Other : nullptr;
        if (Event.Type == 0)
        {
            FHitResult Hit = Event.Hit; Hit.HitComponent = Other;
            Event.Component->OnComponentHit.Broadcast(Event.Component, Other, Hit);
        }
        else if (Event.Type == 1 && Other) Event.Component->OnComponentBeginOverlap.Broadcast(Event.Component, Other);
        else if (Event.Type == 2) Event.Component->OnComponentEndOverlap.Broadcast(Event.Component, Other);
    }
    bDispatchingCollisionEvents = false;
    const auto Components = DeferredComponentDestruction; DeferredComponentDestruction.Reset();
    for (const auto& [Owner, Component] : Components)
        if (GUObjectArray.Find(Owner) != INDEX_NONE && GUObjectArray.Find(Component) != INDEX_NONE) Owner->DestroyComponent(Component);
    const auto Actors = DeferredActorDestruction; DeferredActorDestruction.Reset();
    for (AActor* Actor : Actors) if (GUObjectArray.Find(Actor) != INDEX_NONE) DestroyActor(Actor);
}

void UWorld::RemoveCollisionListeners(const void* Listener)
{
    for (UPrimitiveComponent* Component : CollisionComponents)
    {
        Component->OnComponentHit.RemoveAll(Listener);
        Component->OnComponentBeginOverlap.RemoveAll(Listener);
        Component->OnComponentEndOverlap.RemoveAll(Listener);
    }
}
