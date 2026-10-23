#pragma once

#include "Engine/Engine.h"
#include "Render/SceneViewPipeline.h"

class UGameEngine : public UEngine
{
	DECLARE_CLASS(UGameEngine, UEngine)
public:
	FEngineConfig GetConfig() const override;

	virtual bool Init() override;
	virtual void Tick(float DeltaTime)override;
	void Render() override;
	virtual void PreExit() override;
private:
	FWorldContext* GameContext = nullptr;
	FSceneViewPipeline ViewPipeline;
};
