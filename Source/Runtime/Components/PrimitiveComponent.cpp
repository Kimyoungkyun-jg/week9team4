#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "Render/LineBatcher.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

namespace
{
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
	FHitResult Result(1.f);
    const FVector Start = GetWorldLocation();
    const FRotator PreviousRotation = GetWorldRotation();
    Result.Init(Start, Start + Delta);
    for (int Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(Delta[Axis]) || !std::isfinite(NewRotation[Axis])) { if (Hit) *Hit = Result; return false; }
    UWorld* World = GetWorld();
    FCollisionGeometry Geometry;
    TArray<UPrimitiveComponent*> SweptOverlaps;
    const bool bBlocked = bSweep && World && IsCollisionEnabled() && GetCollisionGeometry(Geometry) &&
        World->SweepSingle(*this, Geometry, Delta, Result, &SweptOverlaps);
    const float Length = Delta.Size();
    const float Time = bBlocked ? std::max(0.f, Result.Time - (Length > SMALL_NUMBER ? 0.001f / Length : 0.f)) : 1.f;
    const FVector Destination = Start + Delta * Time;
    ApplyWorldLocationAndRotation(Destination, NewRotation);
    Result.Location = Destination;
    if (Hit) *Hit = Result;
    if (World)
    {
        World->NotifySweptOverlaps(this, SweptOverlaps);
        World->UpdateComponentOverlaps(this);
        if (bBlocked) World->QueueHit(this, Result);
    }
    const bool bRotated = PreviousRotation.Pitch != NewRotation.Pitch || PreviousRotation.Yaw != NewRotation.Yaw || PreviousRotation.Roll != NewRotation.Roll;
    return !bBlocked || Time > 0.f || bRotated;
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
    if (Property.Name == "StaticMesh") { OnTransformDirty(); MarkRenderStateDirty(); }
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
    return CollisionSettings.bEnabled && GetOwner() && GetOwner()->GetActorEnableCollision();
}

void UPrimitiveComponent::OnRegister()
{
    Super::OnRegister();
    GetWorld()->RegisterCollisionComponent(this);
}

void UPrimitiveComponent::OnUnregister()
{
    GetWorld()->UnregisterCollisionComponent(this);
    Super::OnUnregister();
}

void UPrimitiveComponent::SetCollisionEnabled(bool bEnabled)
{ CollisionSettings.bEnabled = bEnabled; if (GetWorld()) GetWorld()->MarkCollisionDirty(this); }
void UPrimitiveComponent::SetGenerateOverlapEvents(bool bEnabled)
{ CollisionSettings.bGenerateOverlapEvents = bEnabled; if (GetWorld()) GetWorld()->MarkCollisionDirty(this); }
void UPrimitiveComponent::SetCollisionObjectType(ECollisionChannel Channel)
{ if (IsCollisionChannel(Channel)) { CollisionSettings.ObjectChannel = Channel; if (GetWorld()) GetWorld()->MarkCollisionDirty(this); } }
void UPrimitiveComponent::SetCollisionResponseToChannel(ECollisionChannel Channel, ECollisionResponse Response)
{
    if (!IsCollisionChannel(Channel) || Response < ECollisionResponse::Ignore || Response > ECollisionResponse::Block) return;
    CollisionSettings.Responses[static_cast<int32>(Channel)] = Response;
    if (GetWorld()) GetWorld()->MarkCollisionDirty(this);
}
void UPrimitiveComponent::SetCollisionResponseToAllChannels(ECollisionResponse Response)
{
    if (Response < ECollisionResponse::Ignore || Response > ECollisionResponse::Block) return;
    CollisionSettings.Responses.fill(Response);
    if (GetWorld()) GetWorld()->MarkCollisionDirty(this);
}
void UPrimitiveComponent::UpdateOverlaps()
{ if (GetWorld()) GetWorld()->UpdateComponentOverlaps(this); }

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
    if (GetWorld()) GetWorld()->MarkCollisionDirty(this);
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
