#include "EnginePCH.h"
#include "Sequencer/LevelSequenceActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"

#include <algorithm>

ALevelSequenceActor::ALevelSequenceActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);

	UBillboardComponent* Icon = CreateDefaultSubobject<UBillboardComponent>("SequenceIcon");
	if (GetRootComponent())
	{
		Icon->SetupAttachment(GetRootComponent());
	}
	Icon->SetEditorIcon(true);
	Icon->SetHiddenInGame(true);
	Icon->SetIconSize(1.8f);
	Icon->SetHiddenInDetails(true);
	Icon->SetCastShadow(false);
	Icon->SetReceiveShadow(false);

	UMaterial* BaseMat = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* IconTex = UAssetManager::Get().LoadTexture("Assets/Icons/Toolbar/Play.png");
	if (BaseMat && IconTex)
	{
		UMaterial* MatInst = UMaterial::CreateInstance(BaseMat);
		MatInst->Textures[0] = IconTex;
		Icon->SetMaterial(0, MatInst);
	}
}

void ALevelSequenceActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoPlay && !SequenceAssetPath.empty())
	{
		PlaySequence();
	}
}

void ALevelSequenceActor::EndPlay()
{
	StopSequence();
	Super::EndPlay();
}

bool ALevelSequenceActor::ReloadSequenceAsset()
{
	if (SequenceAssetPath.empty())
	{
		bAssetLoaded = false;
		return false;
	}
	bAssetLoaded = FLevelSequenceAsset::LoadFromFile(SequenceAssetPath, SequenceData);
	return bAssetLoaded;
}

void ALevelSequenceActor::PlaySequence()
{
	if (bIsPlayingSequence && bIsPausedSequence)
	{
		bIsPausedSequence = false;
		return;
	}

	if (!bAssetLoaded)
	{
		if (!ReloadSequenceAsset())
		{
			return;
		}
	}

	CurrentFrame = static_cast<float>(SequenceData.StartFrame);
	PreviousFrame = -1.0f;
	if (bIsPlayingSequence) RestoreActorStates();
	CaptureActorStates();
	FLevelSequenceAsset::SpawnMissingActors(SequenceData, GetWorld());
	for (const FSequenceSpawnableActor& Entry : SequenceData.SpawnableActors)
	{
		if (Entry.ActorName.empty()) continue;
		AActor* Spawned = FLevelSequenceAsset::FindActorByNameInWorld(GetWorld(), Entry.ActorName);
		if (!Spawned) continue;
		bool bExisted = false;
		for (const FActorState& State : SavedActorStates)
			if (State.Actor == Spawned) { bExisted = true; break; }
		if (!bExisted) SpawnedSequenceActors.Add(Spawned);
	}
	bIsPlayingSequence = true;
	bIsPausedSequence = false;
	bHoldFinalCameraEffects = false;

	ApplyFrameEvaluation(CurrentFrame, PreviousFrame, true);
	PreviousFrame = CurrentFrame;
}

void ALevelSequenceActor::PauseSequence()
{
	if (bIsPlayingSequence)
	{
		bIsPausedSequence = true;
	}
}

void ALevelSequenceActor::StopSequence()
{
	bIsPlayingSequence = false;
	bIsPausedSequence = false;
	bHoldFinalCameraEffects = false;
	bHasActiveCameraCut = false;
	RestorePlayerCameraViewTarget();
	RestoreActorStates();
}

void ALevelSequenceActor::CaptureActorStates()
{
	SavedActorStates.Empty();
	SpawnedSequenceActors.Empty();
	UWorld* World = GetWorld();
	if (!World) return;

	for (const FSequenceTrack& Track : SequenceData.Tracks)
	{
		if (Track.bMuted || Track.TargetActorName.empty()) continue;
		if (Track.TrackType != ESequenceTrackType::ActorTransform &&
			Track.TrackType != ESequenceTrackType::ActorVisibility &&
			Track.TrackType != ESequenceTrackType::CameraFOV) continue;
		AActor* Target = FLevelSequenceAsset::FindActorByNameInWorld(World, Track.TargetActorName);
		if (!Target) continue;
		bool bSaved = false;
		for (const FActorState& State : SavedActorStates)
			if (State.Actor == Target) { bSaved = true; break; }
		if (bSaved) continue;

		FActorState State;
		State.Actor = Target;
		State.Transform = Target->GetActorTransform();
		State.Camera = Target->GetComponentByClass<UCameraComponent>();
		if (State.Camera) State.FieldOfView = State.Camera->GetFieldOfView();
		for (UActorComponent* Component : Target->GetComponents())
		{
			if (USceneComponent* Scene = Cast<USceneComponent>(Component))
				State.Components.Add({ Scene, Scene->IsVisible(), Scene->IsHiddenInGame() });
		}
		SavedActorStates.Add(std::move(State));
	}
	// 기존에 존재하던 spawnable도 종료 시 제거 대상에서 제외한다.
	for (const FSequenceSpawnableActor& Entry : SequenceData.SpawnableActors)
	{
		AActor* Target = FLevelSequenceAsset::FindActorByNameInWorld(World, Entry.ActorName);
		if (!Target) continue;
		bool bSaved = false;
		for (const FActorState& State : SavedActorStates)
			if (State.Actor == Target) { bSaved = true; break; }
		if (bSaved) continue;
		FActorState State;
		State.Actor = Target;
		State.bRestoreState = false;
		SavedActorStates.Add(std::move(State));
	}
}

