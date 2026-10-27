#include "EnginePCH.h"
#include "Sequencer/SequencePlayer.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraComponent.h"
#include "Serialization/JsonArchive.h"
#include "UObject/UObjectGlobals.h"
#include <cmath>

namespace
{
bool IsLiveActor(UWorld* World, AActor* Actor)
{
	return Actor && GUObjectArray.Find(Actor) != INDEX_NONE && IsValid(Actor) &&
		!Actor->IsActorBeingDestroyed() && (!World || Actor->GetWorld() == World);
}

void ApplyTransform(AActor* Actor, const FTransform& Transform)
{
	if (USceneComponent* Root = Actor->GetRootComponent())
	{
		Root->SetWorldLocationAndRotation(Transform.Location, Transform.Rotation, false);
		Root->SetWorldScale3D(Transform.Scale);
	}
}

float Lerp(float A, float B, float Alpha) { return A + (B - A) * Alpha; }
float LerpAngle(float A, float B, float Alpha) { return A + std::remainder(B - A, 360.0f) * Alpha; }
FVector Lerp(const FVector& A, const FVector& B, float Alpha) { return A + (B - A) * Alpha; }
FTransform Lerp(const FTransform& A, const FTransform& B, float Alpha)
{
	return {FRotator(LerpAngle(A.Rotation.Pitch, B.Rotation.Pitch, Alpha),
		LerpAngle(A.Rotation.Yaw, B.Rotation.Yaw, Alpha), LerpAngle(A.Rotation.Roll, B.Rotation.Roll, Alpha)),
		Lerp(A.Location, B.Location, Alpha), Lerp(A.Scale, B.Scale, Alpha)};
}

template <typename Key, typename Value>
bool Sample(const std::vector<Key>& Keys, int32 Frame, Value& Out)
{
	if (Keys.empty()) return false;
	if (Frame <= Keys.front().Frame) { Out = Keys.front().Value; return true; }
	for (size_t I = 1; I < Keys.size(); ++I)
	{
		if (Frame > Keys[I].Frame) continue;
		const Key& Left = Keys[I - 1];
		const Key& Right = Keys[I];
		const float Alpha = Left.Interpolation == ESequenceInterpolation::Constant ? 0.0f :
			static_cast<float>(Frame - Left.Frame) / static_cast<float>(Right.Frame - Left.Frame);
		Out = Lerp(Left.Value, Right.Value, Alpha);
		return true;
	}
	Out = Keys.back().Value;
	return true;
}
}

void FPreAnimatedState::Capture(AActor* Actor, ESequenceTrackType Property)
{
	if (!Actor) return;
	for (FSnapshot& Snapshot : Snapshots)
		if (Snapshot.Actor == Actor)
		{
			Snapshot.bRestoreTransform |= Property == ESequenceTrackType::Transform;
			Snapshot.bRestoreFov |= Property == ESequenceTrackType::FieldOfView;
			return;
		}
	UCameraComponent* Camera = Actor->GetComponentByClass<UCameraComponent>();
	Snapshots.push_back({Actor, Actor->GetActorTransform(), Camera ? Camera->GetFieldOfView() : 0.0f,
		Property == ESequenceTrackType::Transform, Property == ESequenceTrackType::FieldOfView && Camera != nullptr});
}

void FPreAnimatedState::Restore(UWorld* World, bool bClear)
{
	if (!World || GUObjectArray.Find(World) == INDEX_NONE || !IsValid(World))
	{
		if (bClear) Snapshots.clear();
		return;
	}
	for (const FSnapshot& Snapshot : Snapshots)
	{
		if (!IsLiveActor(World, Snapshot.Actor)) continue;
		if (Snapshot.bRestoreTransform) ApplyTransform(Snapshot.Actor, Snapshot.Transform);
		if (Snapshot.bRestoreFov)
			if (UCameraComponent* Camera = Snapshot.Actor->GetComponentByClass<UCameraComponent>())
				Camera->SetFieldOfView(Snapshot.FieldOfView);
	}
	if (bClear) Snapshots.clear();
}

