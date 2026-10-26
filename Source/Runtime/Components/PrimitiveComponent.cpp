#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "Render/LineBatcher.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	bool CanNotifyActorCollision(AActor* Actor)
	{
		return IsValid(Actor) && !Actor->IsPendingKillPending();
	}

	bool CanNotifyCollision(UPrimitiveComponent* Component)
	{
		return IsValid(Component) && !Component->IsBeingDestroyed() &&
			(!Component->GetOwner() || !Component->GetOwner()->IsPendingKillPending());
	}

	bool CanGenerateOverlap(UPrimitiveComponent* A, UPrimitiveComponent* B)
	{
		return A != B && CanNotifyCollision(A) && CanNotifyCollision(B) && A->IsRegistered() && B->IsRegistered() && A->GetWorld() == B->GetWorld() && A->IsCollisionEnabled() && B->IsCollisionEnabled() &&
			A->GetOwner() != B->GetOwner() && A->GetCollisionSettings().bGenerateOverlapEvents && B->GetCollisionSettings().bGenerateOverlapEvents &&
			ResolveCollisionResponse(A->GetCollisionSettings(), B->GetCollisionSettings()) == ECollisionResponse::Overlap;
	}

	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
    UpdateOverlaps();
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
    if (GetWorld()) GetWorld()->MarkCollisionDirty(this);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending()))
	{
		if (Hit) *Hit = FHitResult(1.f);
		return false;
	}
    const FVector Start = GetWorldLocation();
    const FRotator PreviousRotation = GetWorldRotation();
    FHitResult Result(1.f);
    Result.Init(Start, Start + Delta);
    for (int Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(Delta[Axis]) || !std::isfinite(NewRotation[Axis])) { if (Hit) *Hit = Result; return false; }
    UWorld* World = GetWorld();
    UWorld::FScopedObjectExecution ObjectScope(World ? World : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
    const bool bRotated = PreviousRotation.Pitch != NewRotation.Pitch || PreviousRotation.Yaw != NewRotation.Yaw || PreviousRotation.Roll != NewRotation.Roll;
    const float MinMovementDistSq = bSweep ? 16.f * KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER : 0.f;
    const float LengthSquared = Delta.SizeSquared();
    if (LengthSquared <= MinMovementDistSq && !bRotated)
    {
        if (Hit) *Hit = Result;
        return true;
    }
    TArray<UPrimitiveComponent*> SweptOverlaps;
    int32 FirstNonInitialOverlap = INDEX_NONE;
    if (bSweep && World && IsCollisionEnabled() && LengthSquared > MinMovementDistSq)
    {
        TArray<FHitResult> Hits;
        World->ComponentSweepMulti(Hits, this, Start, Start + Delta, PreviousRotation.Quaternion());
        const float Length = std::sqrt(LengthSquared);
        float BlockingNormalDotDelta = FLT_MAX;
        for (FHitResult& TestHit : Hits)
        {
            TestHit.Time = std::max(0.f, TestHit.Time - 0.001f / Length);
            if (TestHit.bBlockingHit)
            {
                if (TestHit.bStartPenetrating)
                {
                    const float NormalDotDelta = TestHit.ImpactNormal.Dot(Delta);
                    if (!Result.bBlockingHit || NormalDotDelta < BlockingNormalDotDelta)
                    {
                        Result = TestHit;
                        BlockingNormalDotDelta = NormalDotDelta;
                    }
                }
                else if (!Result.bBlockingHit) { Result = TestHit; break; }
            }
            else if (CanGenerateOverlap(this, TestHit.HitComponent))
            {
                if (FirstNonInitialOverlap == INDEX_NONE && TestHit.Time > 0.f) FirstNonInitialOverlap = SweptOverlaps.Num();
                SweptOverlaps.Add(TestHit.HitComponent);
            }
        }
    }
    FVector Destination = LengthSquared > MinMovementDistSq ? Start + Delta * Result.Time : Start;
    if (Result.bBlockingHit && (Destination - Start).SizeSquared() <= MinMovementDistSq)
    {
        Destination = Start;
        Result.Time = 0.f;
        if (FirstNonInitialOverlap != INDEX_NONE) SweptOverlaps.RemoveAt(FirstNonInitialOverlap, SweptOverlaps.Num() - FirstNonInitialOverlap);
    }
    const bool bMoved = (Destination - Start).SizeSquared() > 0.f || bRotated;
    if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending()))
    {
        if (Hit) *Hit = Result;
        return false;
    }
    if (bMoved) ApplyWorldLocationAndRotation(Destination, NewRotation);
    Result.Location = Destination;
    Result.Distance = (Destination - Start).Size();
    if (bMoved) UpdateOverlaps(&SweptOverlaps);
    if (Result.bBlockingHit && CanNotifyCollision(this)) DispatchBlockingHit(Result);
    if (Result.HitComponent && !IsValid(Result.HitComponent)) Result.HitComponent = nullptr;
    if (Hit) *Hit = Result;
    return bMoved;
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
    if (Property.Name == "StaticMesh") UpdateCollisionShape();
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" ||
		Property.Name == "bReceiveSpecular")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveSpecular(bool NewValue)
{
	if (bReceiveSpecular == NewValue) return;
	bReceiveSpecular = NewValue;
	MarkRenderStateDirty();
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::GetCollisionGeometry(FCollisionGeometry& OutGeometry) const
{
    FCollisionShape Shape;
    if (!BuildCollisionShape(Shape)) return false;
    OutGeometry = MakeCollisionGeometry(Shape, FTransform(GetWorldRotation(), GetWorldLocation(), GetWorldScale3D()));
    return OutGeometry.IsValid();
}

bool UPrimitiveComponent::IsCollisionEnabled() const
{
    return IsValid(this) && !IsBeingDestroyed() && CollisionSettings.bEnabled && IsValid(GetOwner()) &&
        !GetOwner()->IsActorBeingDestroyed() && GetOwner()->GetActorEnableCollision();
}

void UPrimitiveComponent::OnRegister()
{
    Super::OnRegister();
    GetWorld()->RegisterCollisionComponent(this);
    if (HasBegunPlay()) UpdateOverlaps();
}

void UPrimitiveComponent::OnUnregister()
{
    GetWorld()->UnregisterCollisionComponent(this);
    ClearComponentOverlaps(true);
    Super::OnUnregister();
}

void UPrimitiveComponent::SetCollisionEnabled(bool bEnabled)
{
    if (CollisionSettings.bEnabled == bEnabled) return;
    CollisionSettings.bEnabled = bEnabled;
    OnComponentCollisionSettingsChanged();
}
void UPrimitiveComponent::SetGenerateOverlapEvents(bool bEnabled)
{
    if (CollisionSettings.bGenerateOverlapEvents == bEnabled) return;
    CollisionSettings.bGenerateOverlapEvents = bEnabled;
    OnComponentCollisionSettingsChanged();
}
void UPrimitiveComponent::SetCollisionObjectType(ECollisionChannel Channel)
{
    if (!IsCollisionChannel(Channel) || CollisionSettings.ObjectChannel == Channel) return;
    CollisionSettings.ObjectChannel = Channel;
    OnComponentCollisionSettingsChanged();
}
void UPrimitiveComponent::SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response)
{
    if (!IsCollisionChannel(Channel) || Response < ECollisionResponse::Ignore || Response > ECollisionResponse::Block) return;
    if (CollisionSettings.GetResponse(Channel) == Response) return;
    CollisionSettings.Responses[static_cast<int32>(Channel)] = Response;
    OnComponentCollisionSettingsChanged();
}
void UPrimitiveComponent::SetCollisionResponseToAllChannels(ECollisionResponse Response)
{
    if (Response < ECollisionResponse::Ignore || Response > ECollisionResponse::Block) return;
    if (std::all_of(CollisionSettings.Responses.begin(), CollisionSettings.Responses.end(), [Response](ECollisionResponse Value) { return Value == Response; })) return;
    CollisionSettings.Responses.fill(Response);
    OnComponentCollisionSettingsChanged();
}

