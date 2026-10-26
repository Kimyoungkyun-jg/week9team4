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
	if (IsPendingKillPending()) return;
	UWorld* PawnWorld = GetWorld();
	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game)) return;
	for (AProjectileActor*& Projectile : Projectiles)
		if (!Projectile) Projectile = PawnWorld->SpawnActor<AProjectileActor>();
}

void ATestPawn::FireProjectile()
{
	UWorld* PawnWorld = GetWorld();
	UCameraComponent* Camera = GetCameraComponent();
	if (IsPendingKillPending() || !PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game) ||
		GetComponents().Find(Camera) == INDEX_NONE || !IsValid(Camera) || Camera->IsBeingDestroyed()) return;
	if (bUseStaticMeshProjectiles && !ProjectileMesh) return;
	auto IsLiveProjectile = [](AProjectileActor* Projectile)
	{
		return Projectile && GUObjectArray.Find(Projectile) != INDEX_NONE && IsValid(Projectile) &&
			Projectile->IsA<AProjectileActor>() && !Projectile->IsActorBeingDestroyed();
	};
	int32 SelectedIndex = -1;
	for (int32 Offset = 0; Offset < ProjectilePoolSize; ++Offset)
	{
		const int32 Index = (NextProjectileIndex + Offset) % ProjectilePoolSize;
		if (!IsLiveProjectile(Projectiles[Index]) || !Projectiles[Index]->IsInFlight()) { SelectedIndex = Index; break; }
	}
	if (SelectedIndex < 0) SelectedIndex = NextProjectileIndex;
	AProjectileActor*& Projectile = Projectiles[SelectedIndex];
	if (!IsLiveProjectile(Projectile)) Projectile = PawnWorld->SpawnActor<AProjectileActor>();
	if (!Projectile)
	{
		HTR_LOG(Warning, "FireProjectile: projectile pool slot {} is empty", SelectedIndex);
		return;
	}
	const FVector Forward = Camera->GetWorldRotation().Quaternion().GetForwardVector().Normalized();
	const FVector Position = Camera->GetWorldLocation() + Forward * 2.0f;
	if (bUseStaticMeshProjectiles)
	{
		auto* MeshComponent = Projectile->GetStaticMeshComponent();
		if (Projectile->GetComponents().Find(MeshComponent) == INDEX_NONE || !IsValid(MeshComponent) || MeshComponent->IsBeingDestroyed()) return;
		MeshComponent->SetStaticMesh(ProjectileMesh);
	}
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
