cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;

    float2 gTexScale;
    float2 gTexOffset;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gSamLinear : register(s0);

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct PixelOut
{
    float4 Position : SV_TARGET0;
    float4 Normal   : SV_TARGET1;
    float4 Albedo   : SV_TARGET2;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    vout.TexC = vin.TexC * gTexScale + gTexOffset;

    return vout;
}

PixelOut PS(VertexOut pin)
{
    PixelOut pout;

    float3 N = normalize(pin.NormalW);
    float4 albedo = gDiffuseMap.Sample(gSamLinear, pin.TexC);

    // Отбрасываем почти прозрачные пиксели (листва / ткань)
    clip(albedo.a - 0.1f);

    pout.Position = float4(pin.PosW, 1.0f);
    pout.Normal = float4(N, 0.0f);
    pout.Albedo = float4(albedo.rgb, 1.0f);

    return pout;
}
