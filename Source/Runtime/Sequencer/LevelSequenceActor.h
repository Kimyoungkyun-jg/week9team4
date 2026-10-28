#pragma once

#include "GameFramework/Actor.h"
#include "Sequencer/SequenceAsset.h"
#include "Camera/MinimalViewInfo.h"
#include "Lua/LuaMacros.h"
#include "Core/Delegates/Delegate.h"

// 레벨에 배치하여 컷신을 재생하는 시퀀스 액터
class ALevelSequenceActor : public AActor
{
	DECLARE_CLASS(ALevelSequenceActor, AActor)

	REFLECT_START(ClassName)
		PROPERTY(SequenceAssetPath)
		PROPERTY(bAutoPlay)
		PROPERTY(bLoopSequence)
		PROPERTY(PlayRate)
		PROPERTY(bOverridePlayerCamera)
		PROPERTY(bDisablePlayerInput)
		PROPERTY(RestoreBlendTime)
	REFLECT_END()

	LUA_BIND_START(ALevelSequenceActor)
		LUA_FUNC(PlaySequence)
		LUA_FUNC(PauseSequence)
		LUA_FUNC(StopSequence)
		LUA_FUNC(IsPlayingSequence)
		LUA_FUNC(SetCurrentFrame)
		LUA_FUNC(GetCurrentFrame)
		LUA_FUNC(SetSequenceAssetPath)
		LUA_FUNC(GetSequenceAssetPath)
		LUA_FUNC(SetOnFinishedCallback)
		LUA_FUNC(SetRestoreBlendTime)
		LUA_FUNC(GetRestoreBlendTime)
	LUA_BIND_END()

public:
	ALevelSequenceActor();

	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void EndPlay() override;
	void CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV) const override;

	void PlaySequence();
	void PauseSequence();
	void StopSequence();
	TMulticastDelegate<> OnSequenceFinished;

	bool IsPlayingSequence() const { return bIsPlayingSequence && !bIsPausedSequence; }
	bool HasCameraEffectsView() const { return bIsPlayingSequence || bHoldFinalCameraEffects; }
	bool IsPausedSequence() const { return bIsPausedSequence; }

	void SetCurrentFrame(float InFrame);
	float GetCurrentFrame() const { return CurrentFrame; }

	void SetSequenceAssetPath(const FString& InPath) { SequenceAssetPath = InPath; bAssetLoaded = false; }
	const FString& GetSequenceAssetPath() const { return SequenceAssetPath; }

	void SetOnFinishedCallback(const sol::function& InCallback) { LuaFinishedCallback = InCallback; }
	void SetRestoreBlendTime(float InBlendTime) { RestoreBlendTime = std::max(0.0f, InBlendTime); }
	float GetRestoreBlendTime() const { return RestoreBlendTime; }
	void SetDisablePlayerInput(bool bDisable) { bDisablePlayerInput = bDisable; }
	bool GetDisablePlayerInput() const { return bDisablePlayerInput; }

	bool ReloadSequenceAsset();
	const FLevelSequenceData& GetSequenceData() const { return SequenceData; }

private:
	struct FComponentState
	{
		class USceneComponent* Component = nullptr;
		bool bVisible = true;
		bool bHiddenInGame = false;
	};
	struct FActorState
	{
		AActor* Actor = nullptr;
		bool bRestoreState = true;
		FTransform Transform{};
		class UCameraComponent* Camera = nullptr;
		float FieldOfView = 0.0f;
		TArray<FComponentState> Components;
	};
	void CaptureActorStates();
	void RestoreActorStates();
	void ApplyFrameEvaluation(float EvalFrame, float PrevEvalFrame, bool bTriggerAudio);
	void RestorePlayerCameraViewTarget();
	void RestorePlayerInput();

	FString SequenceAssetPath;
	bool bAutoPlay = true;
	bool bLoopSequence = false;
	float PlayRate = 1.0f;
	bool bOverridePlayerCamera = true;
	bool bDisablePlayerInput = true;
	float RestoreBlendTime = 0.0f;

	FLevelSequenceData SequenceData;
	bool bAssetLoaded = false;
	bool bIsPlayingSequence = false;
	bool bIsPausedSequence = false;
	bool bHoldFinalCameraEffects = false;
	float CurrentFrame = 0.0f;
	float PreviousFrame = -1.0f;

	FMinimalViewInfo ActiveCameraPOV{};
	bool bHasActiveCameraCut = false;
	bool bOverrodeCameraTarget = false;
	bool bInputDisabledBySequence = false;
	AActor* PreviousCameraTarget = nullptr;
	TArray<FActorState> SavedActorStates;
	TArray<AActor*> SpawnedSequenceActors;
	sol::function LuaFinishedCallback;
};
