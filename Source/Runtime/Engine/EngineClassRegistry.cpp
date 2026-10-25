#include "EnginePCH.h"
#include "EngineClassRegistry.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/MapActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/FireBallActor.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/TestGameMode.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/LuaScriptComponent.h"

void RegisterEngineClasses()
{
    (void)AStaticMeshActor::StaticClass();
    (void)AMapActor::StaticClass();
    (void)ADirectionalLightActor::StaticClass();
    (void)APointLightActor::StaticClass();
    (void)ASpotLightActor::StaticClass();
    (void)ARectLightActor::StaticClass();
    (void)ASkyLightActor::StaticClass();
    (void)ATextRenderActor::StaticClass();
    (void)AParticleActor::StaticClass();
    (void)AFireBallActor::StaticClass();
    (void)AProjectileActor::StaticClass();
    (void)ADefaultPawn::StaticClass();
    (void)ATestGameMode::StaticClass();
    (void)ACameraActor::StaticClass();
    (void)UBoxComponent::StaticClass();
    (void)USphereComponent::StaticClass();
    (void)UCapsuleComponent::StaticClass();
    (void)URotatingMovementComponent::StaticClass();
    (void)UProjectileMovementComponent::StaticClass();
    (void)ULuaScriptComponent::StaticClass();
}
