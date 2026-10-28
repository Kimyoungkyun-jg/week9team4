#pragma once

#include "Render/PipelineState.h"
#include "Core/Types.h"

class FTexture2D;
class FConstantBuffer;

class FGammaCorrectionRenderer
{
public:
	~FGammaCorrectionRenderer();
	void Init();
	bool PrepareOutput(FTexture2D* ColorTarget);
	bool OnRender(FTexture2D* ColorTarget, bool bApplyGamma, float FadeOpacity, float LetterboxSize);
	TUniquePtr<FTexture2D>& GetOutputTarget() { return OutputTarget; }

private:
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FTexture2D> OutputTarget;
};
