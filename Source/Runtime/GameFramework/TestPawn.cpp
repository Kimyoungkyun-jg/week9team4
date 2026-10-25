#include "EnginePCH.h"
#include "GameFramework/TestPawn.h"

#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Components/LuaScriptComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor/ProjectileActor.h"

ATestPawn::ATestPawn()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Hitori/Hitori.obj"));
	SetRootComponent(StaticMeshComponent);

	UCameraComponent* Camera = GetCameraComponent();
	Camera->SetupAttachment(StaticMeshComponent);
	Camera->SetRelativeLocation({-5.0f, 0.0f, 8.0f});
	Camera->SetRelativeRotation({45.0f, 0.0f, 0.0f});

	ULuaScriptComponent* ScriptComponent = CreateDefaultSubobject<ULuaScriptComponent>("LuaScriptComponent");
	ScriptComponent->SetScriptPath("Assets/Scripts/player.lua");
	ProjectileMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
}

void ATestPawn::BeginPlay()
{
	Super::BeginPlay();
	UWorld* PawnWorld = GetWorld();
	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game)) return;
	for (AProjectileActor*& Projectile : Projectiles)
		if (!Projectile) Projectile = PawnWorld->SpawnActor<AProjectileActor>();
}

void ATestPawn::FireProjectile()
{
	UWorld* PawnWorld = GetWorld();
	UCameraComponent* Camera = GetCameraComponent();
	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game) || !Camera) return;
	if (bUseStaticMeshProjectiles && !ProjectileMesh) return;
	int32 SelectedIndex = -1;
	for (int32 Offset = 0; Offset < ProjectilePoolSize; ++Offset)
	{
		const int32 Index = (NextProjectileIndex + Offset) % ProjectilePoolSize;
		if (Projectiles[Index] && !Projectiles[Index]->IsInFlight()) { SelectedIndex = Index; break; }
	}
	if (SelectedIndex < 0) SelectedIndex = NextProjectileIndex;
	AProjectileActor* Projectile = Projectiles[SelectedIndex];
	if (!Projectile)
	{
		HTR_LOG(Warning, "FireProjectile: projectile pool slot {} is empty", SelectedIndex);
		return;
	}
	const FVector Forward = Camera->GetWorldRotation().Quaternion().GetForwardVector().Normalized();
	const FVector Position = Camera->GetWorldLocation() + Forward * 2.0f;
	if (bUseStaticMeshProjectiles) Projectile->GetStaticMeshComponent()->SetStaticMesh(ProjectileMesh);
	Projectile->Launch(Position, Forward, bUseStaticMeshProjectiles);
	if (Projectile->IsInFlight())
	{
		HTR_LOG(Info, "FireProjectile: launched slot {} ({})", SelectedIndex,
			bUseStaticMeshProjectiles ? "StaticMesh" : "FireBall");
		NextProjectileIndex = (SelectedIndex + 1) % ProjectilePoolSize;
	}
	else
		HTR_LOG(Warning, "FireProjectile: Launch did not start slot {}", SelectedIndex);
}
