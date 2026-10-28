#include "ColorSpace.hlsli"

cbuffer VignettingConstants : register(b0)
{
    float4 VignetteColor;
    float InnerRadius;
    float Softness;
    float Opacity;
    float AspectRatio;
};

Texture2D<float4> InputColor : register(t0);

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
    uint Width, Height;
    InputColor.GetDimensions(Width, Height);
    float2 UV = Input.Position.xy / float2(Width, Height);
    float2 Offset = (UV - 0.5f) * float2(AspectRatio, 1.0f);
    float Edge = smoothstep(InnerRadius, InnerRadius + Softness, length(Offset)) * Opacity;
    float4 Color = InputColor.Load(int3(int2(Input.Position.xy), 0));
    return float4(lerp(Color.rgb, SRGBToLinear(VignetteColor.rgb), Edge), Color.a);
}
