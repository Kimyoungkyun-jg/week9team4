#pragma once

#include "Core/Types.h"
#include "Render/SceneView.h"
#include "Render/RenderPacket.h"
#include "Render/MeshPassProcessor.h"
#include "Render/SceneRenderData.h"
#include "Render/SceneRenderOptions.h"
#include "Render/RenderGraph/RenderGraph.h"

class UWorld;
class FScene;
class FRenderer;
class FConstantBuffer;
class FTexture2D;
class FStructuredBuffer;
class FSkyboxRenderer;
struct FPipelineState;
struct FSceneViewTargets;

struct FScenePassCallbacks // 공통 화면 효과의 실행 작업
{
	std::function<void()> FireBall;
	std::function<bool()> HeightFog;
	std::function<FConstantBuffer*(bool)> TranslucentFogConstants;
	std::function<void()> SceneText;
	std::function<bool()> Vignetting;
	FTexture2D* VignettingOutput = nullptr;
	std::function<bool()> GammaCorrection;
	FTexture2D* GammaCorrectionOutput = nullptr;
	std::function<bool()> FXAA;
	FTexture2D* FXAAOutput = nullptr;
};


// 씬 렌더링 조율자
class FSceneRenderer
{
public:
	FSceneRenderer(UWorld* InWorld, const FSceneView& InView, FSceneRenderData& InRenderData);
	~FSceneRenderer() = default;
	FSceneRenderer(const FSceneRenderer&) = delete;
	FSceneRenderer& operator=(const FSceneRenderer&) = delete;

	// 가시성 검사 및 렌더 큐 수집
	void InitViews(FRenderer* Renderer, const FSceneRenderOptions& Options);
	static bool SetCascadeCount(uint32 Count);
	static uint32 GetCascadeCount();
	static bool SetCascadeLogRatio(float Ratio);
	static float GetCascadeLogRatio();

	// 뷰 결과를 하나 유지하는 벤치마크에서도 사용한다.
	static void GatherRenderPackets(FScene& Scene, FSceneRenderData& Data, const FViewContext& View,
		const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr);
	static void PrepareDrawData(FRenderer* Renderer, const FSceneRenderData& Data);
	const FRenderStats& GetRenderStats() const { return RenderData.RenderStats; }

	// 한 뷰의 모든 패스를 정해진 순서로 등록한다.
	bool AddViewportPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
		const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox, const FScenePassCallbacks& Callbacks,
		FRenderGraphTextureRef& OutFinalColor);
	bool AddLightCullingDebugPass(FRenderGraphBuilder& Graph, FRenderer* Renderer,
		FRenderGraphTextureRef Color, FRenderGraphTextureRef Depth);
	bool DidCorePassSucceed() const { return bCorePassSucceeded; }
	bool DidVignettingPassSucceed() const { return bVignettingPassSucceeded; }
	bool DidGammaPassSucceed() const { return bGammaPassSucceeded; }
	bool DidFXAAPassSucceed() const { return bFXAAPassSucceeded; }

	// 반투명 렌더링
	void RenderTranslucent(FRenderer* Renderer, FConstantBuffer* FogConstants = nullptr);

	// 전체 렌더 큐 조회
	FRenderQueue& GetRenderQueue() { return RenderData.RenderQueue; }
	const FRenderQueue& GetRenderQueue() const { return RenderData.RenderQueue; }

	// 뷰 정보 조회
	const FSceneView& GetView() const { return View; }
	const FSceneRenderData& GetRenderData() const { return RenderData; }
	
private:
	static void GatherPacketChunk(FSceneRenderData& Data, uint32 Begin, uint32 End, uint32 ChunkIndex,
		bool bPrepareObjectSlots, bool bStaticGroups, const uint8* OccludedMask);
	bool AddCorePasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
		const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox);
	bool AddPostProcessingPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
		const FSceneRenderOptions& Options, const FScenePassCallbacks& Callbacks,
		FRenderGraphTextureRef& OutFinalColor);
	bool AddWorldNormalPass(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
		const FSceneRenderOptions& Options);
	void RenderWorldNormal(FRenderer* Renderer, bool bDrawPrimitives);
	void RenderSkybox(FSkyboxRenderer* Skybox);
	void RenderGBuffer(FRenderer* Renderer);
	void RenderShadowMapPass(FRenderer* Renderer, FRenderGraphBuilder& Graph,
		FRenderGraphTextureRef ShadowMapRef, bool bClearWholeAtlas);
	void RenderDeferredLighting(FRenderer* Renderer, uint32 Width, uint32 Height, EPcfMode PcfMode, float ShadowDepthBias);
	void RenderDepthPrepass(FRenderer* Renderer, FPipelineState* DepthPipeline);
	void BuildForwardLightTiles(FRenderer* Renderer, FTexture2D* DepthTexture, FStructuredBuffer* SceneLights);
	void RenderForwardOpaque(FRenderer* Renderer, const FSceneRenderOptions& Options, bool bUseDepthPrepass);
	void RenderToneMap();
	void RenderLightCullingDebug(FRenderer* Renderer);
	bool BindDeferredLighting(FRenderer* Renderer, uint32 Width, uint32 Height, EPcfMode PcfMode, float ShadowDepthBias);
	bool BindForwardLighting(FRenderer* Renderer, const FSceneRenderOptions& Options);
	void UnbindForwardLighting(FRenderer* Renderer);
	bool IntersectSphereFrustum(const FVector4& PositionRadius, const FPlane (&Planes)[6]) const;

	UWorld* World = nullptr;
	FSceneView View;
	FSceneRenderData& RenderData;
	bool bLightUploadSucceeded = false;
	bool bTilesBuilt = false;
	TArray<FMeshDrawBatch> ForwardBatches;
	FViewContext CachedViewContext;
	bool bCorePassSucceeded = true;
	bool bVignettingPassSucceeded = true;
	bool bGammaPassSucceeded = true;
	bool bFXAAPassSucceeded = true;
	bool bHasFog = false;
};
