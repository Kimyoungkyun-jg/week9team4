#include "EnginePCH.h"
#include "Sequencer/LevelSequence.h"
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace
{
json VectorJson(const FVector& V) { return json::array({V.X, V.Y, V.Z}); }
FVector ReadVector(const json& J)
{
	if (!J.is_array() || J.size() != 3) throw std::runtime_error("Invalid vector");
	return {J.at(0).get<float>(), J.at(1).get<float>(), J.at(2).get<float>()};
}
json TransformJson(const FTransform& T)
{
	return {{"location", VectorJson(T.Location)}, {"rotation", json::array({T.Rotation.Pitch, T.Rotation.Yaw, T.Rotation.Roll})},
		{"scale", VectorJson(T.Scale)}};
}
FTransform ReadTransform(const json& J)
{
	const FVector R = ReadVector(J.at("rotation"));
	return {FRotator(R.X, R.Y, R.Z), ReadVector(J.at("location")), ReadVector(J.at("scale"))};
}
const char* BindingName(ESequenceBindingType T)
{
	switch (T) { case ESequenceBindingType::Spawnable: return "Spawnable";
	case ESequenceBindingType::Possessable: return "Possessable";
	case ESequenceBindingType::Replaceable: return "Replaceable"; }
	return "";
}
ESequenceBindingType ReadBindingType(const FString& S)
{
	if (S == "Spawnable") return ESequenceBindingType::Spawnable;
	if (S == "Possessable") return ESequenceBindingType::Possessable;
	if (S == "Replaceable") return ESequenceBindingType::Replaceable;
	throw std::runtime_error("Unknown binding type");
}
const char* TrackName(ESequenceTrackType T) { return T == ESequenceTrackType::Transform ? "Transform" : "FieldOfView"; }
ESequenceTrackType ReadTrackType(const FString& S)
{
	if (S == "Transform") return ESequenceTrackType::Transform;
	if (S == "FieldOfView") return ESequenceTrackType::FieldOfView;
	throw std::runtime_error("Unknown track type");
}
const char* InterpolationName(ESequenceInterpolation T) { return T == ESequenceInterpolation::Constant ? "Constant" : "Linear"; }
ESequenceInterpolation ReadInterpolation(const FString& S)
{
	if (S == "Constant") return ESequenceInterpolation::Constant;
	if (S == "Linear") return ESequenceInterpolation::Linear;
	throw std::runtime_error("Unknown interpolation");
}
}

bool ULevelSequence::IsValidSequence() const
{
	if (FramesPerSecond <= 0 || EndFrame < StartFrame) return false;
	std::unordered_set<FString> Ids;
	for (const FSequenceBinding& Binding : Bindings)
	{
		if (Binding.Id.empty() || !Ids.insert(Binding.Id).second) return false;
		if (Binding.Type != ESequenceBindingType::Possessable && Binding.ActorClass.empty()) return false;
	}
	for (const FSequenceTrack& Track : Tracks)
	{
		if (!Ids.contains(Track.BindingId)) return false;
		for (const FSequenceSection& Section : Track.Sections)
		{
			if (Section.EndFrame < Section.StartFrame || Section.StartFrame < StartFrame || Section.EndFrame > EndFrame) return false;
			int32 Previous = Section.StartFrame - 1;
			if (Track.Type == ESequenceTrackType::Transform && !Section.FloatChannel.Keys.empty()) return false;		// 각 키는 알맞은 트랙에 존재해야됨
			if (Track.Type == ESequenceTrackType::FieldOfView && !Section.TransformChannel.Keys.empty()) return false;
			if (Track.Type == ESequenceTrackType::Transform)
				for (const auto& Key : Section.TransformChannel.Keys)
				{ if (Key.Frame <= Previous || Key.Frame > Section.EndFrame) return false; Previous = Key.Frame; }
			else
				for (const auto& Key : Section.FloatChannel.Keys)
				{ if (Key.Frame <= Previous || Key.Frame > Section.EndFrame) return false; Previous = Key.Frame; }
		}
	}
	return true;
}

bool ULevelSequence::SaveToFile(const FString& Path) const
{
	const FString Data = SerializeToString();
	if (Data.empty()) return false;
	std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
	if (!Out) return false;
	Out << Data;
	return Out.good();
}

