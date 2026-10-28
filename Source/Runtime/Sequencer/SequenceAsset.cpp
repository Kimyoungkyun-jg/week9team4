#include "EnginePCH.h"
#include "Sequencer/SequenceAsset.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Audio/SoundManager.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Serialization/TypeSerializer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace
{
	constexpr int32 SequenceAssetVersion = 1;

	float NormalizeAngleDelta(float Angle)
	{
		float Result = std::fmod(Angle + 180.0f, 360.0f);
		if (Result < 0.0f)
		{
			Result += 360.0f;
		}
		return Result - 180.0f;
	}
}

void FLevelSequenceAsset::SortTrackKeys(FSequenceTrack& Track)
{
	std::sort(Track.CameraCuts.begin(), Track.CameraCuts.end(),
		[](const FCameraCutClip& Left, const FCameraCutClip& Right)
		{
			return Left.StartFrame < Right.StartFrame;
		});

	std::sort(Track.TransformKeys.begin(), Track.TransformKeys.end(),
		[](const FTransformKeyframe& Left, const FTransformKeyframe& Right)
		{
			return Left.Frame < Right.Frame;
		});

	std::sort(Track.FOVKeys.begin(), Track.FOVKeys.end(),
		[](const FFOVKeyframe& Left, const FFOVKeyframe& Right)
		{
			return Left.Frame < Right.Frame;
		});

	std::sort(Track.AudioClips.begin(), Track.AudioClips.end(),
		[](const FAudioClip& Left, const FAudioClip& Right)
		{
			return Left.StartFrame < Right.StartFrame;
		});
}

float FLevelSequenceAsset::ComputeInterpAlpha(float RawAlpha, ESequenceInterpMode Mode)
{
	const float Clamped = FMath::Clamp(RawAlpha, 0.0f, 1.0f);
	if (Mode == ESequenceInterpMode::Constant)
	{
		return 0.0f;
	}
	if (Mode == ESequenceInterpMode::Cubic)
	{
		return Clamped * Clamped * (3.0f - 2.0f * Clamped);
	}
	return Clamped;
}

FRotator FLevelSequenceAsset::LerpRotatorShortest(const FRotator& StartRot, const FRotator& EndRot, float Alpha)
{
	FRotator Result;
	Result.Pitch = StartRot.Pitch + NormalizeAngleDelta(EndRot.Pitch - StartRot.Pitch) * Alpha;
	Result.Yaw = StartRot.Yaw + NormalizeAngleDelta(EndRot.Yaw - StartRot.Yaw) * Alpha;
	Result.Roll = StartRot.Roll + NormalizeAngleDelta(EndRot.Roll - StartRot.Roll) * Alpha;
	return Result;
}

FTransform FLevelSequenceAsset::EvaluateTransformTrack(const FSequenceTrack& Track, float CurrentFrame, const FTransform& DefaultTransform)
{
	if (Track.TransformKeys.IsEmpty())
	{
		return DefaultTransform;
	}

	if (Track.TransformKeys.Num() == 1 || CurrentFrame <= static_cast<float>(Track.TransformKeys[0].Frame))
	{
		const FTransformKeyframe& Key = Track.TransformKeys[0];
		FTransform OutTransform;
		OutTransform.Location = Key.Location;
		OutTransform.Rotation = Key.Rotation;
		OutTransform.Scale = Key.Scale;
		return OutTransform;
	}

	const int32 LastIdx = Track.TransformKeys.Num() - 1;
	if (CurrentFrame >= static_cast<float>(Track.TransformKeys[LastIdx].Frame))
	{
		const FTransformKeyframe& Key = Track.TransformKeys[LastIdx];
		FTransform OutTransform;
		OutTransform.Location = Key.Location;
		OutTransform.Rotation = Key.Rotation;
		OutTransform.Scale = Key.Scale;
		return OutTransform;
	}

	for (int32 Index = 0; Index < LastIdx; ++Index)
	{
		const FTransformKeyframe& KeyA = Track.TransformKeys[Index];
		const FTransformKeyframe& KeyB = Track.TransformKeys[Index + 1];
		if (CurrentFrame >= static_cast<float>(KeyA.Frame) && CurrentFrame <= static_cast<float>(KeyB.Frame))
		{
			const float Span = static_cast<float>(KeyB.Frame - KeyA.Frame);
			const float RawAlpha = (Span > 0.0001f) ? ((CurrentFrame - static_cast<float>(KeyA.Frame)) / Span) : 0.0f;
			const float Alpha = ComputeInterpAlpha(RawAlpha, KeyA.InterpMode);

			FTransform OutTransform;
			OutTransform.Location = KeyA.Location + (KeyB.Location - KeyA.Location) * Alpha;
			OutTransform.Rotation = LerpRotatorShortest(KeyA.Rotation, KeyB.Rotation, Alpha);
			OutTransform.Scale = KeyA.Scale + (KeyB.Scale - KeyA.Scale) * Alpha;
			return OutTransform;
		}
	}

	return DefaultTransform;
}

