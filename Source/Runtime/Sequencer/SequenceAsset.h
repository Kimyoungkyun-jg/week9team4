#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Camera/MinimalViewInfo.h"

class UWorld;
class AActor;

// 시퀀서 트랙 종류
enum class ESequenceTrackType : int32
{
	CameraCut = 0,
	ActorTransform = 1,
	CameraFOV = 2,
	Audio = 3,
	ActorVisibility = 4,
	CameraEffects = 5
};

// 키프레임 보간 방식
enum class ESequenceInterpMode : int32
{
	Linear = 0,
	Cubic = 1,
	Constant = 2
};

// 카메라 전환 구간 클립
struct FCameraCutClip
{
	int32 StartFrame = 0;
	int32 EndFrame = 60;
	FString CameraActorName;
	float BlendTime = 0.0f;
};

// 액터 트랜스폼 키프레임
struct FTransformKeyframe
{
	int32 Frame = 0;
	FVector Location = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);
	ESequenceInterpMode InterpMode = ESequenceInterpMode::Linear;
};

// 카메라 시야각 키프레임
struct FFOVKeyframe
{
	int32 Frame = 0;
	float FieldOfView = 60.0f;
	ESequenceInterpMode InterpMode = ESequenceInterpMode::Linear;
};

struct FVisibilityKeyframe
{
	int32 Frame = 0;
	bool bVisible = true;
	bool bHiddenInGame = false;
};

struct FCameraEffectsKeyframe
{
	int32 Frame = 0;
	float FadeOpacity = 0.0f;
	float LetterboxSize = 0.0f;
	ESequenceInterpMode InterpMode = ESequenceInterpMode::Linear;
};

struct FSequenceCameraEffects
{
	float FadeOpacity = 0.0f;
	float LetterboxSize = 0.0f;
};

// 오디오 재생 구간 클립
struct FAudioClip
{
	int32 StartFrame = 0;
	int32 EndFrame = 30;
	FString SoundPath;
	bool bIsBGM = false;
	bool bLoopBGM = false;
	float Volume = 1.0f;
};

// 단일 시퀀스 트랙 데이터
struct FSequenceTrack
{
	FString TrackName;
	ESequenceTrackType TrackType = ESequenceTrackType::ActorTransform;
	FString TargetActorName;
	bool bExpanded = true;
	bool bMuted = false;
	bool bConstantSpeed = false;
	bool bSmoothPath = false;

	TArray<FCameraCutClip> CameraCuts;
	TArray<FTransformKeyframe> TransformKeys;
	TArray<FFOVKeyframe> FOVKeys;
	TArray<FVisibilityKeyframe> VisibilityKeys;
	TArray<FCameraEffectsKeyframe> CameraEffectsKeys;
	TArray<FAudioClip> AudioClips;
};

// 시퀀스가 참조하지만 현재 레벨에는 없을 때 생성할 액터
struct FSequenceSpawnableActor
{
	FString ActorName;
	FString ClassName;
	FTransform InitialTransform{};
	float InitialFOV = 60.0f;
};

// 시퀀스 전체 데이터
struct FLevelSequenceData
{
	int32 FrameRate = 30;
	int32 StartFrame = 0;
	int32 EndFrame = 150;
	bool bLoop = false;
	TArray<FSequenceTrack> Tracks;
	TArray<FSequenceSpawnableActor> SpawnableActors;
};

// 시퀀스 직렬화 및 평가 유틸리티
class FLevelSequenceAsset
{
public:
	static bool SaveToFile(const FLevelSequenceData& Data, const FString& Path);
	static bool LoadFromFile(const FString& Path, FLevelSequenceData& OutData);
	static FString SerializeToString(const FLevelSequenceData& Data);
	static bool DeserializeFromString(const FString& Content, FLevelSequenceData& OutData);

	static void SortTrackKeys(FSequenceTrack& Track);
	static float ComputeInterpAlpha(float RawAlpha, ESequenceInterpMode Mode);
	static FRotator LerpRotatorShortest(const FRotator& StartRot, const FRotator& EndRot, float Alpha);
	static FTransform EvaluateTransformTrack(const FSequenceTrack& Track, float CurrentFrame, const FTransform& DefaultTransform);
	static float EvaluateFOVTrack(const FSequenceTrack& Track, float CurrentFrame, float DefaultFOV);
	static FSequenceCameraEffects EvaluateCameraEffects(const FLevelSequenceData& Data, float CurrentFrame);
	static AActor* FindActorByNameInWorld(UWorld* World, const FString& ActorName);
	static void SpawnMissingActors(const FLevelSequenceData& Data, UWorld* World);

	static void EvaluateWorldAtFrame(
		const FLevelSequenceData& Data,
		UWorld* World,
		float CurrentFrame,
		float PrevFrame,
		bool bTriggerAudio,
		FMinimalViewInfo* OutCameraPOV,
		bool* OutHasCameraCut);
};
