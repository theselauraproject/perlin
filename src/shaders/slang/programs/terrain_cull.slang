// gpu-driven terrain: one thread per section record, writes compacted indexed indirect draws per (page, layer)
// bucket and counts them for draw_indexed_indirect_count. mode 0 draws everything that survives frustum, radius
// and cave tests; modes 1 and 2 are the two halves of occlusion culling: 1 draws what was visible last frame,
// 2 tests the rest against the depth pyramid built from 1 and keeps the visibility bits for the next frame

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
    // index counts of the facing groups (+x +y +z other -z -y -x), stored back to back from first; unsplit
    // layers keep everything in faces[3]
    uint faces[7];
    uint flags;
    int chunkX;
    int chunkZ;
    uint caveSlot;
    uint caveBit;
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

// bucket: x = first command, y = capacity
[[vk::binding(0, 0)]] StructuredBuffer<SectionRecord> records : register(t0, space0);
[[vk::binding(1, 0)]] StructuredBuffer<uint2> buckets : register(t1, space0);
[[vk::binding(2, 0)]] StructuredBuffer<uint> caveBits : register(t2, space0);
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> visibility : register(u3, space0);
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> commandsEarly : register(u4, space0);
[[vk::binding(5, 0)]] RWStructuredBuffer<uint> commandsLate : register(u5, space0);
[[vk::binding(6, 0)]] RWStructuredBuffer<Atomic<uint>> counts : register(u6, space0);
[[vk::binding(7, 0)]] RWStructuredBuffer<float> hiz : register(u7, space0);
[[vk::binding(8, 0)]] ConstantBuffer<CullParams> params : register(b8, space0);
[[vk::binding(10, 0)]] RWStructuredBuffer<uint> commandsShadow : register(u10, space0);

// mode 3 culls shadow casters: the light's box as six planes, over one slice of the records
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
// conduit's d3d12 root constants sit at b0 in space 15
[[vk::push_constant]] ConstantBuffer<PushData> push : register(b0, space15);

static const uint kFlagLod = 1u;
static const uint kOptFaces = 1u;
static const uint kOptCave = 2u;
static const uint kOptHiz = 4u;

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

bool survivesCoarse(SectionRecord r)
{
    if (r.bucket == 0xFFFFFFFFu)
        return false;
    if ((r.flags & kFlagLod) == 0u && params.cullRadius > 0)
    {
        int dx = r.chunkX - params.cameraChunkX;
        int dz = r.chunkZ - params.cameraChunkZ;
        int reach = params.cullRadius + 1;
        if (abs(dx) > params.cullRadius || abs(dz) > params.cullRadius || dx * dx + dz * dz > reach * reach)
            return false;
    }
    if ((params.options & kOptCave) != 0u && r.caveSlot != 0xFFFFFFFFu && r.caveBit < 32u)
    {
        if ((caveBits[r.caveSlot] & (1u << r.caveBit)) == 0u)
            return false;
    }
    return insideFrustum(r);
}

float4 toClip(float3 p)
{
    float4 v = float4(p, 1.0f);
    return float4(dot(params.clipRow[0], v), dot(params.clipRow[1], v), dot(params.clipRow[2], v),
                  dot(params.clipRow[3], v));
}

float hizAt(uint level, uint w, int x, int y)
{
    return hiz[params.hizOffsets[level >> 2][level & 3u] + uint(y) * w + uint(x)];
}

