#include "EnginePCH.h"
#include "SpringArmTestGameMode.h"
#include "PlayerPawn.h"
#include "Asset/AssetManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Render/Material.h"

ASpringArmTestGameMode::ASpringArmTestGameMode()
{
	SetDefaultPawnClass(APlayerPawn::StaticClass());
}

void ASpringArmTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	auto* Pawn = GetWorld() ? Cast<APlayerPawn>(GetWorld()->GetPlayerPawn()) : nullptr;
	if (!Pawn || !Pawn->GetCapsuleComponent() || Pawn->FindComponentByName(FName("SpringArmTestPlayerCube"))) return;

	// Configure before registration: this is a visual body, never an extra
	// movement collider or a blocker for the camera sphere sweep.
	auto* Cube = Pawn->CreateDefaultSubobject<UStaticMeshComponent>("SpringArmTestPlayerCube");
	if (!Cube) return;
	Cube->SetCollisionEnabled(false);
	Cube->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	Cube->SetGenerateOverlapEvents(false);
	auto* Capsule = Pawn->GetCapsuleComponent();
	Cube->SetupAttachment(Capsule);
	Cube->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator());
	Cube->SetRelativeScale3D(FVector(Capsule->GetUnscaledCapsuleRadius() * 1.4f,
		Capsule->GetUnscaledCapsuleRadius() * 1.4f, Capsule->GetUnscaledCapsuleHalfHeight() * 1.8f));
	Cube->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cube"));
	Cube->SetCastShadow(false);
	Cube->SetReceiveShadow(false);
	if (auto* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"))
	{
		if (auto* Material = UMaterial::CreateInstance(Base))
		{
			Material->BaseColor = FVector4(0.15f, 0.9f, 0.25f, 1.0f);
			Cube->SetMaterial(0, Material);
		}
	}
	Cube->RegisterComponent();
}
