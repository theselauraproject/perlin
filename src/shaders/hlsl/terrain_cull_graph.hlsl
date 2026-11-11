// work graph version of terrain_cull.slang modes 0 and 1 (the late occlusion pass stays a compute dispatch).
// two nodes: one thread per chunk rejects whole chunks (frustum, radius), survivors launch one small grid over
// just their own section records. compiled with dxc -T lib_6_8; bindings match the compute pipeline layout so the
// graph shares its bind group, records and outputs. keep the record logic in step with terrain_cull.slang

struct SectionRecord
{
    float minX;
    float minY;
    float minZ;
    uint first;
    float maxX;
    float maxY;
    float maxZ;
    uint bucket;
    uint faces[7];
    uint flags;
    int chunkX;
    int chunkZ;
    uint caveSlot;
    uint caveBit;
};

struct ChunkRow
{
    uint start;
    uint count;
    int chunkX;
    int chunkZ;
    float minX;
    float minY;
    float minZ;
    uint flags;
    float maxX;
    float maxY;
    float maxZ;
    uint pad;
};

struct CullParams
{
    float4 planes[6];
    float4 camera;
    float4 clipRow[4];
    uint recordCount;
    int cullRadius;
    int cameraChunkX;
    int cameraChunkZ;
    uint hizLevels;
    uint depthWidth;
    uint depthHeight;
    uint options;
    uint4 hizOffsets[4];
};

struct PushData
{
    uint mode;
    uint bucketCount;
    uint hizValid;
    uint chunkCount;
    uint sliceFirst;
    uint sliceEnd;
    uint pad0;
    uint pad1;
    float4 shadowPlanes[6];
};

StructuredBuffer<SectionRecord> records : register(t0, space0);
StructuredBuffer<uint2> buckets : register(t1, space0);
StructuredBuffer<uint> caveBits : register(t2, space0);
RWStructuredBuffer<uint> visibility : register(u3, space0);
RWStructuredBuffer<uint> commandsEarly : register(u4, space0);
RWStructuredBuffer<uint> commandsLate : register(u5, space0);
RWStructuredBuffer<uint> counts : register(u6, space0);
RWStructuredBuffer<float> hiz : register(u7, space0);
ConstantBuffer<CullParams> params : register(b8, space0);
StructuredBuffer<ChunkRow> chunkTable : register(t9, space0);
ConstantBuffer<PushData> push : register(b0, space15);

static const uint kFlagLod = 1u;
static const uint kOptFaces = 1u;
static const uint kOptCave = 2u;

bool boxInFrustum(float3 lo, float3 hi)
{
    for (uint i = 0; i < 6; i++)
    {
        float4 p = params.planes[i];
        float x = p.x >= 0.0f ? hi.x : lo.x;
        float y = p.y >= 0.0f ? hi.y : lo.y;
        float z = p.z >= 0.0f ? hi.z : lo.z;
        if (p.x * x + p.y * y + p.z * z + p.w < 0.0f)
            return false;
    }
    return true;
}

bool inRadius(int cx, int cz, uint flags)
{
    if ((flags & kFlagLod) != 0u || params.cullRadius <= 0)
        return true;
    int dx = cx - params.cameraChunkX;
    int dz = cz - params.cameraChunkZ;
    int reach = params.cullRadius + 1;
    return abs(dx) <= params.cullRadius && abs(dz) <= params.cullRadius && dx * dx + dz * dz <= reach * reach;
}

uint visibleFaces(SectionRecord r)
{
    if ((params.options & kOptFaces) == 0u)
        return 0x7Fu;
    float3 c = params.camera.xyz;
    float m = params.camera.w;
    uint mask = 1u << 3;
    if (c.x > r.minX - m)
        mask |= 1u << 0;
    if (c.y > r.minY - m)
        mask |= 1u << 1;
    if (c.z > r.minZ - m)
        mask |= 1u << 2;
    if (c.z < r.maxZ + m)
        mask |= 1u << 4;
    if (c.y < r.maxY + m)
        mask |= 1u << 5;
    if (c.x < r.maxX + m)
        mask |= 1u << 6;
    return mask;
}

void emit(SectionRecord r)
{
    uint mask = visibleFaces(r);
    uint runFirst[4] = { 0u, 0u, 0u, 0u };
    uint runCount[4] = { 0u, 0u, 0u, 0u };
    uint runs = 0;
    uint offset = r.first;
    bool open = false;
    for (uint g = 0; g < 7; g++)
    {
        uint n = r.faces[g];
        if (n != 0u && (mask & (1u << g)) != 0u)
        {
            if (open)
                runCount[runs - 1] += n;
            else if (runs < 4)
            {
                runFirst[runs] = offset;
                runCount[runs] = n;
                runs++;
                open = true;
            }
        }
        else if (n != 0u)
            open = false;
        offset += n;
    }
    if (runs == 0u)
        return;
    uint2 b = buckets[r.bucket];
    uint slot;
    InterlockedAdd(counts[r.bucket], runs, slot);
    if (slot + runs > b.y)
        return;
    for (uint i = 0; i < runs; i++)
    {
        uint at = (b.x + slot + i) * 5u;
        commandsEarly[at + 0] = runCount[i];
        commandsEarly[at + 1] = 1u;
        commandsEarly[at + 2] = runFirst[i];
        commandsEarly[at + 3] = 0u;
        commandsEarly[at + 4] = 0u;
    }
}

struct ChunkInput
{
    uint grid : SV_DispatchGrid;
    uint chunkCount;
};

struct SectionBatch
{
    uint grid : SV_DispatchGrid;
    uint first;
    uint count;
};

[Shader("node")]
[NodeIsProgramEntry]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(65535, 1, 1)]
[NumThreads(64, 1, 1)]
void chunks(DispatchNodeInputRecord<ChunkInput> input, uint tid : SV_DispatchThreadID,
            [MaxRecords(64)] NodeOutput<SectionBatch> sections)
{
    bool pass = false;
    ChunkRow row = (ChunkRow)0;
    if (tid < input.Get().chunkCount)
    {
        row = chunkTable[tid];
        pass = row.count != 0u && inRadius(row.chunkX, row.chunkZ, row.flags) &&
               boxInFrustum(float3(row.minX, row.minY, row.minZ), float3(row.maxX, row.maxY, row.maxZ));
    }
    ThreadNodeOutputRecords<SectionBatch> out = sections.GetThreadNodeOutputRecords(pass ? 1 : 0);
    if (pass)
    {
        out.Get().grid = min((row.count + 31u) / 32u, 64u);
        out.Get().first = row.start;
        out.Get().count = row.count;
    }
    out.OutputComplete();
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(64, 1, 1)]
[NumThreads(32, 1, 1)]
void sections(DispatchNodeInputRecord<SectionBatch> input, uint tid : SV_DispatchThreadID)
{
    SectionBatch batch = input.Get();
    if (tid >= batch.count)
        return;
    uint i = batch.first + tid;
    SectionRecord r = records[i];
    if (r.bucket == 0xFFFFFFFFu)
        return;
    if ((params.options & kOptCave) != 0u && r.caveSlot != 0xFFFFFFFFu && r.caveBit < 32u &&
        (caveBits[r.caveSlot] & (1u << r.caveBit)) == 0u)
        return;
    if (!boxInFrustum(float3(r.minX, r.minY, r.minZ), float3(r.maxX, r.maxY, r.maxZ)))
        return;
    if (push.mode == 1u && visibility[i] == 0u)
        return;
    emit(r);
}
