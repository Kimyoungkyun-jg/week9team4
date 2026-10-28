#include "EnginePCH.h"
#include "LuaBinding.h"
#include "Engine/GameInstance.h"
#include "UObject/Object.h"
#include "Collision/HitResult.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
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
		Lua.set_function("GetWorld", [&GameInstance]() -> UWorld* {
			return GameInstance.GetWorld();
		});
		Lua.set_function("SetGlobalTimeDilation", [&GameInstance](float Dilation) {
			if (UWorld* World = GameInstance.GetWorld())
			{
				World->SetWorldTimeDilation(Dilation);
			}
		});
		Lua.set_function("GetGlobalTimeDilation", [&GameInstance]() -> float {
			if (UWorld* World = GameInstance.GetWorld())
			{
				return World->GetWorldTimeDilation();
			}
			return 1.0f;
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

		// 4. UWorld 바인딩
		Lua.new_usertype<UWorld>("UWorld", sol::no_constructor,
			"GetWorldTimeDilation", &UWorld::GetWorldTimeDilation,
			"SetWorldTimeDilation", &UWorld::SetWorldTimeDilation,
			"GetPlayerPawn", &UWorld::GetPlayerPawn,
			"GetPlayerController", &UWorld::GetPlayerController
		);

		// 5. APlayerController 바인딩
		Lua.new_usertype<APlayerController>("APlayerController", sol::no_constructor,
			"GetPlayerCameraManager", [](APlayerController& Self) -> APlayerCameraManager* {
				return Self.GetPlayerCameraManager();
			}
		);

		// 6. APlayerCameraManager 바인딩
		Lua.new_usertype<APlayerCameraManager>("APlayerCameraManager", sol::no_constructor,
			"FadeIn", sol::overload(
				[](APlayerCameraManager& Self) { Self.FadeIn(); },
				[](APlayerCameraManager& Self, float Duration) { Self.FadeIn(Duration); },
				[](APlayerCameraManager& Self, float Duration, const FLinearColor& Color) { Self.FadeIn(Duration, Color); },
				[](APlayerCameraManager& Self, float Duration, const FLinearColor& Color, bool bInFadeHUD) { Self.FadeIn(Duration, Color, bInFadeHUD); }
			),
			"FadeOut", sol::overload(
				[](APlayerCameraManager& Self) { Self.FadeOut(); },
				[](APlayerCameraManager& Self, float Duration) { Self.FadeOut(Duration); },
				[](APlayerCameraManager& Self, float Duration, const FLinearColor& Color) { Self.FadeOut(Duration, Color); },
				[](APlayerCameraManager& Self, float Duration, const FLinearColor& Color, bool bHold) { Self.FadeOut(Duration, Color, bHold); },
				[](APlayerCameraManager& Self, float Duration, const FLinearColor& Color, bool bHold, bool bInFadeHUD) { Self.FadeOut(Duration, Color, bHold, bInFadeHUD); }
			),
			"StopCameraFade", &APlayerCameraManager::StopCameraFade,
			"HasActiveFade", &APlayerCameraManager::HasActiveFade,
			"GetFadeAmount", &APlayerCameraManager::GetFadeAmount
		);

		// 7. 편리한 전역 Fade 함수 바인딩
		Lua.set_function("FadeIn", [&GameInstance](sol::variadic_args va) {
			if (UWorld* World = GameInstance.GetWorld())
			{
				if (APlayerController* PC = World->GetPlayerController())
				{
					if (APlayerCameraManager* PCM = PC->GetPlayerCameraManager())
					{
						float Duration = (va.size() > 0 && va[0].is<float>()) ? va[0].as<float>() : 1.0f;
						PCM->FadeIn(Duration);
					}
				}
			}
		});

		Lua.set_function("FadeOut", [&GameInstance](sol::variadic_args va) {
			if (UWorld* World = GameInstance.GetWorld())
			{
				if (APlayerController* PC = World->GetPlayerController())
				{
					if (APlayerCameraManager* PCM = PC->GetPlayerCameraManager())
					{
						float Duration = (va.size() > 0 && va[0].is<float>()) ? va[0].as<float>() : 1.0f;
						PCM->FadeOut(Duration);
					}
				}
			}
		});
	}
}

// AActor의 GetWorld를 AActor가 등록된 후에 안전하게 확장 바인딩
struct FRegisterActorWorldExtension
{
	FRegisterActorWorldExtension()
	{
		GetLuaAutoRegistry().push_back([](sol::state& Lua) {
			sol::table ActorType = Lua["AActor"];
			if (ActorType.valid())
			{
				ActorType["GetWorld"] = [](AActor& Self) -> UWorld* {
					return Self.GetWorld();
				};
			}
		});
	}
};
static FRegisterActorWorldExtension GRegisterActorWorldExtension;
