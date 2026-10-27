#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Math/Transform.h"
#include <vector>

enum class ESequenceBindingType { Spawnable, Possessable, Replaceable };
enum class ESequenceTrackType { Transform, FieldOfView };
enum class ESequenceInterpolation { Constant, Linear };

struct FSequenceBinding
{
	FString Id;
	ESequenceBindingType Type = ESequenceBindingType::Possessable;
	FString ActorClass;       // Spawnable and Replaceable preview class.
	FString ActorId;          // Default Possessable target in the authored scene.
	FString SourceActorName;  // Name of the Actor captured for a Spawnable.
	FString ActorTemplateJson; // Serialized Actor and components; empty for class-only spawning.
	FTransform SpawnTransform;
};

struct FSequenceTransformKey
{
	int32 Frame = 0;
	FTransform Value;
	ESequenceInterpolation Interpolation = ESequenceInterpolation::Linear;
};

struct FSequenceFloatKey
{
	int32 Frame = 0;
	float Value = 0.0f;
	ESequenceInterpolation Interpolation = ESequenceInterpolation::Linear;
};

template <typename TKey>
struct FSequenceChannel
{
	std::vector<TKey> Keys;
};

struct FSequenceSection
{
	int32 StartFrame = 0;
	int32 EndFrame = 0;
	FSequenceChannel<FSequenceTransformKey> TransformChannel;
	FSequenceChannel<FSequenceFloatKey> FloatChannel;
};

struct FSequenceTrack
{
	FString BindingId;
	ESequenceTrackType Type = ESequenceTrackType::Transform;
	std::vector<FSequenceSection> Sections;
};

class ULevelSequence : public UObject
{
	DECLARE_CLASS(ULevelSequence, UObject)
public:
	int32 FramesPerSecond = 30;
	int32 StartFrame = 0;
	int32 EndFrame = 0;
	std::vector<FSequenceBinding> Bindings;
	std::vector<FSequenceTrack> Tracks;

	bool SaveToFile(const FString& Path) const;
	bool LoadFromFile(const FString& Path);
	FString SerializeToString() const;
	bool DeserializeFromString(const FString& Content);
	bool IsValidSequence() const;
};
