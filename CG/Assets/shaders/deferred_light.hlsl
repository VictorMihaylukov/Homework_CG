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
    float4x4 gInvViewProj;
    float4x4 gShadowTransform[4];
    float4 gCascadeSplits;
    float4x4 gSpotShadowTransform[2];
    int4 gSpotShadowLightIndices;
    uint gPostEffectFlags;
    float gCameraSpeed;
    float2 gPostPad;
};

Texture2D gDepthMap    : register(t0);
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


float CalcShadow(float3 posW, float4x4 shadowTransform, int slice)
{
    float4 p = mul(float4(posW,1.0f), shadowTransform);
    p.xyz /= p.w;
    float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);
    if(p.z <= 0.0f || p.z >= 1.0f || any(uv < 0.0f) || any(uv > 1.0f)) return 1.0f;
    uint w,h,layers; gShadowMap.GetDimensions(w,h,layers);
    float2 texel = 1.0f / float2(w,h);
    float visibility=0.0f;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x)
        visibility += gShadowMap.SampleCmpLevelZero(gSamShadow,float3(uv+float2(x,y)*texel,slice),p.z-0.0008f);
    return visibility/9.0f;
}

float CalcDirectionalShadow(float3 posW, int cascade)
{
    return CalcShadow(posW,gShadowTransform[cascade],cascade);
}

float CalcSpotShadow(float3 posW, int lightIndex)
{
    if(gSpotShadowLightIndices.x==lightIndex)
        return CalcShadow(posW,gSpotShadowTransform[0],4);
    if(gSpotShadowLightIndices.y==lightIndex)
        return CalcShadow(posW,gSpotShadowTransform[1],5);
    return 1.0f;
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

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float4 posH = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        depth,
        1.0f);
    float4 reconstructed = mul(posH, gInvViewProj);
    return reconstructed.xyz / reconstructed.w;
}

float ComputeEdgeStrength(float2 uv)
{
    uint width, height;
    gDepthMap.GetDimensions(width, height);
    float2 texel = 1.0f / float2(width, height);

    float centerDepth = gDepthMap.SampleLevel(gSamPoint, uv, 0.0f).r;
    float3 centerNormal = gNormalMap.SampleLevel(gSamPoint, uv, 0.0f).xyz;
    bool centerValid = centerDepth < 1.0f;

    static const float2 offsets[4] =
    {
        float2(-1.0f, 0.0f), float2(1.0f, 0.0f),
        float2(0.0f, -1.0f), float2(0.0f, 1.0f)
    };

    float silhouetteEdge = 0.0f;
    float normalEdge = 0.0f;
    float depthEdge = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 sampleUv = saturate(uv + offsets[i] * texel);
        float neighborDepth = gDepthMap.SampleLevel(gSamPoint, sampleUv, 0.0f).r;
        float3 neighborNormal = gNormalMap.SampleLevel(gSamPoint, sampleUv, 0.0f).xyz;
        bool neighborValid = neighborDepth < 1.0f;

        silhouetteEdge = max(silhouetteEdge, centerValid != neighborValid ? 1.0f : 0.0f);

        if (centerValid && neighborValid)
        {
            float3 n0 = normalize(centerNormal);
            float3 n1 = normalize(neighborNormal);
            normalEdge = max(normalEdge, 1.0f - saturate(dot(n0, n1)));

            float3 centerPos = ReconstructWorldPosition(uv, centerDepth);
            float3 neighborPos = ReconstructWorldPosition(sampleUv, neighborDepth);
            float d0 = length(centerPos - gEyePosW);
            float d1 = length(neighborPos - gEyePosW);
            float relativeDepthJump = abs(d0 - d1) / max(d0 * 0.02f, 0.05f);
            depthEdge = max(depthEdge, relativeDepthJump);
        }
    }

    float geometricEdge = max(
        smoothstep(0.08f, 0.35f, normalEdge),
        smoothstep(0.35f, 1.25f, depthEdge));

    return saturate(max(silhouetteEdge, geometricEdge));
}

float3 GetVelocityEdgeColor()
{
    float speed01 = saturate(gCameraSpeed / 25.0f);
    return lerp(float3(0.05f, 0.25f, 1.0f),
                float3(1.0f, 0.05f, 0.02f), speed01);
}

float4 PS(VertexOut pin) : SV_Target
{
    float depth = gDepthMap.Sample(gSamPoint, pin.TexC).r;
    float3 normal = normalize(gNormalMap.Sample(gSamPoint, pin.TexC).xyz);
    float3 albedo = gAlbedoMap.Sample(gSamPoint, pin.TexC).rgb;

    if ((gPostEffectFlags & 4u) != 0u && pin.TexC.x < 0.60f && pin.TexC.y < 0.25f)
    {
        if (pin.TexC.x < 0.20f)
        {
            float2 debugUV = float2(pin.TexC.x / 0.20f, pin.TexC.y / 0.25f);
            float d = gDepthMap.Sample(gSamPoint, debugUV).r;
            return float4(d, d, d, 1.0f);
        }

        if (pin.TexC.x < 0.40f)
        {
            float2 debugUV = float2((pin.TexC.x - 0.20f) / 0.20f, pin.TexC.y / 0.25f);
            float3 n = gNormalMap.Sample(gSamPoint, debugUV).xyz;
            return float4(n * 0.5f + 0.5f, 1.0f);
        }

        float2 debugUV = float2((pin.TexC.x - 0.40f) / 0.20f, pin.TexC.y / 0.25f);
        return float4(gAlbedoMap.Sample(gSamPoint, debugUV).rgb, 1.0f);
    }

    const bool hasGeometry = depth < 1.0f;
    float3 color = float3(0.02f, 0.02f, 0.03f);

    if (hasGeometry)
    {
        float3 posW = ReconstructWorldPosition(pin.TexC, depth);
        float3 toEyeW = normalize(gEyePosW - posW);
        color = albedo * gAmbientLight;

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
                float visibility = CalcDirectionalShadow(posW,cascade);
                color += visibility * ComputeDirectionalLight(L, normal, toEyeW, albedo);
            }
            else if (L.Type == 1)
                color += ComputePointLight(L, posW, normal, toEyeW, albedo);
            else if (L.Type == 2)
            {
                float visibility=CalcSpotShadow(posW,i);
                color += visibility*ComputeSpotLight(L,posW,normal,toEyeW,albedo);
            }
        }
    }

    if ((gPostEffectFlags & 1u) != 0u)
        color = ApplyGrayscale(color);

    if ((gPostEffectFlags & 2u) != 0u)
        color = ApplyVignette(color, pin.TexC);

    if ((gPostEffectFlags & 8u) != 0u)
    {
        float edge = ComputeEdgeStrength(pin.TexC);
        color = lerp(color, GetVelocityEdgeColor(), edge);
    }

    return float4(color, 1.0f);
}
