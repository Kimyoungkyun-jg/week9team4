#include "EnginePCH.h"
#include "Sequencer/LevelSequenceActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BillboardComponent.h"
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
	RestorePlayerInput();
	OnSequenceFinished.Clear();
	LuaFinishedCallback = sol::nil;
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
	FLevelSequenceAsset::SpawnMissingActors(SequenceData, GetWorld());
	bIsPlayingSequence = true;
	bIsPausedSequence = false;
	bHoldFinalCameraEffects = false;

	if (bDisablePlayerInput)
	{
		if (UWorld* CurrentWorld = GetWorld())
		{
			if (APlayerController* PC = CurrentWorld->GetPlayerController())
			{
				if (!bInputDisabledBySequence)
				{
					PC->SetIgnoreMoveInput(true);
					PC->SetIgnoreLookInput(true);
					bInputDisabledBySequence = true;
				}
			}
		}
	}

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
	RestorePlayerInput();
	RestorePlayerCameraViewTarget();
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
		if (LuaFinishedCallback.valid())
		{
			LuaFinishedCallback();
		}
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
	else if (bOverrodeCameraTarget)
	{
		RestorePlayerCameraViewTarget();
	}
}

void ALevelSequenceActor::RestorePlayerInput()
{
	if (bInputDisabledBySequence)
	{
		bInputDisabledBySequence = false;
		if (UWorld* CurrentWorld = GetWorld())
		{
			if (APlayerController* PC = CurrentWorld->GetPlayerController())
			{
				PC->SetIgnoreMoveInput(false);
				PC->SetIgnoreLookInput(false);
			}
		}
	}
}

void ALevelSequenceActor::RestorePlayerCameraViewTarget()
{
	RestorePlayerInput();
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
			if (RestoreBlendTime > 0.0f)
			{
				PC->SetViewTargetWithBlend(Fallback, RestoreBlendTime, VTBlend_Cubic, 2.0f);
			}
			else
			{
				PC->SetViewTarget(Fallback);
			}
		}
	}
	PreviousCameraTarget = nullptr;
}
