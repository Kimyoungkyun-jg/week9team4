#include "EnginePCH.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

UActorComponent::~UActorComponent()
{
	assert(!bRegistered && "Delete Without Unregister");
	if (Owner)
	{
		Owner->RemoveOwnedComponent(this);
	}
}

void UActorComponent::DestroyComponent()
{
	if (!IsValid(this) || bIsBeingDestroyed || bIsEndingPlay) return;
	AActor* MyOwner = GetOwner();
	if (MyOwner && MyOwner->GetComponents().Find(this) == INDEX_NONE) return;
	if (MyOwner && MyOwner->GetRootComponent() == this && !MyOwner->IsActorBeingDestroyed())
	{
		HTR_LOG(Warning, "RootComponent cannot be deleted.");
		return;
	}
	UWorld* World = GetWorld();
	if (!World && MyOwner) World = MyOwner->GetWorld();
	if (!World)
	{
		HTR_LOG(Warning, "DestroyComponent requires an owning World.");
		return;
	}
	UWorld::FScopedObjectExecution ObjectScope(World);
	bIsBeingDestroyed = true;
	EndPlayComponent();

	OnComponentDestroyed(MyOwner && MyOwner->IsActorBeingDestroyed());
	UnregisterComponent();
	World->RemoveCollisionListeners(this);
	if (MyOwner) MyOwner->RemoveOwnedComponent(this);
	SetOwner(nullptr);
	MarkAsGarbage();
	World->QueueObjectDeletion(this);
}

void UActorComponent::SetActive(bool bNewActive)
{
	if (bNewActive) Activate();
	else Deactivate();
}

void UActorComponent::SetVisibility(bool bNewVisibility)
{
	if (bVisible == bNewVisibility) return;
	bVisible = bNewVisibility;
	MarkRenderStateDirty();
}

void UActorComponent::SetHiddenInGame(bool bNewHidden)
{
	if (bHiddenInGame == bNewHidden) return;
	bHiddenInGame = bNewHidden;
	MarkRenderStateDirty();
}

void UActorComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bVisible" || Property.Name == "bHiddenInGame")
		MarkRenderStateDirty();
}

void UActorComponent::BeginPlayComponent()
{
	if (!IsValid(this) || bIsBeingDestroyed || !IsRegistered() || !WorldPrivate || !WorldPrivate->HasBegunPlay() ||
		(Owner && Owner->IsPendingKillPending())) return;
	if (!bIsInitialized) InitializeComponent();
	if (!IsValid(this) || bIsBeingDestroyed || !WorldPrivate || (Owner && Owner->IsPendingKillPending())) return;
	RegisterComponentTickFunctions(true);
	if (bHasBegunPlay) return;
	if (bAutoActivate) Activate();
	else SetComponentTickEnabled(false);
	if (!IsValid(this) || bIsBeingDestroyed || !WorldPrivate || (Owner && Owner->IsPendingKillPending())) return;
	bHasBegunPlay = true;
	BeginPlay();
}

void UActorComponent::EndPlayComponent()
{
	if (!bHasBegunPlay) return;
	bHasBegunPlay = false;
	bIsEndingPlay = true;
	RegisterComponentTickFunctions(false);
	EndPlay();
	if (WorldPrivate) WorldPrivate->RemoveCollisionListeners(this);
	bIsEndingPlay = false;
}

void UActorComponent::Activate(bool bReset)
{
	if (!IsValid(this) || bIsBeingDestroyed || (Owner && Owner->IsPendingKillPending())) return;
	if (!bReset && !ShouldActivate()) return;
	SetComponentTickEnabled(true);
	bIsActive = true;
}

void UActorComponent::Deactivate()
{
	if (!bIsActive) return;
	SetComponentTickEnabled(false);
	bIsActive = false;
}

