cbuffer cbShadow : register(b0)
{
    float4x4 gWorldLightViewProj;
};

struct VertexIn { float3 PosL : POSITION; };
struct VertexOut { float4 PosH : SV_POSITION; };

VertexOut VS(VertexIn vin)
{
    VertexOut o;
    o.PosH = mul(float4(vin.PosL, 1.0f), gWorldLightViewProj);
    return o;
}
