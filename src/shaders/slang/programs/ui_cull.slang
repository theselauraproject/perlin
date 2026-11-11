// world-space ui, entity and particle multi draws: each draw's bounding sphere against the frustum; a draw
// outside it keeps its slot with no instances, so the multi draw keeps its order and size

struct Entry
{
    uint command[5];
    uint pad[3];
    float4 bounds;
};

[[vk::binding(0, 0)]] StructuredBuffer<Entry> entries : register(t0, space0);
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output : register(u1, space0);

struct PushData
{
    float4 planes[6];
    uint count;
    uint pad0;
    uint pad1;
    uint pad2;
};
// conduit's d3d12 root constants sit at b0 in space 15
[[vk::push_constant]] ConstantBuffer<PushData> push : register(b0, space15);

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= push.count)
        return;
    Entry e = entries[i];
    bool visible = true;
    if (e.bounds.w >= 0.0f)
    {
        for (uint p = 0; p < 6; p++)
        {
            float4 plane = push.planes[p];
            if (dot(plane.xyz, e.bounds.xyz) + plane.w < -e.bounds.w)
                visible = false;
        }
    }
    uint at = i * 5u;
    output[at + 0] = e.command[0];
    output[at + 1] = visible ? e.command[1] : 0u;
    output[at + 2] = e.command[2];
    output[at + 3] = e.command[3];
    output[at + 4] = e.command[4];
}
