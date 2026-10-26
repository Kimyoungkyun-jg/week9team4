#include "EnginePCH.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/FireBallComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/TestGameMode.h"

namespace
{
    template <typename T>
    bool IsLiveOwnedComponent(const AActor& Owner, T* Component)
    {
        return Component && Owner.GetComponents().Find(Component) != INDEX_NONE && IsValid(Component) && !Component->IsBeingDestroyed();
    }
}

AProjectileActor::AProjectileActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>("Collision");
    Collision->SetSphereRadius(0.5f);
    Collision->SetCollisionObjectType(ECollisionChannel::Projectile);
    Collision->SetCollisionEnabled(true);
    SetRootComponent(Collision);
    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("ProjectileMeshComponent");
    // 이동용 루트는 표시 상태와 분리한다. 숨겨진 메쉬를 부모로 두면 FireBall 자식도 함께 숨겨진다.
    StaticMeshComponent->SetupAttachment(GetRootComponent());
    StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    StaticMeshComponent->SetRelativeScale3D(FVector(0.5f));
    StaticMeshComponent->SetVisible(false);
    StaticMeshComponent->SetHiddenInGame(true);
    StaticMeshComponent->SetAutoActivate(false);
    StaticMeshComponent->SetCollisionEnabled(false);
    FireBallComponent = CreateDefaultSubobject<UFireBallComponent>("FireBallComponent");
    FireBallComponent->SetupAttachment(GetRootComponent());
    FireBallComponent->SetVisibility(false);
    FireBallComponent->SetHiddenInGame(true);
    FireBallComponent->SetAutoActivate(false);
    // FireBall은 깊이 버퍼의 표면에만 색을 입히므로 전용 구체를 함께 움직인다.
    FireBallMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FireBallMeshComponent");
    FireBallMeshComponent->SetupAttachment(GetRootComponent());
    FireBallMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallComponent->GetRadius() * 0.5f));
    FireBallMeshComponent->SetVisible(false);
    FireBallMeshComponent->SetHiddenInGame(true);
    FireBallMeshComponent->SetAutoActivate(false);
    FireBallMeshComponent->SetCollisionEnabled(false);
    ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovementComponent");
    ProjectileMovementComponent->SetAutoActivate(false);
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->InitialSpeed = 20.0f;
    ProjectileMovementComponent->MaxSpeed = 100.0f;
    ProjectileMovementComponent->ProjectileGravityScale = 1.0f;
    ProjectileMovementComponent->bShouldBounce = true;
    ProjectileMovementComponent->bRotationFollowsVelocity = true;
    ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;
    ProjectileMovementComponent->bSimulationEnabled = false;
    ProjectileMovementComponent->PrimaryComponentTick.bStartWithTickEnabled = false;
}

void AProjectileActor::BeginPlay()
{
	Super::BeginPlay();
	if (IsPendingKillPending()) return;
	if (IsLiveOwnedComponent(*this, ProjectileMovementComponent))
		ProjectileMovementComponent->OnProjectileImpact.AddRaw(this, &AProjectileActor::HandleProjectileImpact);
}

void AProjectileActor::EndPlay()
{
	if (IsLiveOwnedComponent(*this, ProjectileMovementComponent))
		ProjectileMovementComponent->OnProjectileImpact.RemoveAll(this);
	Super::EndPlay();
}

void AProjectileActor::HandleProjectileImpact(const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	if (ATestGameMode* GameMode = World ? Cast<ATestGameMode>(World->GetGameMode()) : nullptr)
		GameMode->NotifyProjectileHit(Hit);
}

