#include "EnginePCH.h"
#include "FlashlightActor.h"
#include "FlashlightCollision.h"

#include "Asset/AssetManager.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Collision/Ray.h"
#include "Engine/World.h"
#include "Render/Material.h"
#include <string>

namespace
{
	void UpdateCapsuleBeam(UStaticMeshComponent* Body, UStaticMeshComponent* Front, UStaticMeshComponent* Back,
		const FVector& Origin, float Length, float Radius)
	{
		Body->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		Body->SetRelativeScale3D(FVector(Radius, Radius, Length - 2.0f * Radius));
		Body->SetRelativeLocation(Origin + FVector(Length * 0.5f, 0.0f, 0.0f));
		Back->SetRelativeScale3D(FVector(Radius));
		Front->SetRelativeScale3D(FVector(Radius));
		Back->SetRelativeLocation(Origin + FVector(Radius, 0.0f, 0.0f));
		Front->SetRelativeLocation(Origin + FVector(Length - Radius, 0.0f, 0.0f));
	}
	void SetCapsuleBeamVisible(UStaticMeshComponent* Body, UStaticMeshComponent* Front, UStaticMeshComponent* Back,
		float Length, float Radius, bool bVisible)
	{
		bVisible = bVisible && Length > 0.0f;
		Body->SetVisibility(bVisible && Length > 2.0f * Radius);
		Front->SetVisibility(bVisible);
		Back->SetVisibility(bVisible);
	}
}

AFlashlightActor::AFlashlightActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FlashlightMesh");
	MeshComponent->SetupAttachment(GetRootComponent());
	MeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Flashlight/Flashlight.obj"));
	for (int32 Slot = 0; Slot < MeshComponent->GetNumMaterials(); ++Slot)
	{
		if (UMaterial* Material = UMaterial::CreateInstance(MeshComponent->GetMaterial(Slot)))
		{
			Material->ShadingModel = EMaterialShadingModel::Unlit;
			MeshComponent->SetMaterial(Slot, Material);
		}
	}
	MeshComponent->SetRelativeScale3D({0.2f, 0.2f, 0.2f});
	MeshComponent->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetCollisionEnabled(false);
	SetFlags(EObjectFlags::RF_Transient);
	// Beam visuals follow the flashlight; its owned collision capsule is moved
	// independently so parent movement cannot skip its translation sweep.
	UMaterial* BeamMaterial = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
	if (BeamMaterial)
	{
		// Unlit white texture, alpha 1.
		BeamMaterial->PSOType = EPSOType::StaticMesh_Translucent;
		BeamMaterial->BlendState = EBlendState::AlphaBlend;
	}
	auto ConfigureBeamMesh = [&](UStaticMeshComponent* Mesh, const char* Asset, USceneComponent* Parent)
	{
		Mesh->SetupAttachment(Parent);
		Mesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>(Asset));
		Mesh->SetCollisionEnabled(false);
		Mesh->SetCastShadow(false);
		Mesh->SetReceiveShadow(false);
		Mesh->SetReceiveSpecular(false);
		Mesh->SetVisibility(false);
		if (BeamMaterial) Mesh->SetMaterial(0, BeamMaterial);
	};
	AttackBeamBody = CreateDefaultSubobject<UStaticMeshComponent>("AttackBeamBody");
	AttackBeamFront = CreateDefaultSubobject<UStaticMeshComponent>("AttackBeamFront");
	AttackBeamBack = CreateDefaultSubobject<UStaticMeshComponent>("AttackBeamBack");
	ConfigureBeamMesh(AttackBeamBody, "Cylinder", GetRootComponent());
	ConfigureBeamMesh(AttackBeamFront, "Sphere", GetRootComponent());
	ConfigureBeamMesh(AttackBeamBack, "Sphere", GetRootComponent());
	for (int32 Index = 0; Index < AttackTrailCount; ++Index)
	{
		auto& Trail = AttackTrails[Index];
		const FString Prefix = "AttackTrail" + std::to_string(Index + 1);
		Trail.Root = CreateDefaultSubobject<USceneComponent>(FName(Prefix));
		Trail.Root->SetupAttachment(GetRootComponent());
		Trail.Body = CreateDefaultSubobject<UStaticMeshComponent>(FName(Prefix + "Body"));
		Trail.Front = CreateDefaultSubobject<UStaticMeshComponent>(FName(Prefix + "Front"));
		Trail.Back = CreateDefaultSubobject<UStaticMeshComponent>(FName(Prefix + "Back"));
		ConfigureBeamMesh(Trail.Body, "Cylinder", Trail.Root);
		ConfigureBeamMesh(Trail.Front, "Sphere", Trail.Root);
		ConfigureBeamMesh(Trail.Back, "Sphere", Trail.Root);
	}

	SpotLightComponent = CreateDefaultSubobject<USpotLightComponent>("FlashlightSpotLight");
	SpotLightComponent->SetupAttachment(GetRootComponent());
	SpotLightComponent->SetRelativeLocation({0.2f, 0.0f, 0.0f});
	SpotLightComponent->SetInnerConeAngle(20.0f);
	SpotLightComponent->SetOuterConeAngle(35.0f);
	SpotLightComponent->SetAttenuationRadius(15.0f);
	AttackCapsule = CreateDefaultSubobject<UCapsuleComponent>("AttackCapsule");
	// Deliberately no SetupAttachment: ownership does not imply transform inheritance.
	AttackCapsule->SetCollisionEnabled(false);
	AttackCapsule->SetCollisionObjectType(FlashlightAttackChannel);
	AttackCapsule->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	AttackCapsule->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
	AttackCapsule->SetGenerateOverlapEvents(true);
	AttackCapsule->SetVisibility(false);
	UpdateLightIntensity();
}