float FLevelSequenceAsset::EvaluateFOVTrack(const FSequenceTrack& Track, float CurrentFrame, float DefaultFOV)
{
	if (Track.FOVKeys.IsEmpty())
	{
		return DefaultFOV;
	}

	if (Track.FOVKeys.Num() == 1 || CurrentFrame <= static_cast<float>(Track.FOVKeys[0].Frame))
	{
		return Track.FOVKeys[0].FieldOfView;
	}

	const int32 LastIdx = Track.FOVKeys.Num() - 1;
	if (CurrentFrame >= static_cast<float>(Track.FOVKeys[LastIdx].Frame))
	{
		return Track.FOVKeys[LastIdx].FieldOfView;
	}

	for (int32 Index = 0; Index < LastIdx; ++Index)
	{
		const FFOVKeyframe& KeyA = Track.FOVKeys[Index];
		const FFOVKeyframe& KeyB = Track.FOVKeys[Index + 1];
		if (CurrentFrame >= static_cast<float>(KeyA.Frame) && CurrentFrame <= static_cast<float>(KeyB.Frame))
		{
			const float Span = static_cast<float>(KeyB.Frame - KeyA.Frame);
			const float RawAlpha = (Span > 0.0001f) ? ((CurrentFrame - static_cast<float>(KeyA.Frame)) / Span) : 0.0f;
			const float Alpha = ComputeInterpAlpha(RawAlpha, KeyA.InterpMode);
			return KeyA.FieldOfView + (KeyB.FieldOfView - KeyA.FieldOfView) * Alpha;
		}
	}

	return DefaultFOV;
}

AActor* FLevelSequenceAsset::FindActorByNameInWorld(UWorld* World, const FString& ActorName)
{
	if (!World || ActorName.empty())
	{
		return nullptr;
	}

	ULevel* Level = World->GetPersistentLevel();
	if (!Level)
	{
		return nullptr;
	}

	for (AActor* Actor : Level->GetActors())
	{
		if (Actor && IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->GetName() == ActorName)
		{
			return Actor;
		}
	}

	return nullptr;
}

