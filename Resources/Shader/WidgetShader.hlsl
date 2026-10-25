cbuffer WidgetParams : register(b0)
{
    float4 ViewportAndMode; // width, height, MSDF distance range, unused
    float4 Tint;
};

Texture2D WidgetTexture : register(t0);
SamplerState WidgetSampler : register(s0);

struct VSInput
{
    float2 Position : POSITION;
    float2 UV : TEXCOORD0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    Output.Position = float4(Input.Position.x * 2.0 / ViewportAndMode.x - 1.0,
        1.0 - Input.Position.y * 2.0 / ViewportAndMode.y, 0.0, 1.0);
    Output.UV = Input.UV;
    return Output;
}

float4 mainPS(PSInput Input) : SV_TARGET
{
    float4 Sample = WidgetTexture.Sample(WidgetSampler, Input.UV);
    if (ViewportAndMode.z <= 0.0)
        return Sample * Tint;

    float Distance = max(min(Sample.r, Sample.g), min(max(Sample.r, Sample.g), Sample.b));
    uint Width, Height;
    WidgetTexture.GetDimensions(Width, Height);
    float2 UnitRange = ViewportAndMode.z / float2(Width, Height);
    float2 ScreenTexSize = 1.0 / max(fwidth(Input.UV), float2(1e-6, 1e-6));
    float PixelRange = max(0.5 * dot(UnitRange, ScreenTexSize), 1.0);
    float Opacity = saturate(PixelRange * (Distance - 0.5) + 0.5);
    return float4(Tint.rgb, Tint.a * Opacity);
}
