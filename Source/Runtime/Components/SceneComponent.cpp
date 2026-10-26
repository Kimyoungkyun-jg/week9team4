#include "EnginePCH.h"
#include "Components/SceneComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
	FTransform TransformFromMatrix(const FMatrix& Matrix)
	{
		FMatrix RotationMatrix = Matrix;
		FVector Scale;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const FVector Vector(Matrix[Axis][0], Matrix[Axis][1], Matrix[Axis][2]);
			Scale[Axis] = Vector.Size();
			RotationMatrix.SetAxis(Axis, Vector.GetSafeNormal());
		}
		return FTransform(MatrixToRotator(RotationMatrix), FVector(Matrix[3][0], Matrix[3][1], Matrix[3][2]), Scale);
	}

	void UpdateChildVisibility(USceneComponent* Root, bool bPropagate, bool bVisible, bool bHidden)
	{
		TArray<USceneComponent*> Pending = Root->GetAttachChildren();
		while (!Pending.IsEmpty())
		{
			USceneComponent* Child = Pending[Pending.Num() - 1];
			Pending.RemoveLast();
			if (!Child) continue;
			if (bPropagate)
			{
				if (bVisible) Child->UActorComponent::SetVisibility(Root->IsVisible());
				if (bHidden) Child->UActorComponent::SetHiddenInGame(Root->IsHiddenInGame());
			}
			Child->MarkRenderStateDirty();
			for (USceneComponent* Descendant : Child->GetAttachChildren()) Pending.Add(Descendant);
		}
	}
}

USceneComponent::~USceneComponent()
{
	DetachOnDestruction();
}

void USceneComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (!bDestroyingHierarchy)
	{
		const auto Children = AttachChildren;
		for (USceneComponent* Child : Children)
		{
			if (!IsValid(Child) || Child->IsBeingDestroyed()) continue;
			Child->DetachFromParent(EAttachmentRule::KeepWorld);
			if (IsValid(Child) && !Child->IsBeingDestroyed())
				Child->SetupAttachment(AttachParent, EAttachmentRule::KeepWorld);
		}
	}
	DetachOnDestruction();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void USceneComponent::DetachOnDestruction()
{
	TArray<USceneComponent*> Children = AttachChildren;
	DetachFromParent();
	for (USceneComponent* Child : Children)
	{
		if (!Child || Child->AttachParent != this) continue;
		Child->AttachParent = nullptr;
		if (IsValid(Child) && !Child->IsBeingDestroyed())
		{
			Child->MarkTransformDirty();
			Child->MarkRenderStateDirty();
			Child->UpdateOverlaps();
		}
	}
	AttachChildren.Reset();
}

void USceneComponent::SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	const FVector WorldLocation = AttachParent ? AttachParent->GetWorldMatrix().TransformPosition(NewLocation) : NewLocation;
	const FRotator WorldRotation = AttachParent ? (AttachParent->GetWorldRotation().Quaternion() * NewRotation.Quaternion()).ToFRotator() : NewRotation;
	MoveComponent(WorldLocation - GetWorldLocation(), WorldRotation, false);
}

void USceneComponent::SetRelativeScale3D(const FVector& InScale)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending()) || Transform.Scale == InScale) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	Transform.Scale = InScale;
	MarkTransformDirty();
	UpdateOverlaps();
}

void USceneComponent::SetTransform(const FTransform& InTransform)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	const FTransform Desired = InTransform;
	SetRelativeLocationAndRotation(Desired.Location, Desired.Rotation);
	SetRelativeScale3D(Desired.Scale);
}

void USceneComponent::PostEditChangeProperty(const FProperty& Property)
{
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Transform") 
	{
		MarkTransformDirty();
		UpdateOverlaps();
	}
	else if (Property.Name == "bVisible" || Property.Name == "bHiddenInGame")
		UpdateChildVisibility(this, false, false, false);
}

bool USceneComponent::IsShown(bool bGameView) const
{
	for (const USceneComponent* Component = this; Component; Component = Component->AttachParent)
		if (!Component->UActorComponent::IsShown(bGameView)) return false;
	return true;
}

void USceneComponent::SetVisibility(bool bNewVisibility, EVisibilityPropagation Propagation)
{
	const bool bChanged = bVisible != bNewVisibility;
	Super::SetVisibility(bNewVisibility);
	if (bChanged || Propagation == EVisibilityPropagation::Propagate)
		UpdateChildVisibility(this, Propagation == EVisibilityPropagation::Propagate, true, false);
}