void UPrimitiveComponent::OnComponentCollisionSettingsChanged(bool bUpdateOverlaps)
{
    if (!CanNotifyCollision(this)) return;
    UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
    if (GetWorld()) GetWorld()->MarkCollisionDirty(this);
    if (bUpdateOverlaps) UpdateOverlaps();
}

void UPrimitiveComponent::UpdateCollisionShape()
{
    if (!CanNotifyCollision(this)) return;
    UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
    OnTransformDirty();
    if (!CanNotifyCollision(this)) return;
    MarkRenderStateDirty();
    UpdateOverlaps();
}
void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* Other)
{
    if (!CanGenerateOverlap(this, Other) || OverlappingComponents.Find(Other) != INDEX_NONE) return;
    AActor* MyActor = GetOwner();
    AActor* OtherActor = Other->GetOwner();
    const bool bNotifyActorBegin = !MyActor->IsOverlappingActor(OtherActor);
    OverlappingComponents.Add(Other);
    if (Other->OverlappingComponents.Find(this) == INDEX_NONE) Other->OverlappingComponents.Add(this);
    if (!GetWorld() || !GetWorld()->HasBegunPlay()) return;
    OnComponentBeginOverlap.Broadcast(this, Other);
    if (CanNotifyCollision(Other)) Other->OnComponentBeginOverlap.Broadcast(Other, this);
    if (bNotifyActorBegin)
    {
        if (CanNotifyActorCollision(MyActor)) MyActor->OnActorBeginOverlap.Broadcast(MyActor, OtherActor);
        if (CanNotifyActorCollision(OtherActor)) OtherActor->OnActorBeginOverlap.Broadcast(OtherActor, MyActor);
    }
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* Other, bool bSkipNotifySelf)
{
    const int32 Index = OverlappingComponents.Find(Other);
    if (Index == INDEX_NONE) return;
    AActor* MyActor = GetOwner();
    AActor* OtherActor = Other->GetOwner();
    OverlappingComponents.RemoveAtSwap(Index);
    const int32 OtherIndex = Other->OverlappingComponents.Find(this);
    if (OtherIndex != INDEX_NONE) Other->OverlappingComponents.RemoveAtSwap(OtherIndex);
    // Capture the last-pair transition before component callbacks can remove another pair.
    const bool bNotifyActorEnd = MyActor && OtherActor && MyActor != OtherActor && !MyActor->IsOverlappingActor(OtherActor);
    if (!GetWorld() || !GetWorld()->HasBegunPlay()) return;
    if (!bSkipNotifySelf && CanNotifyCollision(this)) OnComponentEndOverlap.Broadcast(this, Other);
    if (CanNotifyCollision(Other)) Other->OnComponentEndOverlap.Broadcast(Other, this);
    if (bNotifyActorEnd)
    {
        if (CanNotifyActorCollision(MyActor)) MyActor->OnActorEndOverlap.Broadcast(MyActor, OtherActor);
        if (CanNotifyActorCollision(OtherActor)) OtherActor->OnActorEndOverlap.Broadcast(OtherActor, MyActor);
    }
}