void UActorComponent::SetAutoActivate(bool bNewAutoActivate)
{
	if (IsRegistered())
	{
		HTR_LOG(Warning, "SetAutoActivate: cannot change AutoActivate after component registration");
		return;
	}
	bAutoActivate = bNewAutoActivate;
}

void UActorComponent::SetComponentTickEnabled(bool bEnabled)
{
	if (bEnabled && (!IsValid(this) || bIsBeingDestroyed || (Owner && Owner->IsPendingKillPending()))) return;
	if (!PrimaryComponentTick.bCanEverTick) return;
	PrimaryComponentTick.bStartWithTickEnabled = bEnabled;
	PrimaryComponentTick.SetTickFunctionEnable(bEnabled);
}

void UActorComponent::RegisterComponentTickFunctions(bool bRegister)
{
	if (!bRegister)
	{
		PrimaryComponentTick.UnRegisterTickFunction();
		return;
	}
	if (IsValid(this) && !bIsBeingDestroyed && (!Owner || !Owner->IsPendingKillPending()) &&
		IsRegistered() && WorldPrivate && PrimaryComponentTick.bCanEverTick)
		PrimaryComponentTick.RegisterTickFunction(WorldPrivate->GetTickTaskManager());
}

void UActorComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* OwnerObj = Owner;
		Record << SA_VALUE("Owner", OwnerObj);
		Owner = static_cast<AActor*>(OwnerObj);
	}
}

void UActorComponent::RegisterComponent()
{
	AActor* MyOwner = GetOwner();
	UWorld* MyOwnerWorld = (MyOwner ? MyOwner->GetWorld() : nullptr);
	if (MyOwnerWorld)
	{
		RegisterComponentWithWorld(MyOwnerWorld);
	}
}

void UActorComponent::RegisterComponentWithWorld(UWorld* InWorld)
{
	if (!IsValid(this) || bIsBeingDestroyed || (Owner && Owner->IsPendingKillPending())) return;
	if (IsRegistered()) { HTR_LOG(Info, "RegisterComponentWithWorld: {} Already registered. Aborting.", GetClass()->Name); return; }
	if (!IsValid(InWorld)) { return; }

	WorldPrivate = InWorld;
	ExecuteRegisterEvents();
	if (!IsValid(this) || bIsBeingDestroyed || !IsRegistered() || !WorldPrivate) return;
	if (AActor* MyOwner = GetOwner(); MyOwner && MyOwner->HasBegunPlay())
		BeginPlayComponent();
}

void UActorComponent::UnregisterComponent()
{
	if (!IsRegistered()) return;
	UWorld::FScopedObjectExecution ObjectScope(WorldPrivate);
    if (WorldPrivate) WorldPrivate->RemoveCollisionListeners(this);
	RegisterComponentTickFunctions(false);
	ExecuteUnregisterEvents();
	WorldPrivate = nullptr;
}

void UActorComponent::OnRegister()
{
	bRegistered = true;
}

void UActorComponent::OnUnregister()
{
	bRegistered = false;
}

void UActorComponent::CreateRenderState()
{
	bRenderStateCreated = true;
}

void UActorComponent::DestroyRenderState()
{
	bRenderStateCreated = false;
}

void UActorComponent::ExecuteRegisterEvents()
{
	if (!bRegistered)
	{
		OnRegister();
		if (!IsValid(this) || bIsBeingDestroyed || (Owner && Owner->IsPendingKillPending())) return;
		assert(bRegistered && "Forgot to Call OnRegister: Super ");
	}
	if (!bRenderStateCreated && ShouldCreateRenderState())
	{
		CreateRenderState();
		if (!IsValid(this) || bIsBeingDestroyed || (Owner && Owner->IsPendingKillPending())) return;
		assert(bRenderStateCreated && "Forgot to Call CreateRenderState: Super ");
	}
}

void UActorComponent::ExecuteUnregisterEvents()
{
	if (bRenderStateCreated)
	{
		DestroyRenderState();
	}
	if (bRegistered)
	{
		OnUnregister();
	}
}