void USceneComponent::SetHiddenInGame(bool bNewHidden, EVisibilityPropagation Propagation)
{
	const bool bChanged = bHiddenInGame != bNewHidden;
	Super::SetHiddenInGame(bNewHidden);
	if (bChanged || Propagation == EVisibilityPropagation::Propagate)
		UpdateChildVisibility(this, Propagation == EVisibilityPropagation::Propagate, false, true);
}

void USceneComponent::SetupAttachment(USceneComponent* InParent, EAttachmentRule Rule)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	if (InParent && (!IsValid(InParent) || InParent->IsBeingDestroyed() ||
		(InParent->GetOwner() && InParent->GetOwner()->IsPendingKillPending()))) return;
	if (InParent == this || AttachParent == InParent) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));

	// 순환 체크
	for (USceneComponent* Parent = InParent; Parent != nullptr; Parent = Parent->AttachParent)
		if (Parent == this) return;

	const FMatrix WorldMatrix = Rule == EAttachmentRule::KeepWorld ? GetWorldMatrix() : FMatrix::Identity;
	if (AttachParent)
	{
		const int32 Index = AttachParent->AttachChildren.Find(this);
		if (Index != INDEX_NONE) AttachParent->AttachChildren.RemoveAt(Index);
	}
	AttachParent = InParent;
	if (AttachParent) AttachParent->AttachChildren.Add(this);
	if (Rule == EAttachmentRule::KeepWorld) ApplyWorldTransform(WorldMatrix);
	else if (Rule == EAttachmentRule::SnapToTarget) Transform = FTransform::Identity;
	MarkTransformDirty();
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
	UpdateOverlaps();
}

void USceneComponent::DetachFromParent(EAttachmentRule Rule)
{
	if (!AttachParent) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	const FMatrix WorldMatrix = Rule == EAttachmentRule::KeepWorld ? GetWorldMatrix() : FMatrix::Identity;
	const int32 Index = AttachParent->AttachChildren.Find(this);
	if (Index != INDEX_NONE) AttachParent->AttachChildren.RemoveAt(Index);
	AttachParent = nullptr;
	if (Rule == EAttachmentRule::KeepWorld) ApplyWorldTransform(WorldMatrix);
	MarkTransformDirty();
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
	UpdateOverlaps();
}

FRotator USceneComponent::GetWorldRotation() const
{
	if (AttachParent)
	{
		FQuat ParentQuat = AttachParent->GetWorldRotation().Quaternion();
		FQuat LocalQuat = Transform.GetOrientation();

		return (ParentQuat * LocalQuat).ToFRotator();
	}

	return Transform.Rotation;
}

FVector USceneComponent::GetWorldLocation() const
{
	FMatrix WorldMatrix = GetWorldMatrix();

	return FVector(WorldMatrix[3][0], WorldMatrix[3][1], WorldMatrix[3][2]);
}

FVector USceneComponent::GetWorldScale3D() const
{
	if (AttachParent)
	{
		FVector ParentScale = AttachParent->GetWorldScale3D();

		return FVector(
			Transform.Scale.X * ParentScale.X,
			Transform.Scale.Y * ParentScale.Y,
			Transform.Scale.Z * ParentScale.Z
		);
	}

	return Transform.Scale;
}

FMatrix USceneComponent::GetWorldMatrix() const
{
	if (!AttachParent)
	{
		return Transform.GetLocalMatrix();
	}

	const FVector WorldLocation = 
		AttachParent->GetWorldMatrix().TransformPosition(Transform.Location);

	const FQuat WorldRotation = 
		AttachParent->GetWorldRotation().Quaternion() * Transform.GetOrientation();

	const FVector WorldScale =
		Transform.Scale * AttachParent->GetWorldScale3D();

	return FTransform(WorldRotation.ToFRotator(), WorldLocation, WorldScale).GetLocalMatrix();
}

void USceneComponent::SetWorldTransform(const FMatrix& InWorldMatrix, bool bSweep)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	const FTransform Desired = TransformFromMatrix(InWorldMatrix);
	SetWorldLocationAndRotation(Desired.Location, Desired.Rotation, bSweep);
	SetWorldScale3D(Desired.Scale);
}

void USceneComponent::ApplyWorldTransform(const FMatrix& WorldMatrix)
{
	Transform = TransformFromMatrix(WorldMatrix);
	if (AttachParent)
	{
		Transform.Location = AttachParent->GetWorldMatrix().Inverse().TransformPosition(Transform.Location);
		Transform.Rotation = (AttachParent->GetWorldRotation().Quaternion().Inverse() * Transform.Rotation.Quaternion()).ToFRotator();
		const FVector ParentScale = AttachParent->GetWorldScale3D();
		for (int Axis = 0; Axis < 3; ++Axis)
			Transform.Scale[Axis] = std::abs(ParentScale[Axis]) > SMALL_NUMBER ? Transform.Scale[Axis] / ParentScale[Axis] : 0.f;
	}
}

