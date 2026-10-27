#include "EnginePCH.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include <algorithm>

void APlayerCameraManager::Initialize(APlayerController* InOwner)
{
	PCOwner = InOwner;
}

void APlayerCameraManager::SetViewTarget(AActor* InTarget)
{
	if (InTarget && (GUObjectArray.Find(InTarget) == INDEX_NONE || !IsValid(InTarget) || InTarget->IsActorBeingDestroyed() ||
		InTarget->GetWorld() != GetWorld())) return;
	ViewTarget = InTarget;
}

AActor* APlayerCameraManager::GetViewTarget() const
{
	return ViewTarget && GUObjectArray.Find(ViewTarget) != INDEX_NONE &&
		IsValid(ViewTarget) && !ViewTarget->IsActorBeingDestroyed() ? ViewTarget : nullptr;
}

UCameraModifier* APlayerCameraManager::AddNewCameraModifier(UClass* ModifierClass)
{
	if (IsPendingKillPending() || !ModifierClass || !ModifierClass->IsChildOf(UCameraModifier::StaticClass()) ||
		!ModifierClass->Constructor) return nullptr;
	UCameraModifier* Modifier = NewObject<UCameraModifier>(this, ModifierClass, NAME_None, EObjectFlags::RF_Transient);
	if (Modifier) Modifiers.Add(Modifier);
	return Modifier;
}

void APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier)
{
	const int32 Index = Modifiers.Find(Modifier);
	if (Index == INDEX_NONE) return;
	Modifiers.RemoveAt(Index);
	if (IsValid(Modifier) && GetWorld())
	{
		Modifier->MarkAsGarbage();
		GetWorld()->QueueObjectDeletion(Modifier);
	}
}

void APlayerCameraManager::ClearCameraModifiers()
{
	while (!Modifiers.IsEmpty()) RemoveCameraModifier(Modifiers.Last());
}

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	if (IsPendingKillPending()) return;
	AActor* Target = GetViewTarget();
	if (!Target && PCOwner && GUObjectArray.Find(PCOwner) != INDEX_NONE &&
		IsValid(PCOwner) && !PCOwner->IsActorBeingDestroyed())
	{
		Target = PCOwner->GetPawn();
		if (!IsValid(Target) || Target->IsActorBeingDestroyed()) Target = nullptr;
	}
	if (!Target && GetWorld())
	{
		Target = GetWorld()->GetMainCamera();
		if (!IsValid(Target) || Target->IsActorBeingDestroyed()) Target = nullptr;
	}
	if (!Target)
	{
		ViewTarget = nullptr;
		bHasCameraCache = false;
		return;
	}
	ViewTarget = Target;

	FMinimalViewInfo NewPOV;
	Target->CalcCamera(DeltaTime, NewPOV);
	if (IsPendingKillPending()) return;

	TArray<UCameraModifier*> OrderedModifiers;
	for (UCameraModifier* Modifier : Modifiers)
		if (IsValid(Modifier) && Modifier->IsActive()) OrderedModifiers.Add(Modifier);
	std::stable_sort(OrderedModifiers.begin(), OrderedModifiers.end(),
		[](const UCameraModifier* A, const UCameraModifier* B) { return A->GetPriority() < B->GetPriority(); });
	for (UCameraModifier* Modifier : OrderedModifiers)
	{
		if (!IsValid(Modifier) || Modifiers.Find(Modifier) == INDEX_NONE || !Modifier->IsActive()) continue;
		const bool bStop = Modifier->ModifyCamera(DeltaTime, NewPOV);
		if (IsPendingKillPending()) return;
		if (bStop) break;
	}
	CameraCachePOV = NewPOV;
	bHasCameraCache = true;
}

void APlayerCameraManager::EndPlay()
{
	ClearCameraModifiers();
	bHasCameraCache = false;
	ViewTarget = nullptr;
	PCOwner = nullptr;
	Super::EndPlay();
}
