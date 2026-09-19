cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldViewProj;
    float2 gTexScale;
    float2 gTexOffset;
    float3 gEyePosW;
    float gDisplacementScale;
    float gTessNear;
    float gTessFar;
    float gTessMin;
    float gTessMax;
};

cbuffer cbMaterialFlags : register(b1)
{
    uint gHasNormalMap;
    uint gHasDisplacementMap;
};

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gSamLinear : register(s0);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct DomainOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PixelOut
{
    float4 Position : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 Albedo : SV_TARGET2;
};

DomainOut VS(VertexIn vin)
{
    DomainOut vout;
    float3 posL = vin.PosL;
    float3 normalL = normalize(vin.NormalL);
    float2 texC = vin.TexC * gTexScale + gTexOffset;

    if (gHasDisplacementMap != 0)
    {
        float height = gDisplacementMap.SampleLevel(gSamLinear, texC, 0.0f).r;
        posL += normalL * ((height - 0.5f) * gDisplacementScale);
    }

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(normalL, (float3x3)gWorld));
    vout.PosH = mul(float4(posL, 1.0f), gWorldViewProj);
    vout.TexC = texC;
    return vout;
}

PixelOut PS(DomainOut pin)
{
    PixelOut pout;
    float3 N = normalize(pin.NormalW);

    if (gHasNormalMap != 0)
    {
        float3 dp1 = ddx(pin.PosW);
        float3 dp2 = ddy(pin.PosW);
        float2 duv1 = ddx(pin.TexC);
        float2 duv2 = ddy(pin.TexC);

        float3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
        T = normalize(T - N * dot(N, T));
        float3 B = normalize(cross(N, T));

        float3 normalSample = gNormalMap.Sample(gSamLinear, pin.TexC).xyz * 2.0f - 1.0f;
        normalSample.y = -normalSample.y;
        N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * N);
    }

    float4 albedo = gDiffuseMap.Sample(gSamLinear, pin.TexC);
    clip(albedo.a - 0.1f);

    pout.Position = float4(pin.PosW, 1.0f);
    pout.Normal = float4(N, 0.0f);
    pout.Albedo = float4(albedo.rgb, 1.0f);
    return pout;
}
