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

ConsumeStructuredBuffer<Particle> gConsume : register(u0);
AppendStructuredBuffer<Particle>  gAppend  : register(u1);

cbuffer ComputeCB : register(b0)
{
    float gDeltaTime;
    float gTotalTime;
    float3 gEmitter;
    float gDrag;
    float3 gGravity;
    float gSpeed;
    float2 gComputePad;
};

float Hash(float n)
{
    return frac(sin(n * 12.9898 + gTotalTime * 0.173) * 43758.5453);
}

[numthreads(128, 1, 1)]
void CS(uint3 tid : SV_DispatchThreadID)
{
    Particle p = gConsume.Consume();
    p.Age += gDeltaTime;

    if (p.Age >= p.Lifetime || p.Position.y < 0.05f)
    {
        float h0 = Hash(p.Seed + 1.0f);
        float h1 = Hash(p.Seed + 17.0f);
        float h2 = Hash(p.Seed + 41.0f);
        float angle = h0 * 6.2831853f;
        float radius = 0.15f + h1 * 0.75f;

        p.Position = gEmitter + float3(cos(angle) * radius, 0, sin(angle) * radius);
        p.Velocity = float3(cos(angle) * (0.4f + h1), 2.8f + h2 * 2.8f, sin(angle) * (0.4f + h1)) * gSpeed;
        p.Age = 0.0f;
        p.Lifetime = 2.8f + h1 * 2.5f;
        p.Size = 0.11f + h2 * 0.20f;
        p.Color = float4(1.0f, 0.25f + 0.65f * h1, 0.04f + 0.12f * h2, 1.0f);
        p.Seed += 13.37f;
    }
    else
    {
        p.Velocity += gGravity * gDeltaTime;
        p.Velocity *= max(0.0f, 1.0f - gDrag * gDeltaTime);
        p.Position += p.Velocity * gDeltaTime;
    }

    gAppend.Append(p);
}