AActor* FSequenceBindingResolver::FindActor(UWorld* World, const FString& PersistentId)
{
	if (!World || PersistentId.empty()) return nullptr;
	for (ULevel* Level : World->GetLevels())
		if (Level) for (AActor* Actor : Level->GetActors())
			if (IsLiveActor(World, Actor) && Actor->GetPersistentActorId() == PersistentId) return Actor;
	return nullptr;
}

bool FSequenceBindingResolver::Resolve(UWorld* World, const ULevelSequence& Sequence, bool bPreview,
	const std::unordered_map<FString, FString>& Overrides,
	std::unordered_map<FString, AActor*>& OutActors, std::vector<AActor*>& OutSpawned)
{
	if (!World) return false;
	for (const FSequenceBinding& Binding : Sequence.Bindings)
	{
		AActor* Actor = nullptr;
		const bool bSpawn = Binding.Type == ESequenceBindingType::Spawnable ||
			(Binding.Type == ESequenceBindingType::Replaceable && bPreview);
		if (bSpawn)
		{
			UClass* Class = FindClass(Binding.ActorClass);
			if (Class && Class->IsChildOf(AActor::StaticClass()))
			{
				Actor = World->SpawnActor(Class, NAME_None, &Binding.SpawnTransform);
				if (Actor)
				{
					Actor->SetFlags(EObjectFlags::RF_Transient);
					if (!Binding.ActorTemplateJson.empty() &&
						!FJsonArchive::LoadActorFromString(Actor, Binding.ActorTemplateJson))
					{
						World->DestroyActor(Actor);
						Actor = nullptr;
					}
					if (Actor)
					{
						if (!Binding.ActorTemplateJson.empty())
						{
							Actor->RegeneratePersistentActorId();
							ApplyTransform(Actor, Binding.SpawnTransform);
							Actor->RegisterAllComponents();
						}
						OutSpawned.push_back(Actor);
					}
				}
			}
		}
		else
		{
			auto Override = Overrides.find(Binding.Id);
			const FString TargetId = Override != Overrides.end() ? Override->second :
				(Binding.Type == ESequenceBindingType::Replaceable ? FString{} : Binding.ActorId);
			Actor = FindActor(World, TargetId);
		}
		if (!Actor)
		{
			HTR_LOG(Warning, "Sequence binding '{}' ({}) has no target in this world", Binding.Id,
				Binding.Type == ESequenceBindingType::Spawnable ? "Spawnable" :
				Binding.Type == ESequenceBindingType::Replaceable ? "Replaceable" : "Possessable");
			return false;
		}
		OutActors.emplace(Binding.Id, Actor);
	}
	return true;
}

void FSequenceEvaluator::Evaluate(const ULevelSequence& Sequence,
	const std::unordered_map<FString, AActor*>& Actors, int32 Frame)
{
	for (const FSequenceTrack& Track : Sequence.Tracks)
	{
		auto Found = Actors.find(Track.BindingId);
		if (Found == Actors.end() || !IsLiveActor(nullptr, Found->second)) continue;
		// A later section in the same track takes precedence when sections overlap.
		const FSequenceSection* Active = nullptr;
		for (const FSequenceSection& Section : Track.Sections)
			if (Section.StartFrame <= Frame && Frame <= Section.EndFrame &&
				(!Active || Section.StartFrame >= Active->StartFrame)) Active = &Section;
		if (!Active) continue;
		if (Track.Type == ESequenceTrackType::Transform)
		{
			FTransform Value;
			if (Sample(Active->TransformChannel.Keys, Frame, Value)) ApplyTransform(Found->second, Value);
		}
		else if (UCameraComponent* Camera = Found->second->GetComponentByClass<UCameraComponent>())
		{
			float Value = 0.0f;
			if (Sample(Active->FloatChannel.Keys, Frame, Value)) Camera->SetFieldOfView(Value);
		}
	}
}

