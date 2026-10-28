#include "EnginePCH.h"
#include "Render/SceneViewPipeline.h"

#include "Render/Effects/FireBallRenderer.h"
#include "Render/Effects/FXAARenderer.h"
#include "Render/Effects/GammaCorrectionRenderer.h"
#include "Render/Effects/VignettingRenderer.h"
#include "Render/Effects/HeightFogRenderer.h"
#include "Render/SkyboxRenderer.h"
#include "Text/TextRenderer.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/FireBallComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Render/RenderCommand.h"
#include "Render/SceneViewTargets.h"
#include "UObject/UObjectIterator.h"

FSceneViewPipeline::FSceneViewPipeline() = default;
FSceneViewPipeline::~FSceneViewPipeline() = default;

void FSceneViewPipeline::Init(FRenderer* Renderer)
{
	FireBall = MakeUnique<FFireBallRenderer>();
	HeightFog = MakeUnique<FHeightFogRenderer>();
	FXAA = MakeUnique<FFXAARenderer>();
	GammaCorrection = MakeUnique<FGammaCorrectionRenderer>();
	Vignetting = MakeUnique<FVignettingRenderer>();
	Text = MakeUnique<FTextRenderer>();
	Skybox = MakeUnique<FSkyboxRenderer>();
	FireBall->Init(Renderer);
	HeightFog->Init(Renderer);
	FXAA->Init(Renderer);
	GammaCorrection->Init();
	Vignetting->Init();
	Text->Init();
	Skybox->Init("Assets/SkySphere/Sky.hdr");
}

bool FSceneViewPipeline::Prepare(FRenderGraphBuilder& Graph, FPreparedView& Prepared,
	FSceneRenderer& SceneRenderer, FRenderer* Renderer, FSceneViewTargets& Targets,
	UWorld* World, const FSceneView& View, const FSceneRenderOptions& Options)
{
	FTexture2D* Color = Targets.SceneColor.get();
	FTexture2D* Depth = Targets.Depth.get();
	if (!World || !Renderer || !Skybox || !FireBall || !HeightFog || !FXAA || !GammaCorrection || !Vignetting || !Text ||
		!Color || !Depth || !Color->GetWidth() || !Color->GetHeight() ||
		!Targets.IsValidFor(Color->GetWidth(), Color->GetHeight()))
		return false;

	FScenePassCallbacks Callbacks;
	Callbacks.FireBall = [this, World, &View, Depth, Color] { RenderFireBall(World, View, Depth, Color); };
	Callbacks.HeightFog = [this, World, &View, Depth, Color] { return RenderHeightFog(World, View, Depth, Color); };
	Callbacks.TranslucentFogConstants = [this](bool bHasFog) { return HeightFog->GetTranslucentFogConstants(bHasFog); };
	Callbacks.SceneText = [this, World, &View] { RenderSceneText(World, View); };

	Prepared.bUseVignetting = Options.Vignetting.bEnabled && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && Vignetting->PrepareOutput(Color);
	if (Options.Vignetting.bEnabled && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && !Prepared.bUseVignetting)
		return false;
	FTexture2D* PostVignettingColor = Prepared.bUseVignetting ? Vignetting->GetOutputTarget().get() : Color;
	if (Prepared.bUseVignetting)
	{
		Callbacks.VignettingOutput = PostVignettingColor;
		Callbacks.Vignetting = [this, Color, Settings = Options.Vignetting]
			{ return Vignetting->OnRender(Color, Settings); };
	}
	const bool bNeedsCinematicOverlay = View.FadeOpacity > 0.0f || View.LetterboxSize > 0.0f;
	Prepared.bUseGammaCorrection = (Options.bEnableGammaCorrection || bNeedsCinematicOverlay) && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && GammaCorrection->PrepareOutput(PostVignettingColor);
	if ((Options.bEnableGammaCorrection || bNeedsCinematicOverlay) && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && !Prepared.bUseGammaCorrection)
		return false;
	FTexture2D* PostGammaColor = Prepared.bUseGammaCorrection ? GammaCorrection->GetOutputTarget().get() : PostVignettingColor;
	if (Prepared.bUseGammaCorrection)
	{
		Callbacks.GammaCorrectionOutput = PostGammaColor;
		Callbacks.GammaCorrection = [this, PostVignettingColor, bApplyGamma = Options.bEnableGammaCorrection,
			FadeOpacity = View.FadeOpacity, LetterboxSize = View.LetterboxSize]
			{ return GammaCorrection->OnRender(PostVignettingColor, bApplyGamma, FadeOpacity, LetterboxSize); };
	}
	Prepared.bUseFXAA = Options.bEnableFXAA && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && FXAA->PrepareOutput(PostGammaColor);
	if (Prepared.bUseFXAA)
	{
		Callbacks.FXAAOutput = FXAA->GetOutputTarget().get();
		Callbacks.FXAA = [this, PostGammaColor] { return FXAA->OnRender(PostGammaColor); };
	}

	if (!SceneRenderer.AddViewportPasses(Graph, Renderer, Targets, Options, Skybox.get(), Callbacks, Prepared.FinalColor))
		return false;
	Prepared.Depth = Graph.ImportTexture(Depth, "SceneDepth");
	return static_cast<bool>(Prepared.FinalColor) && static_cast<bool>(Prepared.Depth);
}

