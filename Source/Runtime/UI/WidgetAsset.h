#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

class UCanvasWidget;
class UWidget;

// File format: { "Version": 1, "Root": { "Class", "Properties", "Children" } }.
class FWidgetAsset
{
public:
	static bool Save(UCanvasWidget& Root, const FString& Path);
	static TUniquePtr<UCanvasWidget> Load(const FString& Path);
	static FString Serialize(UCanvasWidget& Root);
	static TUniquePtr<UCanvasWidget> Deserialize(const FString& Content);
	static TUniquePtr<UWidget> Clone(UWidget& Widget);
};
