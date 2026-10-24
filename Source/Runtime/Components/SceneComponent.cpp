#include "EnginePCH.h"
#include "Components/SceneComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
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
	TArray<USceneComponent*> Children = AttachChildren;
	DetachFromParent();
	for (USceneComponent* Child : Children)
	{
		Child->AttachParent = nullptr;
	}
	AttachChildren.Reset();

	DetachFromParent();
}

void USceneComponent::SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation)
{
	Transform.Location = NewLocation;
	Transform.Rotation = NewRotation;
	MarkTransformDirty();
}

void USceneComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Transform") 
		MarkTransformDirty();
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
	if (InParent == this || AttachParent == InParent) return;

	// 순환 체크
	for (USceneComponent* Parent = InParent; Parent != nullptr; Parent = Parent->AttachParent)
		if (Parent == this) return;

	if (Rule == EAttachmentRule::KeepWorld)
	{
		FMatrix WorldMatrix = GetWorldMatrix();
		DetachFromParent(Rule);
		AttachParent = InParent;
		if (AttachParent)
		{
			AttachParent->AttachChildren.Add(this);
		}
		SetWorldTransform(WorldMatrix, false);
	}
	else
	{
		DetachFromParent(Rule);
		AttachParent = InParent;
		if (AttachParent)
		{
			AttachParent->AttachChildren.Add(this);
		}
	}

	if (Rule == EAttachmentRule::SnapToTarget)
	{
		SetTransform(FTransform::Identity);
	}

	MarkTransformDirty();
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
}

void USceneComponent::DetachFromParent(EAttachmentRule Rule)
{
	if (!AttachParent) return;

	if (Rule == EAttachmentRule::KeepWorld)
	{
		FMatrix WorldMatrix = GetWorldMatrix();
		TArray<USceneComponent*>& Siblings = AttachParent->AttachChildren;
		for (uint32 i = 0;i < Siblings.Num(); ++i)
		{
			if (Siblings[i] == this)
			{
				Siblings.RemoveAt(i, 1);
				break;
			}
		}
		AttachParent = nullptr;
		SetWorldTransform(WorldMatrix, false);
	}
	else
	{
		TArray<USceneComponent*>& Siblings = AttachParent->AttachChildren;
		for (uint32 i = 0;i < Siblings.Num(); ++i)
		{
			if (Siblings[i] == this)
			{
				Siblings.RemoveAt(i, 1);
				break;
			}
		}
		AttachParent = nullptr;
	}
	
	MarkTransformDirty();
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
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
    FMatrix RotationMatrix = InWorldMatrix;
    FVector Scale;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        FVector Vector(InWorldMatrix[Axis][0], InWorldMatrix[Axis][1], InWorldMatrix[Axis][2]);
        Scale[Axis] = Vector.Size();
        RotationMatrix.SetAxis(Axis, Vector.GetSafeNormal());
    }
    SetWorldScale3D(Scale);
    SetWorldLocationAndRotation(FVector(InWorldMatrix[3][0], InWorldMatrix[3][1], InWorldMatrix[3][2]), MatrixToRotator(RotationMatrix), bSweep);
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

bool USceneComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
    const FVector Start = GetWorldLocation();
    if (Hit) { *Hit = FHitResult(1.f); Hit->Init(Start, Start + Delta); Hit->Location = Start + Delta; }
    for (int Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(Delta[Axis]) || !std::isfinite(NewRotation[Axis])) return false;
    ApplyWorldLocationAndRotation(Start + Delta, NewRotation);
    return true;
}

void USceneComponent::MarkTransformDirty()
{
	OnTransformDirty();         

	for (USceneComponent* Child : AttachChildren)
		Child->MarkTransformDirty();      // 부모가 움직이면 자식의 월드 행렬도 바뀐다
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