void AFlashlightActor::BeginPlay()
{
	Super::BeginPlay();
	SetGauge(Gauge);
}

void AFlashlightActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (DeltaTime > 0.0f && Gauge > 0.0f)
		SetGauge(Gauge - std::max(0.0f, DrainRatePerSecond) * DeltaTime);
}

void AFlashlightActor::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	DrainRatePerSecond = std::max(0.0f, DrainRatePerSecond);
	MaxLightIntensity = std::max(0.0f, MaxLightIntensity);
	SetGauge(Gauge);
}

void AFlashlightActor::SetDrainRatePerSecond(float InRate)
{
	DrainRatePerSecond = std::max(0.0f, InRate);
}

void AFlashlightActor::SetGauge(float NewGauge)
{
	Gauge = FMath::Clamp(NewGauge, 0.0f, MaxGauge);
	UpdateLightIntensity();
}

void AFlashlightActor::AddGauge(float Amount)
{
	if (Amount > 0.0f) SetGauge(Gauge + Amount);
}

void AFlashlightActor::UpdateLightIntensity()
{
	if (SpotLightComponent)
		SpotLightComponent->SetIntensity(MaxLightIntensity * (Gauge / MaxGauge) * AttackBrightnessMultiplier);
}

bool AFlashlightActor::TryConsumeGauge(float Amount)
{
	if (Amount < 0.0f || Gauge < Amount) return false;
	SetGauge(Gauge - Amount);
	return true;
}

