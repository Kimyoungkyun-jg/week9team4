#include "EnginePCH.h"
#include "Launch/EntryPoint.h"
#include "GameEngine.h"

UClass* GetEngineClass()
{
	return UGameEngine::StaticClass();
}
