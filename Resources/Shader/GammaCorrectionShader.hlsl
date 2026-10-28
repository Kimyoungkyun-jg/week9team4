#include "ColorSpace.hlsli"

cbuffer CinematicOverlayConstants : register(b0)
{
    float FadeOpacity;
    float LetterboxSize;
    float ApplyGamma;
    float Padding;
};

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
    uint Width, Height;
    LinearSceneColor.GetDimensions(Width, Height);
    float Y = Input.Position.y / max(float(Height), 1.0f);
    if (Y < LetterboxSize || Y > 1.0f - LetterboxSize)
        return float4(0.0f, 0.0f, 0.0f, Color.a);
    float3 OutputColor = ApplyGamma > 0.5f ? LinearToSRGB(Color.rgb) : Color.rgb;
    return float4(lerp(OutputColor, float3(0.0f, 0.0f, 0.0f), saturate(FadeOpacity)), Color.a);
}
