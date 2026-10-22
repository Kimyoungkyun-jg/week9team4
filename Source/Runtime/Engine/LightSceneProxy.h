#pragma once

#include "Components/LightComponent.h"
#include "SceneManagement.h"
#include "Render/LightRenderData.h"

// MSVC 최적화
// 가상함수 테이블과 패딩 방지
class FLightSceneProxyFixLayout
{
public:
	virtual ~FLightSceneProxyFixLayout() = default;
};

class FLightSceneProxy : public FLightSceneProxyFixLayout
{
public:
	FLightSceneProxy(const ULightComponent* InLightComponent)
		: LightComponent(InLightComponent) {}
	virtual ~FLightSceneProxy() = default;

	virtual void GetLightShaderParameters(FLightRenderParameters& OutParameters) const = 0;

	const ULightComponent* GetLightComponent() const { return LightComponent; }
	bool IsVisible() const { return LightComponent ? LightComponent->IsVisible() : false; }
	bool IsEnabled() const { return LightComponent && LightComponent->GetIntensity() > 0.0f; }
	bool IsShown(bool bGameView) const { return LightComponent ? LightComponent->IsShown(bGameView) : false; }

	// 컴포넌트 값을 매 프레임 읽는다. 에디터에서 잘못 입력한 값은 여기서 범위를 맞춘다.
	FLightShadowSettings GetShadowSettings() const
	{
		FLightShadowSettings Settings{};
		if (!LightComponent)
		{
			Settings.bCastShadows = false;
			return Settings;
		}
		Settings.Light = LightComponent;
		Settings.bCastShadows = LightComponent->CastsShadows();
		Settings.ResolutionScale = FMath::Clamp(LightComponent->GetShadowResolutionScale(), 0.0625f, 4.0f);
		Settings.Bias = std::max(LightComponent->GetShadowBias(), 0.0f);
		Settings.SlopeBias = std::max(LightComponent->GetShadowSlopeBias(), 0.0f);
		Settings.Sharpen = FMath::Clamp(LightComponent->GetShadowSharpen(), 0.0f, 1.0f);
		return Settings;
	}
protected:
	const ULightComponent* LightComponent = nullptr;

private:
	friend class FScene;
	FScene* Scene = nullptr;
	int32 PackedIndex = INDEX_NONE;
};

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
};

class FLocalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
};

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
};

class FSpotLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
};

class FRectLightSceneProxy : public FLocalLightSceneProxy
{
public:
	using FLocalLightSceneProxy::FLocalLightSceneProxy;
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;
};

class FAmbientLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FAmbientLightRenderData& GetRenderData() const { return RenderData; }

private:
	FAmbientLightRenderData RenderData{};
};

class FSkyLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FSkyLightRenderData& GetRenderData() const { return RenderData; }

private:
	FSkyLightRenderData RenderData{};
};
