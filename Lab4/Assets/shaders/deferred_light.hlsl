#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
    int Type; // 0 = directional, 1 = point, 2 = spot
    float3 Pad;
};

cbuffer cbPass : register(b0)
{
    float3 gEyePosW;
    int gNumLights;
    float3 gAmbientLight;
    float gPad0;
    Light gLights[MaxLights];
    int gShowGBuffer;
    float3 gDebugPadding;
};

Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);

SamplerState gSamPoint : register(s0);

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

VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    // Fullscreen triangle
    float2 uv = float2((vid << 1) & 2, vid & 2);
    vout.PosH = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    vout.TexC = uv;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 positionSample = gPositionMap.Sample(gSamPoint, pin.TexC);
    float3 normal = normalize(gNormalMap.Sample(gSamPoint, pin.TexC).xyz);
    float3 albedo = gAlbedoMap.Sample(gSamPoint, pin.TexC).rgb;
    
    if (gShowGBuffer != 0)
    {
        float2 uv = pin.TexC;
        
        if (uv.x < 0.6f && uv.y < 0.25f)
        {
        //position
            if (uv.x < 0.2f)
            {
                float2 debugUV;
                debugUV.x = uv.x / 0.2f;
                debugUV.y = uv.y / 0.25f;

                float3 p =
                gPositionMap.Sample(
                    gSamPoint,
                    debugUV).xyz;

                p = p * 0.05f + 0.5f;

                return float4(p, 1.0f);
            }

        // normal
            else if (uv.x < 0.4f)
            {
                float2 debugUV;
                debugUV.x = (uv.x - 0.2f) / 0.2f;
                debugUV.y = uv.y / 0.25f;

                float3 n =
                gNormalMap.Sample(
                    gSamPoint,
                    debugUV).xyz;

                n = n * 0.5f + 0.5f;

                return float4(n, 1.0f);
            }

        // albedo
            else
            {
                float2 debugUV;
                debugUV.x = (uv.x - 0.4f) / 0.2f;
                debugUV.y = uv.y / 0.25f;

                float3 a =
                gAlbedoMap.Sample(
                    gSamPoint,
                    debugUV).rgb;

                return float4(a, 1.0f);
            }
        }
    }

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
            color += ComputeDirectionalLight(L, normal, toEyeW, albedo);
        else if (L.Type == 1)
            color += ComputePointLight(L, posW, normal, toEyeW, albedo);
        else if (L.Type == 2)
            color += ComputeSpotLight(L, posW, normal, toEyeW, albedo);
    }

    return float4(color, 1.0f);
}