bool FSceneViewPipeline::Execute(FRenderGraphBuilder& Graph, const FPreparedView& Prepared,
	FSceneRenderer& SceneRenderer, FSceneViewTargets& Targets)
{
	if (!Graph.Execute() || !SceneRenderer.DidCorePassSucceed() ||
		!SceneRenderer.DidVignettingPassSucceed() ||
		!SceneRenderer.DidGammaPassSucceed() || !SceneRenderer.DidFXAAPassSucceed())
		return false;
	if (Prepared.bUseFXAA && !Targets.SwapSceneColorAndBind(FXAA->GetOutputTarget()))
		return false;
	if (!Prepared.bUseFXAA && Prepared.bUseGammaCorrection &&
		!Targets.SwapSceneColorAndBind(GammaCorrection->GetOutputTarget()))
		return false;
	if (!Prepared.bUseFXAA && !Prepared.bUseGammaCorrection && Prepared.bUseVignetting &&
		!Targets.SwapSceneColorAndBind(Vignetting->GetOutputTarget()))
		return false;
	RenderCommand::EndRenderPass();
	return true;
}

bool FSceneViewPipeline::Render(FSceneRenderer& SceneRenderer, FRenderer* Renderer,
	FSceneViewTargets& Targets, UWorld* World, const FSceneView& View,
	const FSceneRenderOptions& Options)
{
	FRenderGraphBuilder Graph;
	FPreparedView Prepared;
	return Prepare(Graph, Prepared, SceneRenderer, Renderer, Targets, World, View, Options) &&
		Execute(Graph, Prepared, SceneRenderer, Targets);
}

void FSceneViewPipeline::RenderSceneText(UWorld* World, const FSceneView& View)
{
	for (TObjectIterator<UTextRenderComponent> Item; Item; ++Item)
	{
		if (!Item || !Item->IsRegistered() || !Item->GetFont() || !Item->IsShown(View.bGameView) ||
			!Item->GetOwner() || Item->GetOwner()->GetWorld() != World)
			continue;
		Text->OnRender(Item->GetText(), Item->GetWorldMatrix(), Item->GetTextSize(),
			*Item->GetFont(), View.ViewProjectionMatrix);
	}
}

bool FSceneViewPipeline::RenderHeightFog(UWorld* World, const FSceneView& View,
	FTexture2D* Depth, FTexture2D* Color)
{
	if (!World || !Depth || !Color || !View.bIsPerspective)
		return false;
	for (TObjectIterator<UExponentialHeightFogComponent> Fog; Fog; ++Fog)
	{
		if (!Fog || !Fog->IsRegistered() || !Fog->IsShown(View.bGameView) || !Fog->GetOwner() ||
			Fog->GetOwner()->GetWorld() != World)
			continue;
		FHeightFogSetting Setting;
		Setting.FogDensity = Fog->GetFogDensity();
		Setting.FogHeightFalloff = Fog->GetFogHeightFalloff();
		Setting.FogHeight = Fog->GetFogHeight();
		Setting.FogColor = Fog->GetFogColor();
		Setting.StartDistance = Fog->GetStartDistance();
		Setting.FogMaxOpacity = Fog->GetFogMaxOpacity();
		Setting.FogCutoffDistance = Fog->GetFogCutoffDistance();
		if (HeightFog->OnRender(View, Depth, Color, Setting))
			return true;
	}
	return false;
}

bool FSceneViewPipeline::RenderFireBall(UWorld* World, const FSceneView& View,
	FTexture2D* Depth, FTexture2D* Color)
{
	if (!World || !Depth || !Color)
		return false;
	bool bRendered = false;
	for (TObjectIterator<UFireBallComponent> Item; Item; ++Item)
	{
		if (!Item || !Item->IsRegistered() || !Item->IsVisible() || !Item->IsShown(View.bGameView) ||
			!Item->GetOwner() || Item->GetOwner()->GetWorld() != World)
			continue;
		FFireBallData Data{};
		Data.Center = Item->GetWorldLocation();
		Data.Radius = Item->GetRadius();
		Data.Color = Item->GetColor();
		Data.Intensity = Item->GetIntensity();
		Data.RadiusFallOff = Item->GetRadiusFallOff();
		bRendered |= FireBall->OnRender(View, Depth, Color, Data);
	}
	return bRendered;
}