void AProjectileActor::Launch(const FVector& Position, const FVector& Direction, bool bUseStaticMesh)
{
    if (IsPendingKillPending() || !GetWorld() || (GetWorld()->GetWorldType() != EWorldType::PIE && GetWorld()->GetWorldType() != EWorldType::Game) || Direction.Dot(Direction) <= 0.0001f ||
        !IsLiveOwnedComponent(*this, ProjectileMovementComponent) || !IsLiveOwnedComponent(*this, StaticMeshComponent) ||
        !IsLiveOwnedComponent(*this, FireBallComponent) || !IsLiveOwnedComponent(*this, FireBallMeshComponent))
        return;
    ReturnToPool();
    const FVector Forward = Direction.Normalized();
    GetRootComponent()->SetWorldLocation(Position, false);
    if (bUseStaticMesh)
    {
        StaticMeshComponent->SetVisible(true);
        StaticMeshComponent->Activate();
        StaticMeshComponent->SetHiddenInGame(false);
    }
    else
    {
        FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallComponent->GetRadius() * 0.5f));
        FireBallMeshComponent->SetVisible(true);
        FireBallMeshComponent->Activate();
        FireBallMeshComponent->SetHiddenInGame(false);
        FireBallComponent->SetVisibility(true);
        FireBallComponent->Activate();
        FireBallComponent->SetHiddenInGame(false);
    }
    const UStaticMeshComponent* Visual = bUseStaticMesh ? StaticMeshComponent : FireBallMeshComponent;
    float CollisionRadius = bUseStaticMesh ? 0.5f : FireBallComponent->GetRadius() * 0.5f;
    if (const FStaticMeshData* Mesh = Visual->GetMeshData(); Mesh && !Mesh->Vertices.IsEmpty())
    {
        const FMatrix VisualToRoot = Visual->GetWorldMatrix() * GetRootComponent()->GetWorldMatrix().Inverse();
        CollisionRadius = 0.f;
        for (const FVertex& Vertex : Mesh->Vertices)
            CollisionRadius = std::max(CollisionRadius, VisualToRoot.TransformPosition(Vertex.Position).Size());
    }
    USphereComponent* Collision = CastChecked<USphereComponent>(GetRootComponent());
    Collision->SetSphereRadius(CollisionRadius);
    Collision->SetCollisionEnabled(true);
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->Velocity = Forward * ProjectileMovementComponent->InitialSpeed;
    ProjectileMovementComponent->bSimulationEnabled = true;
    ProjectileMovementComponent->Activate(true);
    RemainingLife = LifeTime;
    bInFlight = true;
    PrimaryActorTick.SetTickFunctionEnable(true);
}

void AProjectileActor::ReturnToPool()
{
    if (IsPendingKillPending()) return;
    bInFlight = false;
    CastChecked<UPrimitiveComponent>(GetRootComponent())->SetCollisionEnabled(false);
    RemainingLife = 0.0f;
    if (IsLiveOwnedComponent(*this, ProjectileMovementComponent))
    {
        ProjectileMovementComponent->Velocity = FVector::ZeroVector;
        ProjectileMovementComponent->bSimulationEnabled = false;
        ProjectileMovementComponent->Deactivate();
    }
    if (IsLiveOwnedComponent(*this, StaticMeshComponent))
    {
        StaticMeshComponent->Deactivate();
        StaticMeshComponent->SetHiddenInGame(true);
        StaticMeshComponent->SetVisible(false);
    }
    if (IsLiveOwnedComponent(*this, FireBallComponent))
    {
        FireBallComponent->Deactivate();
        FireBallComponent->SetHiddenInGame(true);
        FireBallComponent->SetVisibility(false);
    }
    if (IsLiveOwnedComponent(*this, FireBallMeshComponent))
    {
        FireBallMeshComponent->Deactivate();
        FireBallMeshComponent->SetHiddenInGame(true);
        FireBallMeshComponent->SetVisible(false);
    }
    PrimaryActorTick.SetTickFunctionEnable(false);
}

void AProjectileActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (IsPendingKillPending() || !bInFlight) return;
    RemainingLife -= DeltaTime;
    if (RemainingLife <= 0.0f || !IsLiveOwnedComponent(*this, ProjectileMovementComponent) || !ProjectileMovementComponent->bSimulationEnabled) ReturnToPool();
}
