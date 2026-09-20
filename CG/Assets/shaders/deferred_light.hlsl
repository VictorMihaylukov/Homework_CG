#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
    int Type;
    float3 Pad;
};

cbuffer cbPass : register(b0)
{
    float3 gEyePosW;
    int gNumLights;
    float3 gAmbientLight;
    float gPad0;
    Light gLights[MaxLights];
    float4x4 gView;
    float4x4 gShadowTransform[4];
    float4 gCascadeSplits;
    uint gPostEffectFlags;
    float3 gPostPad;
};

Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);
Texture2DArray<float> gShadowMap : register(t3);

SamplerState gSamPoint : register(s0);
SamplerComparisonState gSamShadow : register(s1);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / max(falloffEnd - falloffStart, 0.0001f));
}

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, float3 albedo)
{
    const float m = 32.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(float3(0.04f, 0.04f, 0.04f), halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (albedo + specAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light L, float3 normal, float3 toEye, float3 albedo)
{
    float3 lightVec = -normalize(L.Direction);
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye, albedo);
}

float3 ComputePointLight(Light L, float3 pos, float3 normal, float3 toEye, float3 albedo)
{
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);

    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    lightVec /= d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, albedo);
}

float3 ComputeSpotLight(Light L, float3 pos, float3 normal, float3 toEye, float3 albedo)
{
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);

    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    lightVec /= d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    float spotFactor = pow(max(dot(-lightVec, normalize(L.Direction)), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, albedo);
}


float CalcShadow(float3 posW, int cascade)
{
    float4 p = mul(float4(posW,1.0f), gShadowTransform[cascade]);
    p.xyz /= p.w;
    float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);
    if(p.z <= 0.0f || p.z >= 1.0f || any(uv < 0.0f) || any(uv > 1.0f)) return 1.0f;
    uint w,h,layers; gShadowMap.GetDimensions(w,h,layers);
    float2 texel = 1.0f / float2(w,h);
    float visibility=0.0f;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
        visibility += gShadowMap.SampleCmpLevelZero(gSamShadow,float3(uv+float2(x,y)*texel,cascade),p.z-0.0008f);
    return visibility/9.0f;
}

VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    static const float2 positions[6] =
    {
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f),
        float2(-1.0f, -1.0f),
        float2( 1.0f,  1.0f),
        float2( 1.0f, -1.0f)
    };

    static const float2 texCoords[6] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };

    vout.PosH = float4(positions[vid], 0.0f, 1.0f);
    vout.TexC = texCoords[vid];
    return vout;
}

float3 ApplyGrayscale(float3 color)
{
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    return luminance.xxx;
}

float3 ApplyVignette(float3 color, float2 uv)
{
    float2 centered = uv * 2.0f - 1.0f;
    float radius = length(centered);
    float factor = 1.0f - smoothstep(0.45f, 1.35f, radius);
    return color * lerp(0.35f, 1.0f, factor);
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 positionSample = gPositionMap.Sample(gSamPoint, pin.TexC);
    float3 normal = normalize(gNormalMap.Sample(gSamPoint, pin.TexC).xyz);
    float3 albedo = gAlbedoMap.Sample(gSamPoint, pin.TexC).rgb;
    
    if (positionSample.a < 0.5f)
        return float4(0.02f, 0.02f, 0.03f, 1.0f);

    float3 posW = positionSample.xyz;
    float3 toEyeW = normalize(gEyePosW - posW);

    float3 color = albedo * gAmbientLight;

    [unroll]
    for (int i = 0; i < MaxLights; ++i)
    {
        if (i >= gNumLights)
            break;

        Light L = gLights[i];

        if (L.Type == 0)
        {
            float depthV = mul(float4(posW,1.0f), gView).z;
            int cascade = depthV > gCascadeSplits.x ? 1 : 0;
            cascade = depthV > gCascadeSplits.y ? 2 : cascade;
            cascade = depthV > gCascadeSplits.z ? 3 : cascade;
            float visibility = CalcShadow(posW, cascade);
            color += visibility * ComputeDirectionalLight(L, normal, toEyeW, albedo);
        }
        else if (L.Type == 1)
            color += ComputePointLight(L, posW, normal, toEyeW, albedo);
        else if (L.Type == 2)
            color += ComputeSpotLight(L, posW, normal, toEyeW, albedo);
    }

    if ((gPostEffectFlags & 1u) != 0u)
        color = ApplyGrayscale(color);

    if ((gPostEffectFlags & 2u) != 0u)
        color = ApplyVignette(color, pin.TexC);

    return float4(color, 1.0f);
}
