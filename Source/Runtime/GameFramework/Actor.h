#pragma once

#include "UObject/Object.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/UObjectHash.h"
#include "UObject/Casts.h"

#include "Lua/LuaMacros.h"
#include "Components/MovementComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

inline sol::object PushComponentToLua(lua_State* L, UActorComponent* Comp)
{
	if (!Comp)
	{
		return sol::make_object(L, sol::nil);
	}

	if (auto* MoveComp = Cast<UMovementComponent>(Comp))
	{
		return sol::make_object(L, MoveComp);
	}
	if (auto* FogComp = Cast<UExponentialHeightFogComponent>(Comp))
	{
		return sol::make_object(L, FogComp);
	}
	if (auto* PrimComp = Cast<UPrimitiveComponent>(Comp))
	{
		return sol::make_object(L, PrimComp);
	}
	if (auto* SceneComp = Cast<USceneComponent>(Comp))
	{
		return sol::make_object(L, SceneComp);
	}

	return sol::make_object(L, Comp);
}

class UWorld;
class ULevel;
struct FMinimalViewInfo;

class AActor : public UObject
{
	DECLARE_CLASS(AActor, UObject)
	REFLECT_START(ClassName)
		PROPERTY(bActorEnableCollision)
		REFLECT_END()

	LUA_BIND_START(AActor)
		LUA_FUNC(Destroy)
		LUA_FUNC(GetRootComponent)
		LUA_FUNC(GetName)
		LUA_FUNC(GetActorLocation)
		LUA_FUNC(GetActorRotation)
		LUA_FUNC(GetActorScale3D)
		UserType["SetActorLocation"] = [](AActor& Self, const FVector& Loc, sol::optional<bool> bSweep) {
			Self.SetActorLocation(Loc, bSweep.value_or(false));
		};
		UserType["SetActorRotation"] = [](AActor& Self, const FRotator& Rot, sol::optional<bool> bSweep) {
			Self.SetActorRotation(Rot, bSweep.value_or(false));
		};
		UserType["FindComponentByName"] = [](AActor& Self, const FString& Name, sol::this_state s) -> sol::object {
			UActorComponent* Comp = Self.FindComponentByName(FName(Name));
			return PushComponentToLua(s.lua_state(), Comp);
		};
		UserType["GetComponentByClass"] = [](AActor& Self, const FString& ClassName, sol::this_state s) -> sol::object {
			UClass* TargetClass = FindClass(ClassName);
			UActorComponent* Comp = TargetClass ? Self.GetComponentByClass(TargetClass) : nullptr;
			return PushComponentToLua(s.lua_state(), Comp);
		};
		UserType["GetComponentsByClass"] = [](AActor& Self, const FString& ClassName, sol::this_state s) -> sol::table {
			sol::state_view lua(s);
			sol::table Result = lua.create_table();
			UClass* TargetClass = FindClass(ClassName);
			if (!TargetClass) return Result;

			TArray<UActorComponent*> List = Self.GetComponentsByClass(TargetClass);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				Result[i + 1] = PushComponentToLua(s.lua_state(), List[i]);
			}
			return Result;
		};
		UserType["CastToMovementComponent"] = [](AActor& Self, sol::object CompObj) -> UMovementComponent* {
			if (!CompObj.valid() || CompObj.is<sol::nil_t>()) return nullptr;
			if (CompObj.is<UMovementComponent*>()) return CompObj.as<UMovementComponent*>();
			if (CompObj.is<UActorComponent*>()) return Cast<UMovementComponent>(CompObj.as<UActorComponent*>());
			return nullptr;
		};
	LUA_BIND_END()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	bool HasBegunPlay() const { return bHasBegunPlay; }
	// 액터 자신의 로직. 컴포넌트는 각자의 PrimaryComponentTick으로 따로 실행된다.
	virtual void Tick(float DeltaTime) {}
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV) const;
	virtual void EndPlay() {};
	void EndPlayActor();
	// FActorTickFunction이 호출하는 진입점
	void TickActor(float DeltaTime) { Tick(DeltaTime); }

	UWorld* GetWorld() const { return World; }
	ULevel* GetLevel() const { return Level; }

	const TArray<UActorComponent*>& GetComponents() const { return Components; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	void SetRootComponent(USceneComponent* SceneComponent) { RootComponent = SceneComponent; }
	void SetActorEnableCollision(bool bEnabled);
	bool GetActorEnableCollision() const { return bActorEnableCollision; }
	bool IsOverlappingActor(const AActor* Other) const;

	TMulticastDelegate<AActor*, AActor*, const FHitResult&> OnActorHit;
	TMulticastDelegate<AActor*, AActor*> OnActorBeginOverlap;
	TMulticastDelegate<AActor*, AActor*> OnActorEndOverlap;

	void RemoveOwnedComponent(UActorComponent* Component);
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	void SetActorLocation(const FVector& NewLocation, bool bSweep = false)
	{
		if (RootComponent) RootComponent->SetWorldLocation(NewLocation, bSweep);
	}
	void SetActorRotation(const FRotator& NewRotation, bool bSweep = false)
	{
		if (RootComponent) RootComponent->SetWorldRotation(NewRotation, bSweep);
	}
	void SetActorRelativeLocation(const FVector& NewRelativeLocation)
	{
		if (RootComponent) RootComponent->SetRelativeLocation(NewRelativeLocation);
	}
	void SetActorRelativeRotation(const FRotator& NewRelativeRotation)
	{
		if (RootComponent) RootComponent->SetRelativeRotation(NewRelativeRotation);
	}
	void AttachToComponent(USceneComponent* Parent, const FTransform& RelativeTransform = FTransform::Identity)
	{
		if (RootComponent && Parent)
		{
			RootComponent->SetupAttachment(Parent);
			RootComponent->SetTransform(RelativeTransform);
		}
	}
	FVector GetActorScale3D() const;
	void SetActorScale3D(const FVector& NewScale)
	{
		if (RootComponent) RootComponent->SetRelativeScale3D(NewScale);
	}
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();
	bool IsActorBeingDestroyed() const { return bActorIsBeingDestroyed; }
	const FString& GetPersistentActorId() const { return PersistentActorId; }
	void RegeneratePersistentActorId();
	bool IsPendingKillPending() const { return bActorIsBeingDestroyed || !IsValid(this); }
	
	//xx삭제 예정
	friend class UWorld;

	UActorComponent* AddComponent(UClass* ComponentClass, FName Name);
	void DestroyComponent(UActorComponent* Component);

	// bCanEverTick이 켜진 액터·컴포넌트의 Tick 함수만 World의 FTickTaskManager에 등록하거나 해제한다.
	void RegisterAllActorTickFunctions(bool bRegister);

	// UE와 같이 기본값은 bCanEverTick = false. 생성자에서 Target = this
	FActorTickFunction PrimaryActorTick;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	UActorComponent* FindComponentByName(FName Name) const;
	UActorComponent* GetComponentByClass(const UClass* ComponentClass) const;
	TArray<UActorComponent*> GetComponentsByClass(const UClass* ComponentClass) const;

	template <typename T>
	T* GetComponentByClass() const
	{
		static_assert(std::is_base_of_v<UActorComponent, T>, "T must derive from UActorComponent");
		return Cast<T>(GetComponentByClass(T::StaticClass()));
	}

	template <typename T>
	TArray<T*> GetComponentsByClass() const
	{
		static_assert(std::is_base_of_v<UActorComponent, T>, "T must derive from UActorComponent");
		TArray<T*> OutComponents;
		for (UActorComponent* Comp : Components)
		{
			if (T* Casted = Cast<T>(Comp))
			{
				OutComponents.Add(Casted);
			}
		}
		return OutComponents;
	}

	virtual void RegisterAllComponents();
	// Parameter : 바로 다시 Register 할껀지
	virtual void UnregisterAllComponents();
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) override;

	//TSet<TObjectPtr<UActorComponent>> OwnedComponents;
	TArray<UActorComponent*> Components;
	USceneComponent* RootComponent = nullptr;

	UWorld* World = nullptr;
	ULevel* Level = nullptr;
private:
	FString PersistentActorId;
	bool bActorEnableCollision = true;
	bool bHasBegunPlay = false;
	bool bHasEndedPlay = false;
	bool bIsEndingPlay = false;
	bool bActorIsBeingDestroyed = false;

};
