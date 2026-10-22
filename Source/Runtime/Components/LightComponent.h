#pragma once

#include "LightComponentBase.h"
#include "Render/LineBatcher.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(ShadowResolutionScale)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowSlopeBias)
		PROPERTY(ShadowSharpen)
	REFLECT_END()
public:
	ULightComponent();
	virtual ~ULightComponent() override;

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }

	void SetShadowResolutionScale(float InScale) { ShadowResolutionScale = InScale; }
	void SetShadowBias(float InBias) { ShadowBias = InBias; }
	void SetShadowSlopeBias(float InSlopeBias) { ShadowSlopeBias = InSlopeBias; }
	void SetShadowSharpen(float InSharpen) { ShadowSharpen = InSharpen; }

	virtual ELightType GetLightType() const = 0;
	virtual void DrawDebug(FLineBatcher* LineBatcher) const;

	virtual class FLightSceneProxy* CreateSceneProxy() const
	{
		return NULL;
	}
	FLightSceneProxy* SceneProxy = nullptr;
protected:
	virtual bool ShouldCreateRenderState() const override
	{ return true; }

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	// 라이트 타입별 기본 해상도에 곱하는 배율. 아틀라스 배치를 위해 가까운 2의 거듭제곱으로 반올림한다.
	float ShadowResolutionScale = 1.0f;
	// 빛 방향으로 밀어내는 월드 거리. 정면으로 빛을 받는 면의 Shadow Acne를 막는다.
	float ShadowBias = 0.01f;
	// 법선 방향으로 밀어내는 월드 거리. 빛과 비스듬한 면일수록 크게 적용된다.
	float ShadowSlopeBias = 0.01f;
	// 0이면 PCF 결과 그대로, 1에 가까울수록 반그림자 폭을 좁혀 가장자리를 날카롭게 만든다.
	float ShadowSharpen = 0.0f;
private:


};