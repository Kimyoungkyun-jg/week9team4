#pragma once

#include "Render/PipelineState.h"
#include "Core/Types.h"

class FTexture2D;

class FGammaCorrectionRenderer
{
public:
	void Init();
	bool PrepareOutput(FTexture2D* ColorTarget);
	bool OnRender(FTexture2D* ColorTarget);
	TUniquePtr<FTexture2D>& GetOutputTarget() { return OutputTarget; }

private:
	FPipelineState PipelineState;
	TUniquePtr<FTexture2D> OutputTarget;
};