void UPrimitiveComponent::ClearComponentOverlaps(bool bSkipNotifySelf)
{
    const auto Previous = OverlappingComponents;
    for (UPrimitiveComponent* Other : Previous) EndComponentOverlap(Other, bSkipNotifySelf);
}

void UPrimitiveComponent::UpdateOverlaps(const TArray<UPrimitiveComponent*>* PendingOverlaps)
{
    UWorld* World = GetWorld();
    if (!CanNotifyCollision(this) || !IsRegistered() || !World || !World->HasBegunPlay()) return;
    UWorld::FScopedObjectExecution ObjectScope(World);
    if (PendingOverlaps) for (UPrimitiveComponent* Other : *PendingOverlaps) BeginComponentOverlap(Other);
    TArray<UPrimitiveComponent*> Current;
    FCollisionGeometry Geometry;
    if (CanNotifyCollision(this) && IsRegistered() && GetWorld() == World && IsCollisionEnabled() && CollisionSettings.bGenerateOverlapEvents && GetCollisionGeometry(Geometry) && CanNotifyCollision(this))
    {
        World->OverlapMulti(*this, Geometry, Current);
        for (int32 Index = Current.Num() - 1; Index >= 0; --Index)
            if (!CanGenerateOverlap(this, Current[Index])) Current.RemoveAtSwap(Index);
    }
    const auto Previous = OverlappingComponents;
    for (UPrimitiveComponent* Other : Previous) if (Current.Find(Other) == INDEX_NONE) EndComponentOverlap(Other);
    for (UPrimitiveComponent* Other : Current) BeginComponentOverlap(Other);
    Super::UpdateOverlaps();
}

