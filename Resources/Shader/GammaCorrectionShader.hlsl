#include "ColorSpace.hlsli"

Texture2D<float4> LinearSceneColor : register(t0);

struct FPSInput
{
    float4 Position : SV_POSITION;
};

FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    float4 Color = LinearSceneColor.Load(int3(int2(Input.Position.xy), 0));
    return float4(LinearToSRGB(Color.rgb), Color.a);
}
