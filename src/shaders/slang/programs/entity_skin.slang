struct SkinVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;
    float nx;
    float ny;
    float nz;
    uint bone;
};

[[vk::binding(0, 0)]] StructuredBuffer<SkinVertex> skin : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<float4> bones : register(t1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> output : register(u2, space0);

struct PushData
{
    float4 rot0;
    float4 rot1;
    float4 rot2;
    float4 light;
    float4 tint;
    float4 uv;
    uint count;
    uint flat;
    uint outBase;
    uint pad;
};
[[vk::push_constant]] ConstantBuffer<PushData> push : register(b0, space15);

uint quantize(float x)
{
    return uint(saturate(x) * 255.0f + 0.5f);
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= push.count)
        return;
    SkinVertex s = skin[i];
    uint b = s.bone * 4u;
    float4 c0 = bones[b];
    float4 c1 = bones[b + 1u];
    float4 c2 = bones[b + 2u];
    float4 c3 = bones[b + 3u];
    float3 p = c0.xyz * s.x + c1.xyz * s.y + c2.xyz * s.z + c3.xyz;
    float3 color = push.tint.rgb;
    if (push.flat == 0u)
    {
        float lit = 1.0f;
        if (push.light.w > 0.5f)
        {
            float3 n = c0.xyz * s.nx + c1.xyz * s.ny + c2.xyz * s.nz;
            float len = dot(n, n);
            if (len > 1e-12f)
            {
                n = n * rsqrt(len);
                float3 r = push.rot0.xyz * n.x + push.rot1.xyz * n.y + push.rot2.xyz * n.z;
                float3 l0 = normalize(float3(0.2f, 1.0f, -0.7f));
                float3 l1 = normalize(float3(-0.2f, 1.0f, 0.7f));
                lit = min(1.0f, (max(0.0f, dot(r, l0)) + max(0.0f, dot(r, l1))) * 0.6f + 0.4f);
            }
        }
        color = push.light.rgb * lit * push.tint.rgb;
    }
    uint abgr = (quantize(push.tint.a) << 24) | (quantize(color.b) << 16) | (quantize(color.g) << 8) | quantize(color.r);
    uint at = push.outBase + i * 6u;
    output[at + 0u] = asuint(p.x);
    output[at + 1u] = asuint(p.y);
    output[at + 2u] = asuint(p.z);
    output[at + 3u] = abgr;
    output[at + 4u] = asuint(s.u * push.uv.x + push.uv.z);
    output[at + 5u] = asuint(s.v * push.uv.y + push.uv.w);
}
