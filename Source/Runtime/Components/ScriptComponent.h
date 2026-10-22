#pragma once

#include "ActorComponent.h"

struct FScriptRuntime;

class UScriptComponent : public UActorComponent
{
	DECLARE_CLASS(UScriptComponent, UActorComponent)
	REFLECT_START(ClassName)
		PROPERTY(ScriptPath)
	REFLECT_END()

public:
	UScriptComponent();
	~UScriptComponent() override;

	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;
	void EndPlay() override;

	void SetScriptPath(FString InPath) { ScriptPath = std::move(InPath); }
	const FString& GetScriptPath() const { return ScriptPath; }

private:
	FString ScriptPath;
	TUniquePtr<FScriptRuntime> Runtime;
};
