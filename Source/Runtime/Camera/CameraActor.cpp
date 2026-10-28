#include "EnginePCH.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"

#include "../Components/ActorComponent.h"

// 카메라 컴포넌트와 에디터 표시용 컴포넌트를 생성해 루트에 연결
ACameraActor::ACameraActor()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetExternalInputManaged(true);
	SetRootComponent(CameraComponent);

	UBillboardComponent* Icon = CreateDefaultSubobject<UBillboardComponent>("CameraIcon");
	Icon->SetupAttachment(CameraComponent);
	Icon->SetEditorIcon(true);
	Icon->SetHiddenInGame(true);
	Icon->SetIconSize(1.5f);
	Icon->SetHiddenInDetails(true);
	Icon->SetCastShadow(false);
	Icon->SetReceiveShadow(false);

	UStaticMeshComponent* Arrow = CreateDefaultSubobject<UStaticMeshComponent>("CameraArrow");
	Arrow->SetupAttachment(CameraComponent);
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

void ACameraActor::BeginPlay()
{
	Super::BeginPlay();
	if (CameraComponent)
	{
		CameraComponent->SetExternalInputManaged(true);
	}
}
