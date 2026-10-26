#pragma once

#include "../Math/Transform.h"
#include "ActorComponent.h"
#include "Math/Box.h"
#include "Collision/HitResult.h"
#include "Lua/LuaMacros.h"

class UPrimitiveComponent;

// MoveComponent의 비트 플래그. | 연산으로 조합하며, 미구현 값은 전달해도 효과가 없다.
enum EMoveComponentFlags : uint8
{
	// [구현] 추가 옵션이 없는 기본 동작(값 0이며, 켜는 비트가 아님).
	// - bSweep=false이면 블로킹 검사 없이 이동하고, Transform이 바뀌면 오버랩을 갱신한다.
	// - bSweep=true인 병진 이동은 스윕으로 검사하며, 같은 액터 소유 컴포넌트는 제외한다.
	// - 검출된 초기 침투는 ImpactNormal · 이동 단위 방향 > 0인 경우에만 무시한다.
	//   조금만 빠져나가 최종 위치에 침투가 남아 있어도 허용한다. 내적 <= 0이면 블로킹 후보로 남긴다.
	//   여기서 후보란 최종 반환/알림할 대표 BlockingHit을 고르기 위한 검사 결과이다.
	//   단순 접촉(bStartPenetrating=false)은 초기 침투와 다르며, 쿼리의 접촉 판정을 따른다.
	// - 무시하지 않은 초기 침투가 여러 개면 이동 방향과 내적이 가장 작은 법선의 Hit 하나를 선택한다.
	//   선택된 초기 침투는 Time=0에서 병진 이동을 막고, 이동이 없어도 양쪽 Actor/Component Hit을 알린다.
	// - 일반 스윕 충돌은 최초 BlockingHit에서 최대 0.001 거리만큼 후퇴하여 이동하고, 그 Hit 하나를 알린다.
	// - Transform이 바뀌면 이동 중/최종 위치의 오버랩을 갱신하여 Begin/EndOverlap을 알리고, 그 뒤 Hit을 알린다.
	// - 이동량이 최소 기준 이하고 회전 변화도 없으면 스윕/오버랩 갱신/Hit 알림 없이 반환한다.
	// - 회전만 하는 경우 회전 경로는 스윕하지 않는다. 침투 해소/벽면 슬라이딩도 자동 수행하지 않는다.
	MOVECOMP_NoFlags = 0x0000,
	MOVECOMP_IgnoreBases = 0x0001, // [미구현/값만 정의] 액터의 이동 기반(base) 관계에 있는 대상의 BlockingHit을 무시한다.
	MOVECOMP_SkipPhysicsMove = 0x0002, // [미구현/값만 정의] 컴포넌트 Transform은 갱신하되 물리 표현의 이동은 생략한다.
	MOVECOMP_NeverIgnoreBlockingOverlaps = 0x0004, // [구현] 탈출 방향이라도 초기 침투 BlockingHit을 무시하지 않는다. 같은 액터 제외는 유지한다.
	MOVECOMP_DisableBlockingOverlapDispatch = 0x0008, // [구현] 선택된 Hit이 초기 침투면 양쪽 Actor/Component Hit 알림만 생략한다. 이동/Hit 반환/Begin·EndOverlap은 유지한다.
	MOVECOMP_CheckBlockingRootActorInIgnoreList = 0x0010, // [미구현/값만 정의] 블록 대상의 부착 계층 최상위 액터도 UPrimitiveComponent::MoveIgnoreActors(현재 없음)와 비교하여 제외한다.
};
DEFINE_ENUM_OPERATORS(EMoveComponentFlags)

enum class EAttachmentRule
{
	KeepRelative,
	KeepWorld,
	SnapToTarget
};

class USceneComponent : public UActorComponent
{
	DECLARE_CLASS(USceneComponent, UActorComponent)

	REFLECT_START(ClassName)
		PROPERTY(Transform)
		REFLECT_END()