void UPrimitiveComponent::DispatchBlockingHit(const FHitResult& Hit)
{
    UPrimitiveComponent* Other = Hit.HitComponent;
    if (!CanNotifyCollision(this) || !CanNotifyCollision(Other)) return;
    AActor* MyActor = GetOwner();
    AActor* OtherActor = Other->GetOwner();
    if (CanNotifyActorCollision(MyActor)) MyActor->OnActorHit.Broadcast(MyActor, OtherActor, Hit);
    if (CanNotifyCollision(this)) OnComponentHit.Broadcast(this, Other, Hit);
    if (CanNotifyCollision(Other))
    {
        FHitResult Reverse = Hit;
        Reverse.HitComponent = this;
        Reverse.Normal = -Hit.Normal;
        Reverse.ImpactNormal = -Hit.ImpactNormal;
        if (CanNotifyActorCollision(OtherActor)) OtherActor->OnActorHit.Broadcast(OtherActor, MyActor, Reverse);
        if (CanNotifyCollision(Other)) Other->OnComponentHit.Broadcast(Other, this, Reverse);
    }
}

void UPrimitiveComponent::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);
    auto Collision = Record.EnterRecord("Collision");
    Collision << SA_VALUE("Enabled", CollisionSettings.bEnabled) << SA_VALUE("GenerateOverlapEvents", CollisionSettings.bGenerateOverlapEvents);
    int32 Channel = static_cast<int32>(CollisionSettings.ObjectChannel);
    Collision << SA_VALUE("ObjectChannel", Channel);
    if (Record.GetUnderlyingArchive().IsLoading())
        CollisionSettings.ObjectChannel = IsCollisionChannel(static_cast<ECollisionChannel>(Channel)) ? static_cast<ECollisionChannel>(Channel) : ECollisionChannel::WorldDynamic;
    int32 Count = CollisionChannelCount;
    auto Responses = Collision.EnterArray("Responses", Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        int32 Response = Index < CollisionChannelCount ? static_cast<int32>(CollisionSettings.Responses[Index]) : 0;
        Responses.EnterElement() << Response;
        if (Record.GetUnderlyingArchive().IsLoading() && Index < CollisionChannelCount)
            CollisionSettings.Responses[Index] = static_cast<ECollisionResponse>(std::clamp(Response, 0, 2));
    }
    if (Record.GetUnderlyingArchive().IsLoading()) OnComponentCollisionSettingsChanged();
}

void UPrimitiveComponent::DrawCollision(FLineBatcher& Lines, const FVector4& Color) const
{
    FCollisionGeometry G;
    if (!GetCollisionGeometry(G)) return;
    if (G.Type == ECollisionShape::Box)
    {
        Lines.AddOrientedBox({ -G.HalfExtent, G.HalfExtent }, FTransform(G.Rotation.ToFRotator(), G.Center).GetLocalMatrix(), Color);
        return;
    }
    const FVector X = G.Rotation.RotateVector(FVector(1, 0, 0));
    const FVector Y = G.Rotation.RotateVector(FVector(0, 1, 0));
    const FVector Z = G.Rotation.RotateVector(FVector(0, 0, 1));
    const float Segment = G.Type == ECollisionShape::Capsule ? std::max(0.f, G.HalfHeight - G.Radius) : 0.f;
    auto Arc = [&](const FVector& Center, const FVector& U, const FVector& V, float Start, float End)
    {
        FVector Previous = Center + (U * std::cos(Start) + V * std::sin(Start)) * G.Radius;
        for (int I = 1; I <= 32; ++I)
        {
            const float Angle = Start + (End - Start) * I / 32.f;
            const FVector Next = Center + (U * std::cos(Angle) + V * std::sin(Angle)) * G.Radius;
            Lines.AddLine(Previous, Next, Color); Previous = Next;
        }
    };
    constexpr float Pi = 3.14159265358979323846f;
    if (Segment == 0)
    { Arc(G.Center, X, Y, 0, 2 * Pi); Arc(G.Center, X, Z, 0, 2 * Pi); Arc(G.Center, Y, Z, 0, 2 * Pi); }
    else
    {
        for (float Sign : { -1.f, 1.f })
        {
            const FVector Center = G.Center + Z * (Segment * Sign);
            Arc(Center, X, Y, 0, 2 * Pi);
            Arc(Center, X, Z * Sign, 0, Pi); Arc(Center, Y, Z * Sign, 0, Pi);
            Lines.AddLine(G.Center + X * (G.Radius * Sign) - Z * Segment, G.Center + X * (G.Radius * Sign) + Z * Segment, Color);
            Lines.AddLine(G.Center + Y * (G.Radius * Sign) - Z * Segment, G.Center + Y * (G.Radius * Sign) + Z * Segment, Color);
        }
    }
}