void AFlashlightActor::UpdateAttackBeam(float MaxLength, float Radius, const AActor* IgnoreActor)
{
	float Length = std::max(0.0f, MaxLength);
	const FVector Origin = SpotLightComponent->GetWorldLocation();
	const FRotator Rotation = SpotLightComponent->GetWorldRotation();
	const FVector Forward = Rotation.Quaternion().GetForwardVector();
	FHitResult WallHit;
	// Map walls block Pawn; ghost capsules overlap or ignore it.
	if (GetWorld()->LineTraceByChannel(FRay{Origin, Forward}, Length, ECollisionChannel::Pawn, WallHit, IgnoreActor))
		Length *= WallHit.Time;
	AttackBeamLength = Length;
	Radius = AttackBeamRadius = std::clamp(Radius, 0.0f, Length * 0.5f);
	if (Length <= 0.0f)
	{
		AttackCapsule->SetCollisionEnabled(false);
		return;
	}
	UpdateAttackBeamMeshes(Length, Radius);
	// Resize without notifying at the previous pose. On the first attack frame
	// place the disabled capsule directly; subsequent frames sweep from its last pose.
	const bool bSweep = AttackCapsule->IsCollisionEnabled();
	AttackCapsule->SetCapsuleSize(Radius, Length * 0.5f, false);
	AttackCapsule->SetWorldLocationAndRotation(Origin + Forward * (Length * 0.5f),
		FRotator(Rotation.Pitch + 90.0f, Rotation.Yaw, 0.0f), bSweep);
	if (!bSweep) AttackCapsule->SetCollisionEnabled(true);
	else if (AttackCapsule->IsCollisionEnabled()) AttackCapsule->UpdateOverlaps();
}

void AFlashlightActor::UpdateAttackBeamMeshes(float Length, float Radius)
{
	UpdateCapsuleBeam(AttackBeamBody, AttackBeamFront, AttackBeamBack, SpotLightComponent->GetRelativeLocation(), Length, Radius);
}

void AFlashlightActor::UpdateAttackTrail(int32 Index, const FTransform& CameraPose, float MaxLength, float Radius, const AActor* IgnoreActor)
{
	if (!bAttackTrailsEnabled) return;
	const auto* Camera = GetRootComponent()->GetAttachParent();
	if (!Camera) return;
	auto& Trail = AttackTrails[Index];
	const FQuat PoseRotation = CameraPose.Rotation.Quaternion();
	const FVector Origin = Camera->GetWorldMatrix().TransformPosition(
		CameraPose.Location + PoseRotation.RotateVector(SpotLightComponent->GetRelativeLocation()));
	const FQuat WorldRotation = Camera->GetWorldRotation().Quaternion() * PoseRotation *
		SpotLightComponent->GetRelativeRotation().Quaternion();
	float Length = std::max(0.0f, MaxLength);
	FHitResult WallHit;
	if (GetWorld()->LineTraceByChannel(FRay{Origin, WorldRotation.GetForwardVector()}, Length,
		ECollisionChannel::Pawn, WallHit, IgnoreActor)) Length *= WallHit.Time;
	Radius = std::clamp(Radius, 0.0f, Length * 0.5f);
	SetCapsuleBeamVisible(Trail.Body, Trail.Front, Trail.Back, Length, Radius, true);
	if (Length <= 0.0f) return;
	Trail.Root->SetWorldLocationAndRotation(Origin, WorldRotation.ToFRotator(), false);
	UpdateCapsuleBeam(Trail.Body, Trail.Front, Trail.Back, FVector::ZeroVector, Length, Radius);
}

void AFlashlightActor::SetAttackTrailsEnabled(bool bEnabled)
{
	bAttackTrailsEnabled = bEnabled;
	if (!bEnabled)
		for (auto& Trail : AttackTrails)
			SetCapsuleBeamVisible(Trail.Body, Trail.Front, Trail.Back, 0.0f, 0.0f, false);
}

void AFlashlightActor::SetAttackPresentation(float BrightnessMultiplier, bool bShowAttackBeam)
{
	AttackBrightnessMultiplier = BrightnessMultiplier;
	SetCapsuleBeamVisible(AttackBeamBody, AttackBeamFront, AttackBeamBack, AttackBeamLength, AttackBeamRadius, bShowAttackBeam);
	if (!bShowAttackBeam) SetAttackTrailsEnabled(false);
	UpdateLightIntensity();
}