// reversed z: 1 near, 0 far; the pyramid keeps the farthest depth of each block
bool occluded(SectionRecord r)
{
    float3 c = params.camera.xyz;
    if (c.x >= r.minX && c.x <= r.maxX && c.y >= r.minY && c.y <= r.maxY && c.z >= r.minZ && c.z <= r.maxZ)
        return false;
    float2 lo = float2(1.0f, 1.0f);
    float2 hi = float2(0.0f, 0.0f);
    float nearest = 0.0f;
    for (uint i = 0; i < 8; i++)
    {
        float3 p = float3((i & 1u) != 0u ? r.maxX : r.minX, (i & 2u) != 0u ? r.maxY : r.minY,
                          (i & 4u) != 0u ? r.maxZ : r.minZ);
        float4 clip = toClip(p);
        if (clip.w <= 1e-3f)
            return false;
        float3 ndc = clip.xyz / clip.w;
        // clip space is y-up on every backend: row 0 of the depth target is ndc y = +1
        float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        lo = min(lo, uv);
        hi = max(hi, uv);
        nearest = max(nearest, ndc.z);
    }
    if (hi.x < 0.0f || hi.y < 0.0f || lo.x > 1.0f || lo.y > 1.0f)
        return false;
    lo = saturate(lo);
    hi = saturate(hi);
    float2 size = float2(float(params.depthWidth), float(params.depthHeight));
    // one pixel of slack each side: the depth was drawn with the temporal jitter, the test uses the plain matrix
    int2 p0 = max(int2(lo * size) - 1, int2(0, 0));
    int2 p1 = min(int2(hi * size) + 1, int2(params.depthWidth, params.depthHeight) - 1);
    int extent = max(p1.x - p0.x, p1.y - p0.y) + 1;
    uint level = 0;
    while (level + 1 < params.hizLevels && (2 << level) < extent)
        level++;
    uint w = (params.depthWidth + 1u) >> 1;
    uint h = (params.depthHeight + 1u) >> 1;
    for (uint l = 0; l < level; l++)
    {
        w = max(1u, (w + 1u) >> 1);
        h = max(1u, (h + 1u) >> 1);
    }
    int shift = int(level) + 1;
    int x0 = min(p0.x >> shift, int(w) - 1);
    int y0 = min(p0.y >> shift, int(h) - 1);
    int x1 = min(p1.x >> shift, int(w) - 1);
    int y1 = min(p1.y >> shift, int(h) - 1);
    float farthest = 1.0f;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            farthest = min(farthest, hizAt(level, w, x, y));
    return nearest < farthest;
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

void emit(SectionRecord r, bool late)
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
    uint slot = counts[(late ? push.bucketCount : 0u) + r.bucket].add(runs);
    if (slot + runs > b.y)
        return;
    for (uint i = 0; i < runs; i++)
    {
        uint at = (b.x + slot + i) * 5u;
        if (late)
        {
            commandsLate[at + 0] = runCount[i];
            commandsLate[at + 1] = 1u;
            commandsLate[at + 2] = runFirst[i];
            commandsLate[at + 3] = 0u;
            commandsLate[at + 4] = 0u;
        }
        else
        {
            commandsEarly[at + 0] = runCount[i];
            commandsEarly[at + 1] = 1u;
            commandsEarly[at + 2] = runFirst[i];
            commandsEarly[at + 3] = 0u;
            commandsEarly[at + 4] = 0u;
        }
    }
}

bool insideShadow(SectionRecord r)
{
    for (uint i = 0; i < 6; i++)
    {
        float4 p = push.shadowPlanes[i];
        float x = p.x >= 0.0f ? r.maxX : r.minX;
        float y = p.y >= 0.0f ? r.maxY : r.minY;
        float z = p.z >= 0.0f ? r.maxZ : r.minZ;
        if (p.x * x + p.y * y + p.z * z + p.w < 0.0f)
            return false;
    }
    return true;
}

void emitShadow(SectionRecord r)
{
    uint total = 0;
    for (uint g = 0; g < 7; g++)
        total += r.faces[g];
    if (total == 0u)
        return;
    uint2 b = buckets[r.bucket];
    uint slot = counts[push.bucketCount * 2u + r.bucket].add(1u);
    if (slot + 1u > b.y)
        return;
    uint at = (b.x + slot) * 5u;
    commandsShadow[at + 0] = total;
    commandsShadow[at + 1] = 1u;
    commandsShadow[at + 2] = r.first;
    commandsShadow[at + 3] = 0u;
    commandsShadow[at + 4] = 0u;
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (push.mode == 3u)
    {
        i += push.sliceFirst;
        if (i >= push.sliceEnd || i >= params.recordCount)
            return;
        SectionRecord sr = records[i];
        // shadows are drawn without culling: every facing casts
        if (sr.bucket != 0xFFFFFFFFu && insideShadow(sr))
            emitShadow(sr);
        return;
    }
    if (i >= params.recordCount)
        return;
    SectionRecord r = records[i];
    bool alive = survivesCoarse(r);
    if (push.mode == 0u)
    {
        if (alive)
            emit(r, false);
        return;
    }
    if (push.mode == 1u)
    {
        if (alive && visibility[i] != 0u)
            emit(r, false);
        return;
    }
    if (!alive)
    {
        visibility[i] = 0u;
        return;
    }
    bool seen = (params.options & kOptHiz) == 0u || push.hizValid == 0u || !occluded(r);
    uint was = visibility[i];
    visibility[i] = seen ? 1u : 0u;
    if (seen && was == 0u)
        emit(r, true);
}
