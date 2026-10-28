#pragma once

#include "GameFramework/DefaultPawn.h"
#include <array>

class UStaticMesh;
class UStaticMeshComponent;
class AProjectileActor;

class ATestPawn : public ADefaultPawn
{
	DECLARE_CLASS(ATestPawn, ADefaultPawn)
	REFLECT_START(ATestPawn)
		PROPERTY(bUseStaticMeshProjectiles)
		PROPERTY(ProjectileMesh)
	REFLECT_END()
public:
	ATestPawn();
	void BeginPlay() override;
	void FireProjectile();
	void PrimaryAction() override { FireProjectile(); }

	static constexpr int32 ProjectilePoolSize = 16;
	const std::array<AProjectileActor*, ProjectilePoolSize>& GetProjectiles() const { return Projectiles; }
	bool UsesStaticMeshProjectiles() const { return bUseStaticMeshProjectiles; }
	void SetUseStaticMeshProjectiles(bool Value) { bUseStaticMeshProjectiles = Value; }
	void SetProjectileMesh(UStaticMesh* Mesh) { ProjectileMesh = Mesh; }
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	UStaticMesh* GetProjectileMesh() const { return ProjectileMesh; }
private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	bool bUseStaticMeshProjectiles = false;
	UStaticMesh* ProjectileMesh = nullptr;
	std::array<AProjectileActor*, ProjectilePoolSize> Projectiles{};
	int32 NextProjectileIndex = 0;
};
