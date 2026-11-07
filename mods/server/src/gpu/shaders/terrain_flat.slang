// one thread per cell of every chunk in the batch; the block is a function of (x, y, z, seed)
// flat terrain only looks y up in a table; vanilla terrain replaces blockAt with noise and keeps everything around it
//   set 0 binding 0  profile: block id per world y, from the floor up
//   set 0 binding 1  chunks: chunk x, z per batch entry
//   set 0 binding 2  cells: out, [chunk][y][z][x], one uint per cell
// push constants: chunk count, seed low, seed high, unused

struct Params
{
    uint chunkCount;
    uint seedLo;
    uint seedHi;
    uint reserved;
};

[[vk::push_constant]] ConstantBuffer<Params> params;
[[vk::binding(0, 0)]] StructuredBuffer<uint> profile;
[[vk::binding(1, 0)]] StructuredBuffer<int2> chunks;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> cells;

static const uint kHeight = 384;
static const uint kColumn = 256;

uint blockAt(int3 world, uint2 seed)
{
    return profile[world.y];
}

[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint cell = id.x;
    uint perChunk = kHeight * kColumn;
    if (cell >= params.chunkCount * perChunk)
        return;
    uint chunk = cell / perChunk;
    uint inChunk = cell - chunk * perChunk;
    uint y = inChunk / kColumn;
    uint z = (inChunk / 16) & 15;
    uint x = inChunk & 15;
    int2 origin = chunks[chunk] * 16;
    cells[cell] = blockAt(int3(origin.x + int(x), int(y), origin.y + int(z)), uint2(params.seedLo, params.seedHi));
}
