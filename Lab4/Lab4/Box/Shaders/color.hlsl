cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;

    float2 gTexScale;
    float2 gTexOffset;
};

Texture2D gTexture : register(t0);

SamplerState gSampler : register(s0);

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
    vout.TexC = vin.TexC;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 tiledUV = pin.TexC * gTexScale;
    float2 localUV = frac(tiledUV);
    
    int2 tileId = (int2) floor(tiledUV);

    int parity = (tileId.x + tileId.y) & 1;

    float direction = (parity == 0) ? 1.0f : -1.0f;

    float angle = gTexOffset.x * direction;

    float c = cos(angle);
    float s = sin(angle);

    localUV -= float2(0.5f, 0.5f);

    float2 rotatedUV;

    rotatedUV.x =
        localUV.x * c - localUV.y * s;

    rotatedUV.y =
        localUV.x * s + localUV.y * c;

    rotatedUV += float2(0.5f, 0.5f);

    float2 animatedUV = rotatedUV;

    return gTexture.Sample(gSampler, animatedUV);
}