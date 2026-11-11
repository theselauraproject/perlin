struct CullParams
{
    float4 planes[6];
    float4 camera;
    uint recordCount;
    int cullRadius;
    int cameraChunkX;
    int cameraChunkZ;
};

struct SectionRecord
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
    uint first;
    uint count;
    int chunkX;
    int chunkZ;
    uint flags;
    uint pad;
};

[[vk::push_constant]] ConstantBuffer<CullParams> params;
[[vk::binding(0, 0)]] StructuredBuffer<SectionRecord> records;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> commands;

bool insideFrustum(SectionRecord r)
{
    for (uint i = 0; i < 6; i++)
    {
        float4 p = params.planes[i];
        float x = p.x >= 0.0f ? r.maxX : r.minX;
        float y = p.y >= 0.0f ? r.maxY : r.minY;
        float z = p.z >= 0.0f ? r.maxZ : r.minZ;
        if (p.x * x + p.y * y + p.z * z + p.w < 0.0f)
            return false;
    }
    return true;
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= params.recordCount)
        return;
    SectionRecord r = records[i];
    bool visible = r.count != 0 && (r.flags & 1u) == 0 && insideFrustum(r);
    if (visible && params.cullRadius > 0 && (r.flags & 2u) == 0)
    {
        int dx = r.chunkX - params.cameraChunkX;
        int dz = r.chunkZ - params.cameraChunkZ;
        int reach = params.cullRadius + 1;
        if (abs(dx) > params.cullRadius || abs(dz) > params.cullRadius || dx * dx + dz * dz > reach * reach)
            visible = false;
    }
    uint at = i * 5;
    commands[at + 0] = visible ? r.count : 0u;
    commands[at + 1] = visible ? 1u : 0u;
    commands[at + 2] = r.first;
    commands[at + 3] = 0u;
    commands[at + 4] = 0u;
}
