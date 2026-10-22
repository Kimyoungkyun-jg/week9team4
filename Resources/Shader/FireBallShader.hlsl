#pragma pack_matrix(row_major)

cbuffer PerObjectConstants : register(b0)
{
    matrix InverseViewProjection;
    float3 Center;
    float Radius;
    float4 Color;
    float3 CameraPosition;
    float RadiusFallOff;
    float Intensity;
};

Texture2D<float> SceneDepthTexture : register(t0);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    // Scene Depth와 동일하게 Full-Screen Quad로 그린다.
    static const float2 Positions[6] =
    {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1)
    };
    VSOutput Output;
    Output.Position = float4(Positions[VertexID], 0, 1);
    return Output;
}

float4 mainPS(VSOutput Input) : SV_Target
{
    if (Radius <= 0.0f || Intensity <= 0.0f)
        return float4(0, 0, 0, 0);

    float Depth = SceneDepthTexture.Load(int3(int2(Input.Position.xy), 0));

    // 하늘이거나 유효하지 않으면 무시
    if (!isfinite(Depth) || Depth >= 1.0f || Depth <= 0.0f)
        return float4(0, 0, 0, 0);

    uint Width, Height;
    SceneDepthTexture.GetDimensions(Width, Height);
    float2 UV = Input.Position.xy / float2(Width, Height);
    float2 NDC = float2(UV.x * 2.0 - 1.0, 1.0f - UV.y * 2.0);

    float4 WorldH = mul(float4(NDC, Depth, 1.0), InverseViewProjection);
    if (!isfinite(WorldH.w) || abs(WorldH.w) < 1.0e-6f)
        return float4(0, 0, 0, 0);
    float3 WorldPosition = WorldH.xyz / WorldH.w;

    float Distance = length(WorldPosition - Center);
    if (Distance >= Radius)
        return float4(0, 0, 0, 0); // 반경 밖이면 무시

    float x = Distance / Radius;
    float Attenuation = pow(saturate(1.0 - x), max(RadiusFallOff, 0.0f));
    return Color * (Attenuation * Intensity);
}
