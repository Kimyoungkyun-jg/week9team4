#include "EnginePCH.h"
#include "PlayerPawn.h"
#include "FlashlightActor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/HeadBobComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "GameFramework/GameHUD.h"
#include "Components/LuaScriptComponent.h"
#include <algorithm>
#include <string>

APlayerPawn::APlayerPawn()
{
	CreateDefaultSubobject<UHeadBobComponent>("HeadBob");
	// Pacman rules: collision with ghosts is an overlap, and the player cannot jump.
	GetCharacterMovement()->SetJumpAllowed(false);
	auto* CollisionComponent = GetCapsuleComponent();
	CollisionComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
	CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
	CollisionComponent->SetGenerateOverlapEvents(true);

	ULuaScriptComponent* ScriptComp = CreateDefaultSubobject<ULuaScriptComponent>("PlayerScript");
	ScriptComp->SetScriptPath("Assets/Scripts/player.lua");
}

void APlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	ResetWalkingEffects();
	UCameraComponent* Camera = GetCameraComponent();
	UWorld* PawnWorld = GetWorld();
	PlayerStartLocation = GetActorLocation();
	PlayerStartRotation = GetActorRotation();
	if (Camera)
	{
		PlayerStartCameraLocation = Camera->GetRelativeLocation();
		PlayerStartCameraRotation = Camera->GetRelativeRotation();
	}

	SetActorEnableCollision(true);
	auto* CollisionComponent = GetCapsuleComponent();
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionEnabled(true);
		CollisionComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
		CollisionComponent->SetCollisionResponseToAllChannels(ECollisionResponse::Ignore);
		CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldStatic, ECollisionResponse::Block);
		CollisionComponent->SetCollisionResponseToChannel(ECollisionChannel::WorldDynamic, ECollisionResponse::Overlap);
		CollisionComponent->SetGenerateOverlapEvents(true);
		CollisionComponent->UpdateOverlaps();
	}

	if (!PawnWorld || (PawnWorld->GetWorldType() != EWorldType::PIE && PawnWorld->GetWorldType() != EWorldType::Game)) return;

	if (!Camera) return;

	FlashlightActor = PawnWorld->SpawnActor<AFlashlightActor>();
	FlashlightActor->SetActorEnableCollision(false);
	FlashlightActor->AttachToComponent(Camera);
	FlashlightActor->SetActorRelativeLocation({0.35f, 0.28f, -0.2f});
	FlashlightActor->SetActorRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
}

void APlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* PawnWorld = GetWorld();
	const bool bPlaying = PawnWorld && !PawnWorld->IsPaused() &&
		(PawnWorld->GetWorldType() == EWorldType::Game || PawnWorld->GetWorldType() == EWorldType::PIE);
	const auto* Movement = GetCharacterMovement();
	auto* Bob = GetHeadBobComponent();
	const bool bGrounded = Movement && Movement->IsMovingOnGround();
	const FVector FrameMovement = bGrounded ? Movement->GetLastGroundedDisplacement() : FVector::ZeroVector;
	const bool bMoved = bPlaying && (!Bob || !Bob->IsSuspended()) && FrameMovement.SizeSquared() > 0.00000001f;
	const bool bStep = FootstepCadence.Advance(DeltaTime, bMoved);
	// Camera effects use the completed grounded move, never input or a respawn teleport.
	if (bPlaying && Bob) Bob->UpdateBob(DeltaTime, FrameMovement, bStep);
	if (!bStep) return;
	static constexpr const char* Steps[] = {"Assets/Sounds/footstep00.wav", "Assets/Sounds/footstep01.wav",
		"Assets/Sounds/footstep02.wav", "Assets/Sounds/footstep03.wav", "Assets/Sounds/footstep04.wav"};
	const int Index = std::uniform_int_distribution<int>(0, 4)(FootstepRandom);
	if (UGameInstance* Instance = PawnWorld->GetGameInstance())
		if (FSoundManager* Sound = Instance->GetSoundManager()) Sound->PlaySFX(Steps[Index], 0.5f);
}

UHeadBobComponent* APlayerPawn::GetHeadBobComponent() const
{
	return GetComponentByClass<UHeadBobComponent>();
}

void APlayerPawn::ResetWalkingEffects()
{
	FootstepCadence.Reset();
	if (auto* Bob = GetHeadBobComponent()) Bob->Reset();
}

void APlayerPawn::SetHeadBobSuspended(bool bSuspended)
{
	FootstepCadence.Reset();
	if (auto* Bob = GetHeadBobComponent()) Bob->SetSuspended(bSuspended);
}

void APlayerPawn::SetLife(int32 InLife)
{
	Life = std::max(0, InLife);
	if (UWorld* PawnWorld = GetWorld())
	{
		if (AGameHUD* HUD = PawnWorld->GetHUD())
			HUD->SetText("Life", std::to_string(Life));
	}
}

void APlayerPawn::RespawnAtPlayerStart()
{
	ResetWalkingEffects();
	if (auto* Movement = GetCharacterMovement())
	{
		Movement->SetMovementEnabled(true);
		Movement->ResetAfterTeleport();
	}
	SetActorLocation(PlayerStartLocation);
	SetActorRotation(PlayerStartRotation);
	if (UCameraComponent* Camera = GetCameraComponent())
	{
		Camera->SetRelativeLocation(PlayerStartCameraLocation);
		Camera->SetRelativeRotation(PlayerStartCameraRotation);
	}
	// Keep cinematic suspension until the game mode releases camera ownership.
	if (auto* Bob = GetHeadBobComponent()) Bob->Rebase();
	if (auto* CollisionComponent = GetCapsuleComponent()) CollisionComponent->UpdateOverlaps();
}

void APlayerPawn::EndPlay()
{
	ResetWalkingEffects();
	if (FlashlightActor)
	{
		FlashlightActor->Destroy();
		FlashlightActor = nullptr;
	}
	Super::EndPlay();
}
