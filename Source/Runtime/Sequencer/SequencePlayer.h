#pragma once

#include "Sequencer/LevelSequence.h"
#include <unordered_map>
#include <vector>

class AActor;
class UWorld;

class FPreAnimatedState
{
public:
	void Capture(AActor* Actor, ESequenceTrackType Property);
	void Restore(UWorld* World, bool bClear = true);
private:
	struct FSnapshot { AActor* Actor; FTransform Transform; float FieldOfView; bool bRestoreTransform; bool bRestoreFov; };
	std::vector<FSnapshot> Snapshots;
};

class FSequenceBindingResolver
{
public:
	static AActor* FindActor(UWorld* World, const FString& PersistentId);
	static bool Resolve(UWorld* World, const ULevelSequence& Sequence, bool bPreview,
		const std::unordered_map<FString, FString>& Overrides,
		std::unordered_map<FString, AActor*>& OutActors, std::vector<AActor*>& OutSpawned);
};

class FSequenceEvaluator
{
public:
	static void Evaluate(const ULevelSequence& Sequence, const std::unordered_map<FString, AActor*>& Actors, int32 Frame);
};

class ULevelSequencePlayer : public UObject
{
	DECLARE_CLASS(ULevelSequencePlayer, UObject)
public:
	~ULevelSequencePlayer() override { Stop(); }
	bool Initialize(UWorld* InWorld, const ULevelSequence* InSequence, bool bInPreview = false);
	void SetBindingOverride(const FString& BindingId, const FString& PersistentActorId);
	bool Play();
	bool Seek(int32 Frame);
	void Pause() { bPlaying = false; }
	void Stop();
	void Tick(float DeltaTime);
	void SetLooping(bool bInLooping) { bLooping = bInLooping; }
	void SetPlayRate(float Rate) { if (Rate > 0.0f) PlayRate = Rate; }
	bool IsPlaying() const { return bPlaying; }
	bool IsActive() const { return bActive; }
	int32 GetCurrentFrame() const { return CurrentFrame; }
	AActor* GetBoundActor(const FString& BindingId) const;
private:
	bool BeginSession();
	void EvaluateCurrentFrame();
	UWorld* World = nullptr;
	const ULevelSequence* Sequence = nullptr;
	bool bPreview = false;
	bool bActive = false;
	bool bPlaying = false;
	bool bLooping = false;
	float PlayRate = 1.0f;
	double FramePosition = 0.0;
	int32 CurrentFrame = 0;
	std::unordered_map<FString, FString> Overrides;
	std::unordered_map<FString, AActor*> Actors;
	std::vector<AActor*> SpawnedActors;
	FPreAnimatedState PreAnimatedState;
};
