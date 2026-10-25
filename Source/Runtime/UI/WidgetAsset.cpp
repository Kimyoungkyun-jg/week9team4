#include "EnginePCH.h"
#include "UI/WidgetAsset.h"
#include "UI/Widget.h"
#include "Serialization/TypeSerializer.h"

#include <filesystem>
#include <fstream>

namespace
{
	constexpr int32 WidgetAssetVersion = 1;
	constexpr int32 MaxWidgetDepth = 64;
	constexpr int32 MaxWidgetCount = 10000;

	json WriteNode(UWidget& Widget)
	{
		json Properties = json::object();
		Widget.Serialize(Properties, false);
		json Node = {
			{"Class", Widget.GetClass()->Name},
			{"Properties", std::move(Properties)},
			{"Children", json::array()}
		};
		if (auto* Canvas = Cast<UCanvasWidget>(&Widget))
			for (UWidget* Child : Canvas->GetChildren())
				Node["Children"].push_back(WriteNode(*Child));
		return Node;
	}

	TUniquePtr<UWidget> ReadNode(const json& Node, int32 Depth, int32& Count)
	{
		if (!Node.is_object() || Depth > MaxWidgetDepth || ++Count > MaxWidgetCount ||
			!Node.contains("Properties") || !Node["Properties"].is_object() ||
			!Node.contains("Children") || !Node["Children"].is_array())
			return nullptr;

		const FString ClassName = Node.at("Class").get<FString>();
		TUniquePtr<UWidget> Widget;
		if (ClassName == "UCanvasWidget") Widget.reset(NewObject<UCanvasWidget>());
		else if (ClassName == "UImageWidget") Widget.reset(NewObject<UImageWidget>());
		else if (ClassName == "UTextWidget") Widget.reset(NewObject<UTextWidget>());
		else if (ClassName == "UButtonWidget") Widget.reset(NewObject<UButtonWidget>());
		else return nullptr;
		if (!Widget) return nullptr;

		json Properties = Node["Properties"];
		Widget->Serialize(Properties, true);
		if (auto* Canvas = Cast<UCanvasWidget>(Widget.get()))
		{
			for (const json& ChildNode : Node["Children"])
			{
				TUniquePtr<UWidget> Child = ReadNode(ChildNode, Depth + 1, Count);
				if (!Child || !Canvas->AddChild(Child.get())) return nullptr;
				Child.release();
			}
		}
		else if (!Node["Children"].empty())
			return nullptr;
		return Widget;
	}
}

bool FWidgetAsset::Save(UCanvasWidget& Root, const FString& Path)
{
	if (Path.empty()) return false;
	try
	{
		const FString Content = Serialize(Root);
		if (Content.empty()) return false;
		const std::filesystem::path FilePath(Path);
		if (!FilePath.parent_path().empty())
		{
			std::error_code Error;
			std::filesystem::create_directories(FilePath.parent_path(), Error);
			if (Error) return false;
		}
		std::ofstream File(Path, std::ios::binary | std::ios::trunc);
		if (!File) return false;
		File << Content;
		return File.good();
	}
	catch (const json::exception&)
	{
		return false;
	}
}

TUniquePtr<UCanvasWidget> FWidgetAsset::Load(const FString& Path)
{
	if (Path.empty()) return nullptr;
	std::error_code Error;
	if (std::filesystem::file_size(Path, Error) > 16 * 1024 * 1024 || Error)
		return nullptr;
	std::ifstream File(Path, std::ios::binary);
	if (!File) return nullptr;
	return Deserialize(FString(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()));
}

FString FWidgetAsset::Serialize(UCanvasWidget& Root)
{
	try { return json{{"Version", WidgetAssetVersion}, {"Root", WriteNode(Root)}}.dump(4); }
	catch (const json::exception&) { return {}; }
}

TUniquePtr<UCanvasWidget> FWidgetAsset::Deserialize(const FString& Content)
{
	if (Content.size() > 16 * 1024 * 1024) return nullptr;
	json Document = json::parse(Content, nullptr, false);
	if (Document.is_discarded()) return nullptr;
	try
	{
		if (!Document.is_object() || Document.at("Version").get<int32>() != WidgetAssetVersion)
			return nullptr;
		int32 Count = 0;
		TUniquePtr<UWidget> Root = ReadNode(Document.at("Root"), 0, Count);
		if (!Root || !Root->IsA<UCanvasWidget>()) return nullptr;
		return TUniquePtr<UCanvasWidget>(static_cast<UCanvasWidget*>(Root.release()));
	}
	catch (const json::exception&)
	{
		return nullptr;
	}
}

TUniquePtr<UWidget> FWidgetAsset::Clone(UWidget& Widget)
{
	try
	{
		int32 Count = 0;
		return ReadNode(WriteNode(Widget), 0, Count);
	}
	catch (const json::exception&) { return nullptr; }
}
