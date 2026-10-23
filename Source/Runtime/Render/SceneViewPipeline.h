#pragma once

#include "Render/SceneRenderer.h"

class UWorld;
class FFireBallRenderer;
class FHeightFogRenderer;
class FFXAARenderer;
class FTextRenderer;
class FSkyboxRenderer;

// Editor와 Game이 공유하는 한 뷰의 Graph 생성·실행 및 공통 화면 효과.
class FSceneViewPipeline
{
public:
	struct FPreparedView
	{
		FRenderGraphTextureRef FinalColor;
		FRenderGraphTextureRef Depth;
		bool bUseFXAA = false;
	};

	FSceneViewPipeline();
	~FSceneViewPipeline();
	void Init(FRenderer* Renderer);
	bool Prepare(FRenderGraphBuilder& Graph, FPreparedView& Prepared, FSceneRenderer& SceneRenderer,
		FRenderer* Renderer, FSceneViewTargets& Targets, UWorld* World, const FSceneView& View,
		const FSceneRenderOptions& Options);
	bool Execute(FRenderGraphBuilder& Graph, const FPreparedView& Prepared,
		FSceneRenderer& SceneRenderer, FSceneViewTargets& Targets);
	bool Render(FSceneRenderer& SceneRenderer, FRenderer* Renderer, FSceneViewTargets& Targets,
		UWorld* World, const FSceneView& View, const FSceneRenderOptions& Options);

private:
	void RenderSceneText(UWorld* World, const FSceneView& View);
	bool RenderHeightFog(UWorld* World, const FSceneView& View, FTexture2D* Depth, FTexture2D* Color);
	bool RenderFireBall(UWorld* World, const FSceneView& View, FTexture2D* Depth, FTexture2D* Color);

	TUniquePtr<FSkyboxRenderer> Skybox;
	TUniquePtr<FFireBallRenderer> FireBall;
	TUniquePtr<FHeightFogRenderer> HeightFog;
	TUniquePtr<FFXAARenderer> FXAA;
	TUniquePtr<FTextRenderer> Text;
};