void FLevelSequenceAsset::EvaluateWorldAtFrame(
	const FLevelSequenceData& Data,
	UWorld* World,
	float CurrentFrame,
	float PrevFrame,
	bool bTriggerAudio,
	FMinimalViewInfo* OutCameraPOV,
	bool* OutHasCameraCut)
{
	if (OutHasCameraCut)
	{
		*OutHasCameraCut = false;
	}
	if (!World)
	{
		return;
	}

	// 트랜스폼 및 카메라 시야각 트랙 먼저 평가
	for (const FSequenceTrack& Track : Data.Tracks)
	{
		if (Track.bMuted)
		{
			continue;
		}

		if (Track.TrackType == ESequenceTrackType::ActorTransform)
		{
			AActor* Target = FindActorByNameInWorld(World, Track.TargetActorName);
			if (Target && !Track.TransformKeys.IsEmpty())
			{
				const FTransform EvalTransform = EvaluateTransformTrack(Track, CurrentFrame, Target->GetActorTransform());
				Target->SetActorLocation(EvalTransform.Location, false);
				Target->SetActorRotation(EvalTransform.Rotation, false);
				Target->SetActorScale3D(EvalTransform.Scale);
			}
		}
		else if (Track.TrackType == ESequenceTrackType::CameraFOV)
		{
			AActor* Target = FindActorByNameInWorld(World, Track.TargetActorName);
			if (Target && !Track.FOVKeys.IsEmpty())
			{
				if (UCameraComponent* CamComp = Target->GetComponentByClass<UCameraComponent>())
				{
					const float EvalFOV = EvaluateFOVTrack(Track, CurrentFrame, CamComp->GetFieldOfView());
					CamComp->SetFieldOfView(EvalFOV);
				}
			}
		}
		else if (Track.TrackType == ESequenceTrackType::Audio && bTriggerAudio)
		{
			UGameInstance* GameInstance = World->GetGameInstance();
			FSoundManager* SoundMgr = GameInstance ? GameInstance->GetSoundManager() : nullptr;
			if (SoundMgr && SoundMgr->IsAvailable())
			{
				for (const FAudioClip& Clip : Track.AudioClips)
				{
					if (Clip.SoundPath.empty())
					{
						continue;
					}
					const float TriggerFrame = static_cast<float>(Clip.StartFrame);
					const bool bCrossed = (PrevFrame < TriggerFrame && CurrentFrame >= TriggerFrame) ||
						(PrevFrame < 0.0f && Clip.StartFrame == 0 && CurrentFrame >= 0.0f);
					if (bCrossed)
					{
						if (Clip.bIsBGM)
						{
							SoundMgr->SetBGMVolume(Clip.Volume);
							SoundMgr->PlayBGM(Clip.SoundPath, Clip.bLoopBGM);
						}
						else
						{
							SoundMgr->PlaySFX(Clip.SoundPath, Clip.Volume);
						}
					}
				}
			}
		}
	}

	// 카메라 컷 트랙 평가 및 블렌딩 처리
	if (OutCameraPOV && OutHasCameraCut)
	{
		for (const FSequenceTrack& Track : Data.Tracks)
		{
			if (Track.bMuted || Track.TrackType != ESequenceTrackType::CameraCut)
			{
				continue;
			}

			for (int32 CutIdx = 0; CutIdx < Track.CameraCuts.Num(); ++CutIdx)
			{
				const FCameraCutClip& Clip = Track.CameraCuts[CutIdx];
				if (CurrentFrame >= static_cast<float>(Clip.StartFrame) && CurrentFrame <= static_cast<float>(Clip.EndFrame))
				{
					AActor* CamActor = FindActorByNameInWorld(World, Clip.CameraActorName);
					if (CamActor)
					{
						FMinimalViewInfo ActivePOV;
						CamActor->CalcCamera(0.0f, ActivePOV);

						const float BlendFrames = Clip.BlendTime * static_cast<float>(std::max(Data.FrameRate, 1));
						if (BlendFrames > 0.001f && CutIdx > 0 && (CurrentFrame - static_cast<float>(Clip.StartFrame)) < BlendFrames)
						{
							const FCameraCutClip& PrevClip = Track.CameraCuts[CutIdx - 1];
							AActor* PrevCamActor = FindActorByNameInWorld(World, PrevClip.CameraActorName);
							if (PrevCamActor && PrevCamActor != CamActor)
							{
								FMinimalViewInfo PrevPOV;
								PrevCamActor->CalcCamera(0.0f, PrevPOV);
								const float RawBlend = (CurrentFrame - static_cast<float>(Clip.StartFrame)) / BlendFrames;
								const float Alpha = ComputeInterpAlpha(RawBlend, ESequenceInterpMode::Cubic);

								ActivePOV.Location = PrevPOV.Location + (ActivePOV.Location - PrevPOV.Location) * Alpha;
								ActivePOV.Rotation = LerpRotatorShortest(PrevPOV.Rotation, ActivePOV.Rotation, Alpha);
								ActivePOV.FieldOfView = PrevPOV.FieldOfView + (ActivePOV.FieldOfView - PrevPOV.FieldOfView) * Alpha;
							}
						}

						*OutCameraPOV = ActivePOV;
						*OutHasCameraCut = true;
						return;
					}
				}
			}
		}
	}
}

bool FLevelSequenceAsset::SaveToFile(const FLevelSequenceData& Data, const FString& Path)
{
	if (Path.empty())
	{
		return false;
	}
	try
	{
		const FString Content = SerializeToString(Data);
		if (Content.empty())
		{
			return false;
		}
		const std::filesystem::path FilePath(Path);
		if (!FilePath.parent_path().empty())
		{
			std::error_code Error;
			std::filesystem::create_directories(FilePath.parent_path(), Error);
			if (Error)
			{
				return false;
			}
		}
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		if (!File)
		{
			return false;
		}
		File << Content;
		return File.good();
	}
	catch (const json::exception&)
	{
		return false;
	}
}

