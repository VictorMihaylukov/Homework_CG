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

struct VertexOut
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct PatchTess
{
    float Edge[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
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

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosL = vin.PosL;
    vout.NormalL = vin.NormalL;
    vout.TexC = vin.TexC * gTexScale + gTexOffset;
    return vout;
}

PatchTess HSConst(InputPatch<VertexOut, 3> patch)
{
    PatchTess output;
    float3 p0 = mul(float4(patch[0].PosL, 1.0f), gWorld).xyz;
    float3 p1 = mul(float4(patch[1].PosL, 1.0f), gWorld).xyz;
    float3 p2 = mul(float4(patch[2].PosL, 1.0f), gWorld).xyz;
    float3 center = (p0 + p1 + p2) / 3.0f;

    float distanceToCamera = distance(center, gEyePosW);
    float t = saturate((distanceToCamera - gTessNear) / max(gTessFar - gTessNear, 0.001f));
    float tess = lerp(gTessMax, gTessMin, t);

    output.Edge[0] = tess;
    output.Edge[1] = tess;
    output.Edge[2] = tess;
    output.Inside = tess;
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HSConst")]
VertexOut HS(InputPatch<VertexOut, 3> patch, uint i : SV_OutputControlPointID)
{
    return patch[i];
}

[domain("tri")]
DomainOut DS(PatchTess patchConstants, const OutputPatch<VertexOut, 3> patch, float3 bary : SV_DomainLocation)
{
    DomainOut dout;

    float3 posL = patch[0].PosL * bary.x + patch[1].PosL * bary.y + patch[2].PosL * bary.z;
    float3 normalL = normalize(patch[0].NormalL * bary.x + patch[1].NormalL * bary.y + patch[2].NormalL * bary.z);
    float2 texC = patch[0].TexC * bary.x + patch[1].TexC * bary.y + patch[2].TexC * bary.z;

    if (gHasDisplacementMap != 0)
    {
        float height = gDisplacementMap.SampleLevel(gSamLinear, texC, 0.0f).r;
        height = (height - 0.5f) * gDisplacementScale;
        posL += normalL * height;
    }

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    dout.PosW = posW.xyz;
    dout.NormalW = normalize(mul(normalL, (float3x3)gWorld));
    dout.PosH = mul(float4(posL, 1.0f), gWorldViewProj);
    dout.TexC = texC;
    return dout;
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
