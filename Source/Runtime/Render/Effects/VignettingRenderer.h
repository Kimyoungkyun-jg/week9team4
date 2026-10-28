#pragma once

#include "Core/Types.h"
#include "Render/PipelineState.h"
#include "Render/SceneRenderOptions.h"

class FTexture2D;
class FConstantBuffer;

class FVignettingRenderer
{
public:
	FVignettingRenderer();
	~FVignettingRenderer();
	void Init();
	bool PrepareOutput(FTexture2D* ColorTarget);
	bool OnRender(FTexture2D* ColorTarget, const FVignettingSettings& Settings);
	TUniquePtr<FTexture2D>& GetOutputTarget() { return OutputTarget; }

private:
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FTexture2D> OutputTarget;
};
