#include "EnginePCH.h"
#include "PlayerStart.h"

#include "Asset/AssetManager.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"

APlayerStart::APlayerStart()
{
	SetActorEnableCollision(false);

	UCapsuleComponent* Capsule = CreateDefaultSubobject<UCapsuleComponent>("CollisionCapsule");
	Capsule->SetCollisionEnabled(false);
	SetRootComponent(Capsule);

	UBillboardComponent* Icon = CreateDefaultSubobject<UBillboardComponent>("PlayerStartIcon");
	Icon->SetupAttachment(Capsule);
	Icon->SetEditorIcon(true);
	Icon->SetHiddenInGame(true);
	Icon->SetIconSize(2.0f);
	Icon->SetHiddenInDetails(true);
	Icon->SetCastShadow(false);
	Icon->SetReceiveShadow(false);
	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Texture = UAssetManager::Get().LoadTexture("Assets/Icons/Toolbar/Play.png");
	if (Base && Texture)
	{
		UMaterial* IconMaterial = UMaterial::CreateInstance(Base);
		IconMaterial->Textures[0] = Texture;
		Icon->SetMaterial(0, IconMaterial);
	}

	UStaticMeshComponent* Arrow = CreateDefaultSubobject<UStaticMeshComponent>("PlayerStartArrow");
	Arrow->SetupAttachment(Capsule);
	Arrow->SetEditorIcon(true);
	Arrow->SetHiddenInGame(true);
	Arrow->SetHiddenInDetails(true);
	Arrow->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
	Arrow->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
	Arrow->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	Arrow->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.5f));
	Arrow->SetCastShadow(false);
	Arrow->SetReceiveShadow(false);
}
