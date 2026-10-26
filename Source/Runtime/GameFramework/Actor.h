#pragma once

#include "UObject/Object.h"
#include "Components/PrimitiveComponent.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"
#include "UObject/UObjectHash.h"
#include "UObject/Casts.h"

#include "Lua/LuaMacros.h"

class UWorld;
class ULevel;

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
		LUA_FUNC(SetActorLocation)
		LUA_FUNC(SetActorRotation)
		UserType["FindComponentByName"] = [](AActor& Self, const FString& Name) -> UActorComponent* {
			return Self.FindComponentByName(FName(Name));
		};
		UserType["GetComponentByClass"] = [](AActor& Self, const FString& ClassName) -> UActorComponent* {
			UClass* TargetClass = FindClass(ClassName);
			return TargetClass ? Self.GetComponentByClass(TargetClass) : nullptr;
		};
		UserType["GetComponentsByClass"] = [](AActor& Self, const FString& ClassName, sol::this_state s) -> sol::table {
			sol::state_view lua(s);
			sol::table Result = lua.create_table();
			UClass* TargetClass = FindClass(ClassName);
			if (!TargetClass) return Result;

			TArray<UActorComponent*> List = Self.GetComponentsByClass(TargetClass);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				Result[i + 1] = List[i];
			}
			return Result;
		};
	LUA_BIND_END()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	bool HasBegunPlay() const { return bHasBegunPlay; }
	// 액터 자신의 로직. 컴포넌트는 각자의 PrimaryComponentTick으로 따로 실행된다.
	virtual void Tick(float DeltaTime) {}
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

	void RemoveOwnedComponent(UActorComponent* Component);
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	FVector GetActorScale3D() const;
	void SetActorLocation(const FVector& NewLocation);
	void SetActorRotation(const FRotator& NewRotation);
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();
	
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
	bool bActorEnableCollision = true;
	bool bHasBegunPlay = false;
	bool bHasEndedPlay = false;
	bool bIsEndingPlay = false;

};
