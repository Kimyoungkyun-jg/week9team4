#pragma pack_matrix(row_major)
cbuffer MapOverlayConstants : register(b0) { matrix ViewProjection; };
struct VS_INPUT { float3 Position : POSITION; float2 UV : TEXCOORD; float4 Color : COLOR; };
struct PS_INPUT { float4 Position : SV_POSITION; float4 Color : COLOR; };
PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    Output.Position = mul(float4(Input.Position, 1), ViewProjection);
    Output.Color = Input.Color;
    return Output;
}
float4 mainPS(PS_INPUT Input) : SV_TARGET { return Input.Color; }
