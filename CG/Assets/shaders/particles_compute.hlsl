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
AppendStructuredBuffer<Particle> gAppend : register(u1);

cbuffer ComputeCB : register(b0)
{
    float gDeltaTime;
    float gTotalTime;
    float gEmitterX;
    float gEmitterY;

    float gEmitterZ;
    float3 gPad;
};

float Hash(float n)
{
    return frac(sin(n * 12.9898f) * 43758.5453f);
}

[numthreads(128, 1, 1)]
void CS(uint3 tid : SV_DispatchThreadID)
{
    Particle p = gConsume.Consume();

    if (p.Position.y < 0.0f)
    {
        float h0 = Hash(p.Seed + 1.0f);
        float h1 = Hash(p.Seed + 17.0f);
        float h2 = Hash(p.Seed + 41.0f);
        float h3 = Hash(p.Seed + 83.0f);

        p.Position.x = gEmitterX + (h0 - 0.5f) * 52.0f;
        p.Position.y = gEmitterY + h1 * 1.5f;
        p.Position.z = gEmitterZ + (h2 - 0.5f) * 42.0f;

        p.Velocity.x = 0.0f;
        p.Velocity.y = -(0.8f + h3 * 0.7f);
        p.Velocity.z = 0.0f;

        p.Age = 0.0f;
        p.Lifetime = 1000.0f;

        p.Size = 0.05f + h2 * 0.05f;

        p.Color = float4( 0.92f + h0 * 0.08f, 0.95f + h1 * 0.05f, 1.0f, 1.0f);

        p.Seed += 13.37f;
    }

    float swayX =
        sin(gTotalTime * 1.2f + p.Seed) * 0.12f;

    float swayZ =
        cos(gTotalTime * 0.9f + p.Seed * 1.31f) * 0.08f;

    p.Position.x += swayX * gDeltaTime;
    p.Position.z += swayZ * gDeltaTime;

    p.Position.y += p.Velocity.y * gDeltaTime;

    p.Age += gDeltaTime;

    gAppend.Append(p);
}
