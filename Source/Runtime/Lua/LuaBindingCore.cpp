#include "EnginePCH.h"
#include "LuaBinding.h"
#include "Engine/GameInstance.h"
#include "UObject/Object.h"
#include "Collision/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "GameFramework/Actor.h"
#include "Sequencer/LevelSequenceActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Core/EngineLog.h"

namespace LuaBindings
{
	void BindCore(sol::state& Lua, UGameInstance& GameInstance)
	{
		// 1. 전역 유틸리티 함수
		Lua.set_function("Log", [](const FString& Message) {
			HTR_LOG(Info, "[Lua] {}", Message);
		});
		Lua.set_function("LogWarning", [](const FString& Message) {
			HTR_LOG(Warning, "[Lua] {}", Message);
		});
		Lua.set_function("LogError", [](const FString& Message) {
			HTR_LOG(Error, "[Lua] {}", Message);
		});
		Lua.set_function("EndGame", [&GameInstance]() {
			GameInstance.RequestEndSession();
		});
		Lua.set_function("CastToLevelSequenceActor", [](sol::object ActorObj) -> ALevelSequenceActor* {
			if (!ActorObj.valid() || ActorObj.is<sol::nil_t>()) return nullptr;
			if (ActorObj.is<ALevelSequenceActor*>()) return ActorObj.as<ALevelSequenceActor*>();
			if (ActorObj.is<AActor*>()) return Cast<ALevelSequenceActor>(ActorObj.as<AActor*>());
			return nullptr;
		});
		Lua.set_function("PlayLevelSequence", [&GameInstance](sol::optional<FString> TargetNameOrPath) -> bool {
			UWorld* World = GameInstance.GetWorld();
			if (!World || !World->GetPersistentLevel()) return false;
			const FString Query = TargetNameOrPath.value_or("");
			for (AActor* Actor : World->GetPersistentLevel()->GetActors())
			{
				ALevelSequenceActor* SeqActor = Cast<ALevelSequenceActor>(Actor);
				if (!SeqActor || !IsValid(SeqActor) || SeqActor->IsActorBeingDestroyed()) continue;
				if (Query.empty() || SeqActor->GetName() == Query || SeqActor->GetSequenceAssetPath() == Query)
				{
					SeqActor->PlaySequence();
					return true;
				}
			}
			if (!Query.empty() && Query.ends_with(".seq.json"))
			{
				if (ALevelSequenceActor* Spawned = World->SpawnActor<ALevelSequenceActor>())
				{
					Spawned->SetSequenceAssetPath(Query);
					Spawned->PlaySequence();
					return true;
				}
			}
			return false;
		});
		Lua.set_function("PauseLevelSequence", [&GameInstance](sol::optional<FString> TargetNameOrPath) {
			UWorld* World = GameInstance.GetWorld();
			if (!World || !World->GetPersistentLevel()) return;
			const FString Query = TargetNameOrPath.value_or("");
			for (AActor* Actor : World->GetPersistentLevel()->GetActors())
			{
				ALevelSequenceActor* SeqActor = Cast<ALevelSequenceActor>(Actor);
				if (!SeqActor || !IsValid(SeqActor) || SeqActor->IsActorBeingDestroyed()) continue;
				if (Query.empty() || SeqActor->GetName() == Query || SeqActor->GetSequenceAssetPath() == Query)
				{
					SeqActor->PauseSequence();
				}
			}
		});
		Lua.set_function("StopLevelSequence", [&GameInstance](sol::optional<FString> TargetNameOrPath) {
			UWorld* World = GameInstance.GetWorld();
			if (!World || !World->GetPersistentLevel()) return;
			const FString Query = TargetNameOrPath.value_or("");
			for (AActor* Actor : World->GetPersistentLevel()->GetActors())
			{
				ALevelSequenceActor* SeqActor = Cast<ALevelSequenceActor>(Actor);
				if (!SeqActor || !IsValid(SeqActor) || SeqActor->IsActorBeingDestroyed()) continue;
				if (Query.empty() || SeqActor->GetName() == Query || SeqActor->GetSequenceAssetPath() == Query)
				{
					SeqActor->StopSequence();
				}
			}
		});
		if (false) Lua.set_function("UnusedEndGame", [&GameInstance]() {
			GameInstance.RequestEndSession();
		});
		Lua.set_function("CastToMovementComponent", [](sol::object CompObj) -> UMovementComponent* {
			if (!CompObj.valid() || CompObj.is<sol::nil_t>()) return nullptr;
			if (CompObj.is<UMovementComponent*>()) return CompObj.as<UMovementComponent*>();
			if (CompObj.is<UActorComponent*>()) return Cast<UMovementComponent>(CompObj.as<UActorComponent*>());
			return nullptr;
		});

		// 2. UObject 기본 바인딩
		Lua.new_usertype<UObject>("UObject", sol::no_constructor,
			"UUID", &UObject::GetUUID,
			"GetName", &UObject::GetName
		);

		// 3. FHitResult 바인딩
		Lua.new_usertype<FHitResult>("FHitResult",
			sol::constructors<FHitResult(), FHitResult(float)>(),
			sol::call_constructor, sol::constructors<FHitResult(), FHitResult(float)>(),
			"bBlockingHit", &FHitResult::bBlockingHit,
			"bStartPenetrating", &FHitResult::bStartPenetrating,
			"Distance", &FHitResult::Distance,
			"Time", &FHitResult::Time,
			"ImpactPoint", &FHitResult::ImpactPoint,
			"ImpactNormal", &FHitResult::ImpactNormal,
			"Normal", &FHitResult::Normal,
			"Location", &FHitResult::Location,
			"PenetrationDepth", &FHitResult::PenetrationDepth,
			"HitComponent", &FHitResult::HitComponent,
			"GetActor", [](const FHitResult& Self) -> AActor* {
				return Self.HitComponent ? Self.HitComponent->GetOwner() : nullptr;
			}
		);
	}
}
