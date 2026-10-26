#include "EnginePCH.h"
#include "LuaBinding.h"
#include "Engine/GameInstance.h"
#include "UObject/Object.h"
#include "Collision/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "GameFramework/Actor.h"
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
