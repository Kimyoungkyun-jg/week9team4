#include "EnginePCH.h"
#include "GameFramework/Character.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"

ACharacter::ACharacter()
{
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	SetActorEnableCollision(true);
	auto* Capsule = CreateDefaultSubobject<UCapsuleComponent>("CollisionComponent");
	SetRootComponent(Capsule);
	Capsule->SetCapsuleSize(0.5f, 1.f);
	Capsule->SetCollisionEnabled(true);
	Capsule->SetCollisionObjectType(ECollisionChannel::Pawn);
	Capsule->SetGenerateOverlapEvents(true);
	auto* Camera = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	Camera->SetupAttachment(Capsule);
	Camera->SetExternalInputManaged(true);
	CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
}

void ACharacter::BeginPlay()
{
	const FRotator SpawnRotation = GetActorRotation();
	SetActorRotation(FRotator(0.f, SpawnRotation.Yaw, 0.f));
	if (UCameraComponent* Camera = GetCameraComponent())
		Camera->SetRelativeRotation(FRotator(SpawnRotation.Pitch, 0.f, 0.f));
	Super::BeginPlay();
}

void ACharacter::AddMovementInput(const FVector& Direction, float ScaleValue)
{
	Super::AddMovementInput(FVector(Direction.X, Direction.Y, 0.f), ScaleValue);
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (std::isfinite(Value)) SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + Value, 0.f));
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (UCameraComponent* Camera = GetCameraComponent(); Camera && std::isfinite(Value))
		Camera->SetRelativeRotation(FRotator(FMath::Clamp(Camera->GetRelativeRotation().Pitch + Value, -89.f, 89.f), 0.f, 0.f));
}

void ACharacter::Jump()
{
	if (auto* Movement = GetCharacterMovement()) Movement->RequestJump();
}

void ACharacter::StopJumping()
{
	if (auto* Movement = GetCharacterMovement()) Movement->StopJumping();
}

bool ACharacter::CanJump() const
{
	const auto* Movement = GetCharacterMovement();
	return Movement && Movement->CanJump();
}

UCapsuleComponent* ACharacter::GetCapsuleComponent() const { return Cast<UCapsuleComponent>(GetRootComponent()); }
UCameraComponent* ACharacter::GetCameraComponent() const { return GetComponentByClass<UCameraComponent>(); }
UCharacterMovementComponent* ACharacter::GetCharacterMovement() const { return GetComponentByClass<UCharacterMovementComponent>(); }