	LUA_BIND_START(USceneComponent)
		LUA_FUNC(GetRelativeLocation)
		LUA_FUNC(SetRelativeLocation)
		LUA_FUNC(GetRelativeRotation)
		LUA_FUNC(SetRelativeRotation)
		LUA_FUNC(GetRelativeScale3D)
		LUA_FUNC(SetRelativeScale3D)
		LUA_FUNC(GetWorldLocation)
		LUA_FUNC(GetWorldRotation)
		LUA_FUNC(GetWorldScale3D)
		UserType["SetWorldLocation"] = [](USceneComponent& Self, const FVector& Loc, sol::optional<bool> bSweep) {
			Self.SetWorldLocation(Loc, bSweep.value_or(false));
		};
		UserType["SetWorldRotation"] = [](USceneComponent& Self, const FRotator& Rot, sol::optional<bool> bSweep) {
			Self.SetWorldRotation(Rot, bSweep.value_or(false));
		};
	LUA_BIND_END()

public:
	enum class EVisibilityPropagation : uint8 { NoPropagation, DirtyOnly, Propagate };

	USceneComponent() = default;
	virtual ~USceneComponent() override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// Get & Set
	const FVector& GetRelativeLocation() const { return Transform.Location; }
	void SetRelativeLocation(const FVector& InLocation) { SetRelativeLocationAndRotation(InLocation, Transform.Rotation); }

	const FRotator& GetRelativeRotation() const { return Transform.Rotation; }
	void SetRelativeRotation(const FRotator& InRotation) { SetRelativeLocationAndRotation(Transform.Location, InRotation); }
	void SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation);
	const FVector& GetRelativeScale3D() const { return Transform.Scale; }
	void SetRelativeScale3D(const FVector& InScale);

	// 쿼터니언 적용된 회전행렬
	FQuat GetRelativeRotationQuat() const { return Transform.GetOrientation(); }

	const FTransform& GetTransform() const { return Transform; }
	void SetTransform(const FTransform& InTransform);

	// Attatch-To
	USceneComponent* GetAttachParent() const { return AttachParent; }
	const TArray<USceneComponent*>& GetAttachChildren() const { return AttachChildren; }
	void SetupAttachment(USceneComponent* InParent, EAttachmentRule Rule = EAttachmentRule::KeepRelative);
	void DetachFromParent(EAttachmentRule Rule = EAttachmentRule::KeepRelative);

	virtual FBox CalcLocalBounds() const { return FBox{ FVector(), FVector() }; }
	FBox CalcBounds() const { return CalcLocalBounds().GetWorldAABB(GetWorldMatrix()); }

	FVector GetWorldLocation() const;
	FRotator GetWorldRotation() const;
	FVector GetWorldScale3D() const;
	FMatrix GetWorldMatrix() const;
	
	void SetWorldLocation(FVector NewLocation, bool bSweep);
	void SetWorldRotation(FRotator NewRotation, bool bSweep);
	void SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep);
	void SetWorldScale3D(FVector NewScale);	
	void SetWorldTransform(const FMatrix& InWorldMatrix, bool bSweep);

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags);
	virtual void UpdateOverlaps(const TArray<UPrimitiveComponent*>* PendingOverlaps = nullptr);

	void MarkTransformDirty();
	virtual void OnTransformDirty() {};

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual bool IsShown(bool bGameView) const override;
	void SetVisibility(bool bNewVisibility) override
	{ SetVisibility(bNewVisibility, EVisibilityPropagation::NoPropagation); }
	void SetVisibility(bool bNewVisibility, EVisibilityPropagation Propagation);
	void SetVisibility(bool bNewVisibility, bool bPropagateToChildren)
	{ SetVisibility(bNewVisibility, bPropagateToChildren ? EVisibilityPropagation::Propagate : EVisibilityPropagation::NoPropagation); }
	void SetHiddenInGame(bool bNewHidden) override
	{ SetHiddenInGame(bNewHidden, EVisibilityPropagation::NoPropagation); }
	void SetHiddenInGame(bool bNewHidden, EVisibilityPropagation Propagation);
	void SetHiddenInGame(bool bNewHidden, bool bPropagateToChildren)
	{ SetHiddenInGame(bNewHidden, bPropagateToChildren ? EVisibilityPropagation::Propagate : EVisibilityPropagation::NoPropagation); }
	virtual void MarkRenderStateDirty() override {}

public:
	FVector ComponentVelocity;

protected:
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	void DetachOnDestruction();

	void ApplyWorldLocationAndRotation(const FVector& Location, const FRotator& Rotation);
	void ApplyWorldTransform(const FMatrix& WorldMatrix);
	bool bTransformDirty = false;
	FTransform Transform;

	USceneComponent* AttachParent = nullptr; // Attach 부모 정보
	TArray<USceneComponent*> AttachChildren;

	bool bAbsoluteLocation = false;
	bool bAbsoluteRotation = false;
	bool bAbsoluteScale = false;
};