bool ULevelSequencePlayer::Initialize(UWorld* InWorld, const ULevelSequence* InSequence, bool bInPreview)
{
	Stop();
	World = InWorld;
	Sequence = InSequence;
	bPreview = bInPreview;
	Overrides.clear();
	CurrentFrame = Sequence ? Sequence->StartFrame : 0;
	FramePosition = CurrentFrame;
	return World && Sequence && Sequence->IsValidSequence();
}

void ULevelSequencePlayer::SetBindingOverride(const FString& BindingId, const FString& PersistentActorId)
{
	if (bActive) Stop();
	Overrides[BindingId] = PersistentActorId;
}

AActor* ULevelSequencePlayer::GetBoundActor(const FString& BindingId) const
{
	const auto Found = Actors.find(BindingId);
	return Found != Actors.end() && IsLiveActor(World, Found->second) ? Found->second : nullptr;
}

bool ULevelSequencePlayer::BeginSession()
{
	if (!World || GUObjectArray.Find(World) == INDEX_NONE || !IsValid(World) ||
		!Sequence || !Sequence->IsValidSequence()) return false;
	if (!FSequenceBindingResolver::Resolve(World, *Sequence, bPreview, Overrides, Actors, SpawnedActors))
	{
		Stop();
		return false;
	}
	for (const FSequenceTrack& Track : Sequence->Tracks)
	{
		if (auto Found = Actors.find(Track.BindingId); Found != Actors.end())
			PreAnimatedState.Capture(Found->second, Track.Type);
	}
	bActive = true;
	return true;
}

bool ULevelSequencePlayer::Play()
{
	if (!bActive && !BeginSession()) return false;
	if (Sequence && CurrentFrame >= Sequence->EndFrame)
	{
		CurrentFrame = Sequence->StartFrame;
		FramePosition = CurrentFrame;
	}
	EvaluateCurrentFrame();
	bPlaying = true;
	return true;
}

bool ULevelSequencePlayer::Seek(int32 Frame)
{
	if (!Sequence || Frame < Sequence->StartFrame || Frame > Sequence->EndFrame) return false;
	if (!bActive && !BeginSession()) return false;
	CurrentFrame = Frame;
	FramePosition = Frame;
	EvaluateCurrentFrame();
	return true;
}

void ULevelSequencePlayer::Tick(float DeltaTime)
{
	if (!bPlaying || !bActive || !Sequence || DeltaTime <= 0.0f) return;
	FramePosition += static_cast<double>(DeltaTime) * Sequence->FramesPerSecond * PlayRate;
	if (FramePosition >= Sequence->EndFrame)
	{
		if (bLooping && Sequence->EndFrame > Sequence->StartFrame)
		{
			const double Length = static_cast<double>(Sequence->EndFrame - Sequence->StartFrame);
			FramePosition = Sequence->StartFrame + std::fmod(FramePosition - Sequence->StartFrame, Length);
		}
		else
		{
			CurrentFrame = Sequence->EndFrame;
			EvaluateCurrentFrame();
			Stop();
			return;
		}
	}
	CurrentFrame = static_cast<int32>(FramePosition);
	EvaluateCurrentFrame();
}

void ULevelSequencePlayer::EvaluateCurrentFrame()
{
	PreAnimatedState.Restore(World, false);
	FSequenceEvaluator::Evaluate(*Sequence, Actors, CurrentFrame);
}

void ULevelSequencePlayer::Stop()
{
	bPlaying = false;
	PreAnimatedState.Restore(World);
	if (World && GUObjectArray.Find(World) != INDEX_NONE && IsValid(World))
		for (AActor* Actor : SpawnedActors)
			if (IsLiveActor(World, Actor)) World->DestroyActor(Actor);
	SpawnedActors.clear();
	Actors.clear();
	bActive = false;
}