void ALevelSequenceActor::RestoreActorStates()
{
	for (const FActorState& State : SavedActorStates)
	{
		if (!State.bRestoreState || !State.Actor || !IsValid(State.Actor) || State.Actor->IsActorBeingDestroyed()) continue;
		State.Actor->SetActorLocation(State.Transform.Location, false);
		State.Actor->SetActorRotation(State.Transform.Rotation, false);
		State.Actor->SetActorScale3D(State.Transform.Scale);
		if (State.Camera && IsValid(State.Camera)) State.Camera->SetFieldOfView(State.FieldOfView);
		for (const FComponentState& ComponentState : State.Components)
		{
			if (!ComponentState.Component || !IsValid(ComponentState.Component)) continue;
			ComponentState.Component->SetVisibility(ComponentState.bVisible);
			ComponentState.Component->SetHiddenInGame(ComponentState.bHiddenInGame);
		}
	}
	SavedActorStates.Empty();
	for (AActor* Actor : SpawnedSequenceActors)
		if (Actor && IsValid(Actor) && !Actor->IsActorBeingDestroyed()) Actor->Destroy();
	SpawnedSequenceActors.Empty();
}

void ALevelSequenceActor::SetCurrentFrame(float InFrame)
{
	if (!bAssetLoaded && !SequenceAssetPath.empty())
	{
		ReloadSequenceAsset();
	}
	const float Clamped = bAssetLoaded
		? FMath::Clamp(InFrame, static_cast<float>(SequenceData.StartFrame), static_cast<float>(SequenceData.EndFrame))
		: InFrame;
	if (bAssetLoaded) FLevelSequenceAsset::SpawnMissingActors(SequenceData, GetWorld());
	ApplyFrameEvaluation(Clamped, CurrentFrame, false);
	bHoldFinalCameraEffects = true;
	CurrentFrame = Clamped;
	PreviousFrame = Clamped;
}

void ALevelSequenceActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsPlayingSequence || bIsPausedSequence || !bAssetLoaded)
	{
		return;
	}

	const float Fps = static_cast<float>(std::max(SequenceData.FrameRate, 1));
	const float NextFrame = CurrentFrame + DeltaTime * Fps * PlayRate;
	const float MaxFrame = static_cast<float>(SequenceData.EndFrame);
	const float MinFrame = static_cast<float>(SequenceData.StartFrame);

	if (NextFrame >= MaxFrame)
	{
		if (SequenceData.bLoop || bLoopSequence)
		{
			ApplyFrameEvaluation(MaxFrame, PreviousFrame, true);
			CurrentFrame = MinFrame;
			PreviousFrame = -1.0f;
			ApplyFrameEvaluation(CurrentFrame, PreviousFrame, true);
			PreviousFrame = CurrentFrame;
			return;
		}

		CurrentFrame = MaxFrame;
		ApplyFrameEvaluation(CurrentFrame, PreviousFrame, true);
		PreviousFrame = CurrentFrame;
		StopSequence();
		bHoldFinalCameraEffects = true;
		OnSequenceFinished.Broadcast();
		return;
	}

	CurrentFrame = NextFrame;
	ApplyFrameEvaluation(CurrentFrame, PreviousFrame, true);
	PreviousFrame = CurrentFrame;
}

void ALevelSequenceActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV) const
{
	if (bHasActiveCameraCut)
	{
		OutPOV = ActiveCameraPOV;
		return;
	}
	Super::CalcCamera(DeltaTime, OutPOV);
}

void ALevelSequenceActor::ApplyFrameEvaluation(float EvalFrame, float PrevEvalFrame, bool bTriggerAudio)
{
	UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld || !bAssetLoaded)
	{
		return;
	}

	bool bCutActive = false;
	FMinimalViewInfo EvaluatedPOV{};
	FLevelSequenceAsset::EvaluateWorldAtFrame(
		SequenceData,
		CurrentWorld,
		EvalFrame,
		PrevEvalFrame,
		bTriggerAudio,
		&EvaluatedPOV,
		&bCutActive);

	bHasActiveCameraCut = bCutActive;
	if (bCutActive)
	{
		ActiveCameraPOV = EvaluatedPOV;
	}

	if (bOverridePlayerCamera && bHasActiveCameraCut)
	{
		if (APlayerController* PC = CurrentWorld->GetPlayerController())
		{
			if (PC->GetViewTarget() != this)
			{
				if (!bOverrodeCameraTarget)
				{
					PreviousCameraTarget = PC->GetViewTarget();
					bOverrodeCameraTarget = true;
				}
				PC->SetViewTarget(this);
			}
		}
	}
	else if (bOverrodeCameraTarget) // 컷신 중간에 카메라 씬 없으면 플레이어 카메라 사용했다가 다시 카메라 씬 실행
	{
		RestorePlayerCameraViewTarget();
	}
}

void ALevelSequenceActor::RestorePlayerCameraViewTarget()
{
	if (!bOverrodeCameraTarget)
	{
		return;
	}

	bOverrodeCameraTarget = false;
	UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld)
	{
		PreviousCameraTarget = nullptr;
		return;
	}

	if (APlayerController* PC = CurrentWorld->GetPlayerController())
	{
		if (PC->GetViewTarget() == this)
		{
			AActor* Fallback = PreviousCameraTarget;
			if (!Fallback || !IsValid(Fallback) || Fallback->IsActorBeingDestroyed())
			{
				Fallback = reinterpret_cast<AActor*>(CurrentWorld->GetPlayerPawn());
			}
			PC->SetViewTarget(Fallback);
		}
	}
	PreviousCameraTarget = nullptr;
}
