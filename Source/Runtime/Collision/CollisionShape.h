#pragma once

#include "Math/Transform.h"
#include "Math/Box.h"
#include "Collision/HitResult.h"
#include <array>

// Object and trace channels share a response table. Count is never a usable channel.
enum class ECollisionChannel : int32 { WorldStatic, WorldDynamic, Pawn, Projectile, Visibility, Camera, Count };
enum class ECollisionResponse : int32 { Ignore, Overlap, Block };
constexpr int32 CollisionChannelCount = static_cast<int32>(ECollisionChannel::Count);

inline bool IsCollisionChannel(ECollisionChannel Channel)
{
    return static_cast<int32>(Channel) >= 0 && static_cast<int32>(Channel) < CollisionChannelCount;
}

struct FCollisionSettings
{
    bool bEnabled = false;
    bool bGenerateOverlapEvents = false;
    ECollisionChannel ObjectChannel = ECollisionChannel::WorldDynamic;
    std::array<ECollisionResponse, CollisionChannelCount> Responses;

    FCollisionSettings() { Responses.fill(ECollisionResponse::Block); }
    ECollisionResponse GetResponse(ECollisionChannel Channel) const
    { return IsCollisionChannel(Channel) ? Responses[static_cast<int32>(Channel)] : ECollisionResponse::Ignore; }
};

inline ECollisionResponse ResolveCollisionResponse(const FCollisionSettings& A, const FCollisionSettings& B)
{
    if (!A.bEnabled || !B.bEnabled) return ECollisionResponse::Ignore;
    return std::min(A.GetResponse(B.ObjectChannel), B.GetResponse(A.ObjectChannel));
}

enum class ECollisionShape { None, Box, Sphere, Capsule };

// Local-space dimensions. Capsule half-height includes its hemispheres; its axis is Z.
struct FCollisionShape
{
    ECollisionShape Type = ECollisionShape::None;
    FVector Center = FVector::ZeroVector;
    FVector HalfExtent = FVector::ZeroVector;
    float Radius = 0.0f;
    float HalfHeight = 0.0f;
    static FCollisionShape MakeBox(const FVector& Extent, const FVector& Center = FVector::ZeroVector);
    static FCollisionShape MakeSphere(float Radius);
    static FCollisionShape MakeCapsule(float Radius, float HalfHeight);
};

// Query snapshot: no component or rendering dependencies in the geometry kernel.
struct FCollisionGeometry
{
    ECollisionShape Type = ECollisionShape::None;
    FVector Center = FVector::ZeroVector;
    FQuat Rotation;
    FVector HalfExtent = FVector::ZeroVector;
    float Radius = 0.0f;
    float HalfHeight = 0.0f;
    FVector Support(const FVector& Direction) const;
    FBox GetBounds() const;
    bool IsValid() const;
};

FCollisionGeometry MakeCollisionGeometry(const FCollisionShape& Shape, const FTransform& Transform);
bool CollisionBoundsIntersect(const FBox& A, const FBox& B);
bool OverlapCollisionGeometry(const FCollisionGeometry& A, const FCollisionGeometry& B, FHitResult* Hit = nullptr);
bool SweepCollisionGeometry(const FCollisionGeometry& Moving, const FVector& Delta, const FCollisionGeometry& Target, FHitResult& Hit);
