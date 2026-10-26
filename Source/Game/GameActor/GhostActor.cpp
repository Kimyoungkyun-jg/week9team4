#include "EnginePCH.h"
#include "GhostActor.h"
#include "PacmanGameMode.h"
#include "PlayerPawn.h"

#include "Asset/AssetManager.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include <cmath>

AGhostActor::AGhostActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(true);

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	CapsuleComponent->SetupAttachment(GetRootComponent());
	CapsuleComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.5f));
	CapsuleComponent->SetCapsuleSize(0.45f, 0.5f);
	CapsuleComponent->SetCollisionEnabled(true);
	CapsuleComponent->SetCollisionObjectType(ECollisionChannel::WorldDynamic);
	CapsuleComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	CapsuleComponent->SetCollisionResponseToChannel(ECollisionChannel::Pawn, ECollisionResponse::Overlap);
	CapsuleComponent->SetGenerateOverlapEvents(true);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>("BodyMesh");
	BodyMesh->SetupAttachment(GetRootComponent());
	BodyMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Ghost/Ghost_Body.obj"));
	BodyMesh->SetRelativeScale3D(FVector(0.4f));
	BodyMesh->SetCollisionEnabled(false);
	BodyMesh->SetReceiveSpecular(false);

	EyesMesh = CreateDefaultSubobject<UStaticMeshComponent>("EyesMesh");
	EyesMesh->SetupAttachment(GetRootComponent());
	EyesMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Ghost/Ghost_Eyes.obj"));
	EyesMesh->SetRelativeScale3D(FVector(0.4f));
	EyesMesh->SetCollisionEnabled(false);
	EyesMesh->SetReceiveSpecular(false);
}

void AGhostActor::BeginPlay()
{
	Super::BeginPlay();
	InitialLocation = GetActorLocation();
	CapsuleComponent->OnComponentBeginOverlap.AddRaw(this, &AGhostActor::OnOverlapBegin);
	CapsuleComponent->UpdateOverlaps();
}

void AGhostActor::OnOverlapBegin(UPrimitiveComponent*, UPrimitiveComponent* OtherComponent)
{
	APlayerPawn* Player = OtherComponent ? Cast<APlayerPawn>(OtherComponent->GetOwner()) : nullptr;
	if (!Player || OtherComponent != Player->GetCollisionComponent()) return;
	if (UWorld* World = GetWorld())
		if (APacmanGameMode* Mode = Cast<APacmanGameMode>(World->GetGameMode()))
			Mode->StartGhostHit(this, Player);
}

void AGhostActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RunningTime += DeltaTime;
	const float DeltaZ = std::sinf(RunningTime * BobbingSpeed) * BobbingHeight;
	SetActorLocation(FVector(InitialLocation.X, InitialLocation.Y, InitialLocation.Z + DeltaZ));

	FRotator BodyRotation = BodyMesh->GetRelativeRotation();
	BodyRotation.Yaw += BodyRotationSpeed * DeltaTime;
	BodyMesh->SetRelativeRotation(BodyRotation);
}

void AGhostActor::ChangeEye()
{
	if (bEyeChanged || !EyesMesh) return;

	int32 NewEyeSlot = -1;
	int32 NewPupilSlot = -1;
	for (int32 Slot = 0; Slot < EyesMesh->GetNumMaterials(); ++Slot)
	{
		const FString Name = EyesMesh->GetMaterialSlotName(Slot);
		if (Name == "Ghost_Eyes") NewEyeSlot = Slot;
		else if (Name == "Ghost_Pupils") NewPupilSlot = Slot;
	}
	if (NewEyeSlot < 0 || NewPupilSlot < 0) return;

	UTexture2D* White = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
	UTexture2D* FlatNormal = UAssetManager::GetAssetByPath<UTexture2D>("FlatNormalTexture");
	if (!White || !FlatNormal) return;

	auto ConfigureOverride = [this, White, FlatNormal](UMaterial*& Override, int32 Slot, const FVector4& Color) -> bool
	{
		if (!Override)
		{
			UMaterial* Source = EyesMesh->GetMaterial(Slot);
			if (!Source) Source = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			Override = UMaterial::CreateInstance(Source);
		}
		if (!Override) return false;
		Override->Textures.SetNum(2);
		Override->Textures[0] = White;
		Override->Textures[1] = FlatNormal;
		Override->BaseColor = Color;
		Override->Shininess = 32.0f;
		Override->UVScrollSpeed = FVector2(0.0f, 0.0f);
		Override->SamplerState = ESamplerState::LinearClamp;
		Override->BlendState = EBlendState::Opaque;
		return true;
	};
	if (!ConfigureOverride(ChangedEyeOverride, NewEyeSlot, FVector4(0.0f, 0.0f, 0.0f, 1.0f)) ||
		!ConfigureOverride(ChangedPupilOverride, NewPupilSlot, FVector4(1.0f, 0.0f, 0.0f, 1.0f))) return;

	PreviousEyeOverride = EyesMesh->GetOverrideMaterial(NewEyeSlot);
	PreviousPupilOverride = EyesMesh->GetOverrideMaterial(NewPupilSlot);
	EyeSlot = NewEyeSlot;
	PupilSlot = NewPupilSlot;
	EyesMesh->SetMaterial(EyeSlot, ChangedEyeOverride);
	EyesMesh->SetMaterial(PupilSlot, ChangedPupilOverride);
	bEyeChanged = true;
}

void AGhostActor::RestoreEye()
{
	if (!bEyeChanged || !EyesMesh) return;
	EyesMesh->SetMaterial(EyeSlot, PreviousEyeOverride);
	EyesMesh->SetMaterial(PupilSlot, PreviousPupilOverride);
	PreviousEyeOverride = nullptr;
	PreviousPupilOverride = nullptr;
	EyeSlot = PupilSlot = -1;
	bEyeChanged = false;
}
