#include "EnginePCH.h"
#include "FireBallActor.h"
#include "Components/SphereComponent.h"

AFireBallActor::AFireBallActor()
{
    USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>("Collision");
    Collision->SetCollisionObjectType(ECollisionChannel::Projectile);
    SetRootComponent(Collision);
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("UStaticMeshComponent");
	StaticMeshComponent->SetupAttachment(GetRootComponent());
    StaticMeshComponent->SetCollisionEnabled(false);

	FireballComponent = CreateDefaultSubobject<UFireBallComponent>("UFireBallComponent");
	FireballComponent->SetupAttachment(GetRootComponent());

	RotatingMovementComponent = CreateDefaultSubobject<URotatingMovementComponent>("URotatingMovementComponent");	
	RotatingMovementComponent->RotationRate = FRotator(0.0f, 100.0f, 100.0f);
	RotatingMovementComponent->PivotTranslation = FVector(0.0f, 0.0f, 0.0f);

	ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>("UProjectileMovementComponent");
	ProjectileMovementComponent->InitialSpeed = 1.0f;
	ProjectileMovementComponent->MaxSpeed = 5.0f;
	ProjectileMovementComponent->Velocity = FVector(5.0f, 0.0f, 0.0f);
	ProjectileMovementComponent->bShouldBounce = false;
	ProjectileMovementComponent->bInitialVelocityInLocalSpace = true;
	ProjectileMovementComponent->ProjectileGravityScale = 0.0f;
}