FString ULevelSequence::SerializeToString() const
{
	if (!IsValidSequence()) return {};
	try
	{
		json Root = {{"version", 2}, {"framesPerSecond", FramesPerSecond}, {"startFrame", StartFrame},
			{"endFrame", EndFrame}, {"bindings", json::array()}, {"tracks", json::array()}};
		for (const auto& Binding : Bindings)
			Root["bindings"].push_back({{"id", Binding.Id}, {"type", BindingName(Binding.Type)},
				{"actorClass", Binding.ActorClass}, {"actorId", Binding.ActorId},
				{"sourceActorName", Binding.SourceActorName},
				{"actorTemplate", Binding.ActorTemplateJson.empty() ? json(nullptr) : json::parse(Binding.ActorTemplateJson)},
				{"spawnTransform", TransformJson(Binding.SpawnTransform)}});
		for (const auto& Track : Tracks)
		{
			json TrackJson = {{"bindingId", Track.BindingId}, {"type", TrackName(Track.Type)}, {"sections", json::array()}};
			for (const auto& Section : Track.Sections)
			{
				json SectionJson = {{"startFrame", Section.StartFrame}, {"endFrame", Section.EndFrame}, {"keys", json::array()}};
				if (Track.Type == ESequenceTrackType::Transform)
					for (const auto& Key : Section.TransformChannel.Keys)
						SectionJson["keys"].push_back({{"frame", Key.Frame}, {"value", TransformJson(Key.Value)},
							{"interpolation", InterpolationName(Key.Interpolation)}});
				else
					for (const auto& Key : Section.FloatChannel.Keys)
						SectionJson["keys"].push_back({{"frame", Key.Frame}, {"value", Key.Value},
							{"interpolation", InterpolationName(Key.Interpolation)}});
				TrackJson["sections"].push_back(std::move(SectionJson));
			}
			Root["tracks"].push_back(std::move(TrackJson));
		}
		return Root.dump(2);
	}
	catch (const std::exception&) { return {}; }
}

bool ULevelSequence::LoadFromFile(const FString& Path)
{
	std::ifstream In(Path, std::ios::binary);
	if (!In) return false;
	return DeserializeFromString(FString(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>()));
}

bool ULevelSequence::DeserializeFromString(const FString& Content)
{
	try
	{
		const json Root = json::parse(Content);
		const int Version = Root.at("version").get<int>();
		if (Version != 1 && Version != 2) return false;
		ULevelSequence Parsed;
		Parsed.FramesPerSecond = Root.at("framesPerSecond").get<int32>();
		Parsed.StartFrame = Root.at("startFrame").get<int32>();
		Parsed.EndFrame = Root.at("endFrame").get<int32>();
		for (const auto& J : Root.at("bindings"))
		{
			FSequenceBinding Binding;
			Binding.Id = J.at("id").get<FString>();
			Binding.Type = ReadBindingType(J.at("type").get<FString>());
			Binding.ActorClass = J.value("actorClass", FString{});
			Binding.ActorId = J.value("actorId", FString{});
			Binding.SourceActorName = J.value("sourceActorName", FString{});
			if (J.contains("actorTemplate") && !J.at("actorTemplate").is_null())
			{
				if (!J.at("actorTemplate").is_object()) return false;
				Binding.ActorTemplateJson = J.at("actorTemplate").dump();
			}
			Binding.SpawnTransform = ReadTransform(J.at("spawnTransform"));
			Parsed.Bindings.push_back(std::move(Binding));
		}
		for (const auto& J : Root.at("tracks"))
		{
			FSequenceTrack Track;
			Track.BindingId = J.at("bindingId").get<FString>();
			Track.Type = ReadTrackType(J.at("type").get<FString>());
			for (const auto& S : J.at("sections"))
			{
				FSequenceSection Section;
				Section.StartFrame = S.at("startFrame").get<int32>();
				Section.EndFrame = S.at("endFrame").get<int32>();
				for (const auto& K : S.at("keys"))
				{
					const int32 Frame = K.at("frame").get<int32>();
					const auto Interpolation = ReadInterpolation(K.at("interpolation").get<FString>());
					if (Track.Type == ESequenceTrackType::Transform)
						Section.TransformChannel.Keys.push_back({Frame, ReadTransform(K.at("value")), Interpolation});
					else Section.FloatChannel.Keys.push_back({Frame, K.at("value").get<float>(), Interpolation});
				}
				Track.Sections.push_back(std::move(Section));
			}
			Parsed.Tracks.push_back(std::move(Track));
		}
		if (!Parsed.IsValidSequence()) return false;
		FramesPerSecond = Parsed.FramesPerSecond;
		StartFrame = Parsed.StartFrame;
		EndFrame = Parsed.EndFrame;
		Bindings = std::move(Parsed.Bindings);
		Tracks = std::move(Parsed.Tracks);
		return true;
	}
	catch (const std::exception&) { return false; }
}
