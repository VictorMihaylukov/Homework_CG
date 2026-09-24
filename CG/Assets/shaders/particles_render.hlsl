struct Particle
{
    float3 Position;
    float Age;
    float3 Velocity;
    float Lifetime;
    float4 Color;
    float Size;
    float Seed;
    float2 Pad;
};

StructuredBuffer<Particle> gParticles : register(t0);

cbuffer RenderCB : register(b0)
{
    float4x4 gViewProj;
    float3 gCameraRight;
    float gPad0;
    float3 gCameraUp;
    float gPad1;
};

struct VSOut
{
    float3 PositionW : POSITION;
    float4 Color : COLOR;
    float Size : SIZE;
};

VSOut VS(uint vertexId : SV_VertexID)
{
    Particle p = gParticles[vertexId];
    VSOut o;
    o.PositionW = p.Position;
    o.Color = p.Color;
    o.Size = p.Size;
    return o;
}

struct GSOut
{
    float4 PositionH : SV_POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD;
};

[maxvertexcount(4)]
void GS(point VSOut input[1], inout TriangleStream<GSOut> stream)
{
    float3 center = input[0].PositionW;
    float s = input[0].Size;
    float3 right = gCameraRight * s;
    float3 up = gCameraUp * s;

    float3 corners[4] =
    {
        center - right + up,
        center + right + up,
        center - right - up,
        center + right - up
    };
    float2 uvs[4] =
    {
        float2(0,0), float2(1,0), float2(0,1), float2(1,1)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GSOut o;
        o.PositionH = mul(float4(corners[i], 1.0f), gViewProj);
        o.Color = input[0].Color;
        o.UV = uvs[i];
        stream.Append(o);
    }
}

float4 PS(GSOut input) : SV_Target
{
    float2 p = input.UV * 2.0f - 1.0f;
    float radius = length(p);
    float arm0 = abs(p.y);
    float arm1 = abs(0.8660254f * p.x + 0.5f * p.y);
    float arm2 = abs(-0.8660254f * p.x + 0.5f * p.y);
    float armDistance = min(arm0, min(arm1, arm2));
    float snowflake = 1.0f - smoothstep(0.035f, 0.085f, armDistance);
    snowflake *= 1.0f - smoothstep(0.42f, 0.50f, radius);

    if (snowflake < 0.15f)
        discard;

    float glow = 1.0f - smoothstep(0.0f, 0.5f, radius);
    float3 color = input.Color.rgb * (0.85f + 0.35f * glow);
    return float4(color, 1.0f);
}
