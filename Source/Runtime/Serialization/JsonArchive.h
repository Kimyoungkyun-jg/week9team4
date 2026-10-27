#pragma once

class UWorld;
class AActor;

class FJsonArchive
{
public:
	static bool SaveWorld(UWorld* World, const FString& Path);
	static bool LoadWorld(UWorld* World, const FString& Path);
	static bool LoadWorldFromBytes(UWorld* World, const TArray<uint8>& Bytes);
	static bool SaveWorldToBytes(UWorld* World, TArray<uint8>& OutBytes);
	static bool SaveActorToString(AActor* Actor, FString& OutJson);
	static bool LoadActorFromString(AActor* Actor, const FString& ActorJson);
};
