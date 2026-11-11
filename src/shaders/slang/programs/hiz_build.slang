// depth pyramid for occlusion culling, kept in a plain buffer (no per-mip views or layouts): level 0 is half the
// depth target, each texel the farthest (reversed z: smallest) of the 2x2 it covers; edges clamp

[[vk::binding(0, 0)]] Texture2D<float> depthTex : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<float> hiz : register(u1, space0);

struct PushData
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
    uint srcOffset;
    uint dstOffset;
    uint fromDepth;
    uint pad;
};
// conduit's d3d12 root constants sit at b0 in space 15
[[vk::push_constant]] ConstantBuffer<PushData> push : register(b0, space15);

float source(int x, int y)
{
    x = min(x, int(push.srcWidth) - 1);
    y = min(y, int(push.srcHeight) - 1);
    if (push.fromDepth != 0u)
        return depthTex.Load(int3(x, y, 0));
    return hiz[push.srcOffset + uint(y) * push.srcWidth + uint(x)];
}

[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.dstWidth || id.y >= push.dstHeight)
        return;
    int x = int(id.x) * 2;
    int y = int(id.y) * 2;
    float d = min(min(source(x, y), source(x + 1, y)), min(source(x, y + 1), source(x + 1, y + 1)));
    hiz[push.dstOffset + id.y * push.dstWidth + id.x] = d;
}