bool FLevelSequenceAsset::LoadFromFile(const FString& Path, FLevelSequenceData& OutData)
{
	if (Path.empty())
	{
		return false;
	}
	std::error_code Error;
	if (!std::filesystem::exists(Path, Error) || Error)
	{
		return false;
	}
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		return false;
	}
	const FString Content((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	return DeserializeFromString(Content, OutData);
}

FString FLevelSequenceAsset::SerializeToString(const FLevelSequenceData& Data)
{
	try
	{
		json Root = json::object();
		Root["Version"] = SequenceAssetVersion;
		Root["FrameRate"] = Data.FrameRate;
		Root["StartFrame"] = Data.StartFrame;
		Root["EndFrame"] = Data.EndFrame;
		Root["bLoop"] = Data.bLoop;

		json TracksJson = json::array();
		for (const FSequenceTrack& Track : Data.Tracks)
		{
			json TrackObj = json::object();
			TrackObj["TrackName"] = Track.TrackName;
			TrackObj["TrackType"] = static_cast<int32>(Track.TrackType);
			TrackObj["TargetActorName"] = Track.TargetActorName;
			TrackObj["bExpanded"] = Track.bExpanded;
			TrackObj["bMuted"] = Track.bMuted;

			json CutsArray = json::array();
			for (const FCameraCutClip& Cut : Track.CameraCuts)
			{
				CutsArray.push_back({
					{"StartFrame", Cut.StartFrame},
					{"EndFrame", Cut.EndFrame},
					{"CameraActorName", Cut.CameraActorName},
					{"BlendTime", Cut.BlendTime}
				});
			}
			TrackObj["CameraCuts"] = std::move(CutsArray);

			json TransArray = json::array();
			for (const FTransformKeyframe& Key : Track.TransformKeys)
			{
				TransArray.push_back({
					{"Frame", Key.Frame},
					{"Location", {Key.Location.X, Key.Location.Y, Key.Location.Z}},
					{"Rotation", {Key.Rotation.Pitch, Key.Rotation.Yaw, Key.Rotation.Roll}},
					{"Scale", {Key.Scale.X, Key.Scale.Y, Key.Scale.Z}},
					{"InterpMode", static_cast<int32>(Key.InterpMode)}
				});
			}
			TrackObj["TransformKeys"] = std::move(TransArray);

			json FOVArray = json::array();
			for (const FFOVKeyframe& Key : Track.FOVKeys)
			{
				FOVArray.push_back({
					{"Frame", Key.Frame},
					{"FieldOfView", Key.FieldOfView},
					{"InterpMode", static_cast<int32>(Key.InterpMode)}
				});
			}
			TrackObj["FOVKeys"] = std::move(FOVArray);

			json AudioArray = json::array();
			for (const FAudioClip& Clip : Track.AudioClips)
			{
				AudioArray.push_back({
					{"StartFrame", Clip.StartFrame},
					{"EndFrame", Clip.EndFrame},
					{"SoundPath", Clip.SoundPath},
					{"bIsBGM", Clip.bIsBGM},
					{"bLoopBGM", Clip.bLoopBGM},
					{"Volume", Clip.Volume}
				});
			}
			TrackObj["AudioClips"] = std::move(AudioArray);

			TracksJson.push_back(std::move(TrackObj));
		}

		Root["Tracks"] = std::move(TracksJson);
		return Root.dump(4);
	}
	catch (const json::exception&)
	{
		return {};
	}
}

bool FLevelSequenceAsset::DeserializeFromString(const FString& Content, FLevelSequenceData& OutData)
{
	if (Content.empty())
	{
		return false;
	}
	json Document = json::parse(Content, nullptr, false);
	if (Document.is_discarded() || !Document.is_object())
	{
		return false;
	}

	try
	{
		FLevelSequenceData Parsed;
		Parsed.FrameRate = Document.value("FrameRate", 30);
		Parsed.StartFrame = Document.value("StartFrame", 0);
		Parsed.EndFrame = Document.value("EndFrame", 150);
		Parsed.bLoop = Document.value("bLoop", false);

		if (Document.contains("Tracks") && Document["Tracks"].is_array())
		{
			for (const json& TrackObj : Document["Tracks"])
			{
				if (!TrackObj.is_object())
				{
					continue;
				}
				FSequenceTrack Track;
				Track.TrackName = TrackObj.value("TrackName", FString("Track"));
				Track.TrackType = static_cast<ESequenceTrackType>(TrackObj.value("TrackType", 1));
				Track.TargetActorName = TrackObj.value("TargetActorName", FString(""));
				Track.bExpanded = TrackObj.value("bExpanded", true);
				Track.bMuted = TrackObj.value("bMuted", false);

				if (TrackObj.contains("CameraCuts") && TrackObj["CameraCuts"].is_array())
				{
					for (const json& CutObj : TrackObj["CameraCuts"])
					{
						FCameraCutClip Cut;
						Cut.StartFrame = CutObj.value("StartFrame", 0);
						Cut.EndFrame = CutObj.value("EndFrame", 60);
						Cut.CameraActorName = CutObj.value("CameraActorName", FString(""));
						Cut.BlendTime = CutObj.value("BlendTime", 0.0f);
						Track.CameraCuts.Add(Cut);
					}
				}

				if (TrackObj.contains("TransformKeys") && TrackObj["TransformKeys"].is_array())
				{
					for (const json& KeyObj : TrackObj["TransformKeys"])
					{
						FTransformKeyframe Key;
						Key.Frame = KeyObj.value("Frame", 0);
						if (KeyObj.contains("Location") && KeyObj["Location"].is_array() && KeyObj["Location"].size() == 3)
						{
							Key.Location = FVector(
								KeyObj["Location"][0].get<float>(),
								KeyObj["Location"][1].get<float>(),
								KeyObj["Location"][2].get<float>());
						}
						if (KeyObj.contains("Rotation") && KeyObj["Rotation"].is_array() && KeyObj["Rotation"].size() == 3)
						{
							Key.Rotation = FRotator(
								KeyObj["Rotation"][0].get<float>(),
								KeyObj["Rotation"][1].get<float>(),
								KeyObj["Rotation"][2].get<float>());
						}
						if (KeyObj.contains("Scale") && KeyObj["Scale"].is_array() && KeyObj["Scale"].size() == 3)
						{
							Key.Scale = FVector(
								KeyObj["Scale"][0].get<float>(),
								KeyObj["Scale"][1].get<float>(),
								KeyObj["Scale"][2].get<float>());
						}
						Key.InterpMode = static_cast<ESequenceInterpMode>(KeyObj.value("InterpMode", 0));
						Track.TransformKeys.Add(Key);
					}
				}

				if (TrackObj.contains("FOVKeys") && TrackObj["FOVKeys"].is_array())
				{
					for (const json& KeyObj : TrackObj["FOVKeys"])
					{
						FFOVKeyframe Key;
						Key.Frame = KeyObj.value("Frame", 0);
						Key.FieldOfView = KeyObj.value("FieldOfView", 60.0f);
						Key.InterpMode = static_cast<ESequenceInterpMode>(KeyObj.value("InterpMode", 0));
						Track.FOVKeys.Add(Key);
					}
				}

				if (TrackObj.contains("AudioClips") && TrackObj["AudioClips"].is_array())
				{
					for (const json& ClipObj : TrackObj["AudioClips"])
					{
						FAudioClip Clip;
						Clip.StartFrame = ClipObj.value("StartFrame", 0);
						Clip.EndFrame = ClipObj.value("EndFrame", 30);
						Clip.SoundPath = ClipObj.value("SoundPath", FString(""));
						Clip.bIsBGM = ClipObj.value("bIsBGM", false);
						Clip.bLoopBGM = ClipObj.value("bLoopBGM", false);
						Clip.Volume = ClipObj.value("Volume", 1.0f);
						Track.AudioClips.Add(Clip);
					}
				}

				SortTrackKeys(Track);
				Parsed.Tracks.Add(std::move(Track));
			}
		}

		OutData = std::move(Parsed);
		return true;
	}
	catch (const json::exception&)
	{
		return false;
	}
}
