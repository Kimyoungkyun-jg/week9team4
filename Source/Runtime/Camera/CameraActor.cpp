#include "EnginePCH.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PrimitiveSceneProxy.h"

#include "../Components/ActorComponent.h"

// 카메라 컴포넌트와 편집용 시네 카메라 메시를 생성해 루트에 연결
ACameraActor::ACameraActor()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetExternalInputManaged(true);
	SetRootComponent(CameraComponent);

	UStaticMeshComponent* CineMesh = CreateDefaultSubobject<UStaticMeshComponent>("CineCameraMesh");
	CineMesh->SetupAttachment(CameraComponent);
	CineMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Camera/SM_CineCam.obj"));
	CineMesh->SetEditorIcon(true);
	CineMesh->SetHiddenInGame(true);
	CineMesh->SetCastShadow(false);
	CineMesh->SetReceiveShadow(false);
}

void ACameraActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();
	if (!Ar.IsLoading() || Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate)) return;
	// 이전 씬 파일에 저장된 편집용 아이콘과 화살표를 제거한다.
	for (const char* Name : { "CameraIcon", "CameraArrow" })
		if (UActorComponent* OldComponent = FindComponentByName(FName(Name)))
			OldComponent->DestroyComponent();
}

void ACameraActor::BeginPlay()
{
	Super::BeginPlay();
	if (CameraComponent)
	{
		CameraComponent->SetExternalInputManaged(true);
	}
}

bool ACameraActor::ShouldHideEditorMeshFromView(const UStaticMeshComponent* Mesh, const FVector& ViewLocation) const
{
	if (!Mesh || Mesh->GetOwner() != this || !Mesh->IsEditorIcon()) return false;
	if ((GetActorLocation() - ViewLocation).SizeSquared() < 0.01f) return true;
	const FPrimitiveSceneProxy* Proxy = Mesh->GetSceneProxy();
	if (!Proxy) return false;
	const FAABB& Bounds = Proxy->GetBounds();
	const FVector Delta = ViewLocation - Bounds.Center;
	return fabsf(Delta.X) <= Bounds.Extent.X && fabsf(Delta.Y) <= Bounds.Extent.Y && fabsf(Delta.Z) <= Bounds.Extent.Z;
}