void USceneComponent::SetWorldLocation(FVector NewLocation, bool bSweep)
{
    MoveComponent(NewLocation - GetWorldLocation(), GetWorldRotation(), bSweep);
}

void USceneComponent::SetWorldRotation(FRotator NewRotation, bool bSweep)
{
    MoveComponent(FVector::ZeroVector, NewRotation, bSweep);
}

void USceneComponent::SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep)
{
    MoveComponent(NewLocation - GetWorldLocation(), NewRotation, bSweep);
}

void USceneComponent::ApplyWorldLocationAndRotation(const FVector& Location, const FRotator& Rotation)
{
    FVector RelativeLocation = Location;
    FRotator RelativeRotation = Rotation;
    if (AttachParent)
    {
        RelativeLocation = AttachParent->GetWorldMatrix().Inverse().TransformPosition(Location);
        RelativeRotation = (AttachParent->GetWorldRotation().Quaternion().Inverse() * Rotation.Quaternion()).ToFRotator();
    }
    Transform.Location = RelativeLocation;
    Transform.Rotation = RelativeRotation;
    MarkTransformDirty();
}

void USceneComponent::SetWorldScale3D(FVector NewScale)
{
    FVector RelativeScale = NewScale;
    if (AttachParent)
    {
        const FVector ParentScale = AttachParent->GetWorldScale3D();
        for (int Axis = 0; Axis < 3; ++Axis)
            RelativeScale[Axis] = std::abs(ParentScale[Axis]) > SMALL_NUMBER ? NewScale[Axis] / ParentScale[Axis] : 0.f;
    }
    SetRelativeScale3D(RelativeScale);
}

bool USceneComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit, EMoveComponentFlags /*MoveFlags*/)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending()))
	{
		if (Hit) *Hit = FHitResult(1.f);
		return false;
	}
    const FVector Start = GetWorldLocation();
    if (Hit) { *Hit = FHitResult(1.f); Hit->Init(Start, Start + Delta); Hit->Location = Start + Delta; }
    for (int Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(Delta[Axis]) || !std::isfinite(NewRotation[Axis])) return false;
    const FRotator PreviousRotation = GetWorldRotation();
    if (Delta.SizeSquared() == 0.f && PreviousRotation.Pitch == NewRotation.Pitch && PreviousRotation.Yaw == NewRotation.Yaw && PreviousRotation.Roll == NewRotation.Roll) return true;
    UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
    ApplyWorldLocationAndRotation(Start + Delta, NewRotation);
    UpdateOverlaps();
    return true;
}

void USceneComponent::UpdateOverlaps(const TArray<UPrimitiveComponent*>* PendingOverlaps)
{
	if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	const auto Children = AttachChildren;
	for (USceneComponent* Child : Children)
	{
		if (!IsValid(this) || IsBeingDestroyed() || (GetOwner() && GetOwner()->IsPendingKillPending())) return;
		if (AttachChildren.Find(Child) != INDEX_NONE && IsValid(Child) && !Child->IsBeingDestroyed()) Child->UpdateOverlaps();
	}
}

void USceneComponent::MarkTransformDirty()
{
	if (!IsValid(this) || IsBeingDestroyed()) return;
	UWorld::FScopedObjectExecution ObjectScope(GetWorld() ? GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr));
	const auto Children = AttachChildren;
	OnTransformDirty();         
	if (!IsValid(this) || IsBeingDestroyed()) return;

	for (USceneComponent* Child : Children)
	{
		if (AttachChildren.Find(Child) != INDEX_NONE && IsValid(Child) && !Child->IsBeingDestroyed())
			Child->MarkTransformDirty();
		if (!IsValid(this) || IsBeingDestroyed()) return;
	}
}

void USceneComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // UActorComponent: Owner까지 처리됨

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* ParentObj = AttachParent;
		Record << SA_VALUE("AttachParent", ParentObj);
		AttachParent = static_cast<USceneComponent*>(ParentObj);

		int32 Num = AttachChildren.Num();
		FStructuredArchive::FArray ChildArray = Record.EnterArray("AttachChildren", Num);
		if (Ar.IsLoading())
			AttachChildren.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = AttachChildren[i];
			ChildArray.EnterElement() << Obj;
			AttachChildren[i] = static_cast<USceneComponent*>(Obj);
		}
	}
}
