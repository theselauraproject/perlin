
#include "protocol/Packets.hpp"
#include "network/VarInt.hpp"
#include "world/level/chunk/ChunkCodec.hpp"

#include <cstdio>
#include <random>

using namespace perlin;
using namespace perlin::proto;

template <class P> static bool Decode(const uint8_t* data, size_t size, P& packet) {
    perlin::net::ReadStream stream(data, size);
    return Read(stream, packet);
}
static int fails = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                               \
            fails++;                                                                                         \
        }                                                                                                    \
    } while (0)

static ChunkCodec::CellIds MakeCells(int distinct, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<int32_t> vals;
    vals.reserve(distinct);
    for (int i = 0; i < distinct; i++)
        vals.push_back((int32_t)rng());
    ChunkCodec::CellIds c;
    for (auto& v : c)
        v = vals[rng() % vals.size()];
    return c;
}

int main() {
    {
        ChunkCodec::CellIds first, second;
        first.fill(5);
        second.fill(19);
        std::vector<uint8_t> column;
        ChunkCodec::WriteSubChunk(column, -4, first);
        const size_t firstSize = column.size();
        ChunkCodec::WriteSubChunk(column, -3, second);
        std::vector<std::pair<int, SubChunkData>> parsed;
        size_t consumed = 0;
        CHECK(ChunkCodec::ParseColumn(column.data(), column.size(), 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2 && consumed == column.size());
        CHECK(parsed[0].first == -4 && parsed[1].first == -3);
        ChunkCodec::CellIds expanded;
        CHECK(ChunkCodec::Expand(parsed[1].second.storages[0], expanded));
        CHECK(expanded == second);
        // the payload decides, not the count: a column cut cleanly after the first sub-chunk holds one
        CHECK(ChunkCodec::ParseColumn(column.data(), firstSize, 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 1 && parsed[0].first == -4 && consumed == firstSize);
        // a cut inside a sub-chunk is still rejected, with nothing half-applied
        for (size_t cut = firstSize + 1; cut < column.size(); ++cut) {
            CHECK(!ChunkCodec::ParseColumn(column.data(), cut, 2, -4, 24, parsed, consumed));
            CHECK(parsed.empty() && consumed == 0);
        }
        // proxies repeat heights: the first entry wins and the repeat is skipped
        std::vector<uint8_t> duplicate;
        ChunkCodec::WriteSubChunk(duplicate, -4, first);
        ChunkCodec::WriteSubChunk(duplicate, -4, second);
        CHECK(ChunkCodec::ParseColumn(duplicate.data(), duplicate.size(), 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 1 && parsed[0].first == -4 && consumed == duplicate.size());
        // a count above the dimension height or a raised floor no longer rejects a payload that fits the union
        CHECK(ChunkCodec::ParseColumn(column.data(), column.size(), 25, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2);
        CHECK(ChunkCodec::ParseColumn(column.data(), column.size(), 2, 0, 24, parsed, consumed));
        CHECK(parsed.size() == 2);
        // 3d biomes follow the sub-chunks; their palette header (bpb << 1 | 1) is 0x01 or 0x09
        std::vector<uint8_t> biomes = {0x01, 0x00};
        biomes.insert(biomes.end(), 23, 0xff);
        biomes.push_back(0x00);
        std::vector<uint8_t> withBiomes = column;
        withBiomes.insert(withBiomes.end(), biomes.begin(), biomes.end());
        CHECK(ChunkCodec::ParseColumn(withBiomes.data(), withBiomes.size(), 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2 && consumed == column.size());
        CHECK(ChunkCodec::ParseColumn(biomes.data(), biomes.size(), 0, -4, 24, parsed, consumed));
        CHECK(parsed.empty() && consumed == 0);
        std::vector<uint8_t> wideBiome = column;
        wideBiome.push_back(0x09);
        wideBiome.insert(wideBiome.end(), 512 * 4, 0x00);
        wideBiome.push_back(0x02);
        wideBiome.push_back(0x00);
        CHECK(ChunkCodec::ParseColumn(wideBiome.data(), wideBiome.size(), 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2 && consumed == column.size());
        // an empty sub-chunk (no layers) still takes its slot
        std::vector<uint8_t> empty = {9, 0, 0xfc};
        empty.insert(empty.end(), column.begin() + static_cast<std::ptrdiff_t>(firstSize), column.end());
        CHECK(ChunkCodec::ParseColumn(empty.data(), empty.size(), 2, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2 && parsed[1].first == -3 && consumed == empty.size());
        // a count below the payload: further sub-chunks continuing the heights are still read
        CHECK(ChunkCodec::ParseColumn(withBiomes.data(), withBiomes.size(), 1, -4, 24, parsed, consumed));
        CHECK(parsed.size() == 2 && consumed == column.size());
        // a count with no payload at all is still an error
        CHECK(!ChunkCodec::ParseColumn(nullptr, 0, 2, -4, 24, parsed, consumed));
        CHECK(ChunkCodec::ParseColumn(nullptr, 0, 0, -4, 24, parsed, consumed));
        CHECK(parsed.empty() && consumed == 0);
    }

    for (int distinct : {1, 2, 3, 4, 5, 9, 17, 40, 65, 200, 300}) {
        auto cells = MakeCells(distinct, 1000 + distinct);
        std::vector<uint8_t> buf;
        ChunkCodec::WriteSubChunk(buf, -4, cells);
        buf.push_back(0xAB);
        SubChunkData sc;
        size_t used = 0;
        const char* err = nullptr;
        CHECK(ChunkCodec::ParseSubChunk(buf.data(), buf.size(), sc, used, &err));
        CHECK(used == buf.size() - 1);
        CHECK(sc.version == 9 && sc.hasYIndex && sc.yIndex == -4 && sc.storages.size() == 1);
        ChunkCodec::CellIds back;
        CHECK(ChunkCodec::Expand(sc.storages[0], back));
        CHECK(back == cells);
    }

    for (auto [distinct, words] : {std::pair{5, 410}, {20, 683}, {40, 820}}) {
        auto cells = MakeCells(distinct, 7 + distinct);
        std::vector<uint8_t> b;
        ChunkCodec::WriteStorage(b, cells);
        PaletteStorage st;
        wire::Reader r(b.data(), b.size());
        SubChunkData sc;
        size_t used;
        std::vector<uint8_t> sub = {9, 1, 0};
        sub.insert(sub.end(), b.begin(), b.end());
        CHECK(ChunkCodec::ParseSubChunk(sub.data(), sub.size(), sc, used));
        CHECK((int)sc.storages[0].words.size() == words);
    }

    {
        auto cells = MakeCells(30, 5);
        std::vector<uint8_t> buf;
        ChunkCodec::WriteSubChunk(buf, 0, cells);
        for (size_t cut = 0; cut + 1 < buf.size(); cut += 37) {
            SubChunkData sc;
            size_t used;
            CHECK(!ChunkCodec::ParseSubChunk(buf.data(), cut, sc, used));
        }
    }

    {
        ChunkCodec::CellIds c;
        c.fill(0);
        c[1 * 256 + 2 * 16 + 3] = 77;
        std::vector<uint8_t> b;
        ChunkCodec::WriteSubChunk(b, 0, c);
        SubChunkData sc;
        size_t used;
        ChunkCodec::ParseSubChunk(b.data(), b.size(), sc, used);
        ChunkCodec::CellIds back;
        ChunkCodec::Expand(sc.storages[0], back);
        CHECK(back[291] == 77 && back[0] == 0);
    }

    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, -3);
        wire::WriteZigzag32(b, 12);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 2);
        b.push_back(0);
        b.push_back(0);
        wire::WriteVarUint32(b, 0);
        std::vector<uint8_t> payload = {1, 2, 3};
        wire::WriteVarUint32(b, (uint32_t)payload.size());
        b.insert(b.end(), payload.begin(), payload.end());
        b.push_back(0);
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.x == -3 && p.z == 12 && p.dimension == 0 && p.subChunkCount == 2);
        CHECK(!p.highestSubchunkCount.has_value());
        CHECK(p.payload == payload && !p.cacheEnabled);
    }
    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, 5);
        wire::WriteZigzag32(b, -6);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 0);
        b.push_back(1);
        wire::WriteZigzag32(b, 9);
        b.push_back(0);
        wire::WriteVarUint32(b, 0);
        wire::WriteVarUint32(b, 0);
        b.push_back(0);
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.highestSubchunkCount.has_value() && *p.highestSubchunkCount == 9);
    }
    {
        std::vector<uint8_t> b;
        wire::WriteZigzag32(b, 0);
        wire::WriteZigzag32(b, 0);
        wire::WriteZigzag32(b, 0);
        wire::WriteVarUint32(b, 0);
        b.push_back(1);
        wire::WriteZigzag32(b, -1);
        b.push_back(1);
        wire::WriteVarUint32(b, 2);
        for (int i = 0; i < 4; i++)
            wire::WriteLE32(b, (uint32_t)(i + 1));
        wire::WriteVarUint32(b, 0);
        b.push_back(0);
        LevelChunkPacket p;
        CHECK(Decode(b.data(), b.size(), p));
        CHECK(p.highestSubchunkCount.has_value() && *p.highestSubchunkCount == -1 && p.cacheEnabled);
        CHECK(p.blobs.size() == 2 && p.blobs[0] == (1ull | (2ull << 32)));
    }

    {
        auto cells = MakeCells(12, 99);
        std::vector<uint8_t> sub;
        ChunkCodec::WriteSubChunk(sub, 3, cells);
        SubchunkPacket p;
        p.dimension = 0;
        p.origin = {4, 0, -2};
        SubChunkEntryEntry e0;
        e0.dx = 0;
        e0.dy = 5;
        e0.dz = 0;
        e0.result = SubChunkEntryEntryResult::Success;
        e0.payload = sub;
        e0.heightmapType = HeightMapDataType::HasData;
        std::array<std::vector<int8_t>, 16> hm;
        for (auto& row : hm)
            row.assign(16, 7);
        e0.heightmap = hm;
        e0.renderHeightmapType = HeightMapDataType::NoData;
        SubChunkEntryEntry e1;
        e1.dy = 6;
        e1.result = SubChunkEntryEntryResult::SuccessAllAir;
        e1.heightmapType = HeightMapDataType::NoData;
        e1.renderHeightmapType = HeightMapDataType::NoData;
        p.entries = {e0, e1};
        SubchunkPacket q;
        auto bytes = perlin::net::Encode(p);
        CHECK(Decode(bytes.data(), bytes.size(), q));
        CHECK(q.origin.x == 4 && q.origin.z == -2 && q.entries.size() == 2);
        CHECK(q.entries[0].dy == 5 && q.entries[0].result == SubChunkEntryEntryResult::Success);
        CHECK(q.entries[0].payload && *q.entries[0].payload == sub);
        CHECK(q.entries[0].heightmap && (*q.entries[0].heightmap)[3].size() == 16);
        CHECK(q.entries[1].result == SubChunkEntryEntryResult::SuccessAllAir);
    }
    {
        SubchunkRequestPacket req;
        req.dimension = 0;
        req.origin = {-3, 0, 7};
        req.requests.push_back({0, -4, 0});
        auto bytes = perlin::net::Encode(req);
        wire::Reader r(bytes.data(), bytes.size());
        int32_t d;
        uint32_t n, x, y, z;
        int8_t a, bb, c;
        CHECK(r.ReadZigzag32(d) && r.ReadVarUint32(n));
        CHECK(r.ReadInt8(a) && r.ReadInt8(bb) && r.ReadInt8(c));
        CHECK(r.ReadLE32(x) && r.ReadLE32(y) && r.ReadLE32(z));
        CHECK(d == 0 && n == 1 && a == 0 && bb == -4 && c == 0);
        CHECK((int32_t)x == -3 && y == 0 && z == 7);
        CHECK(r.Remaining() == 0);
    }
    printf(fails ? "%d FAILED\n" : "all chunk self-tests passed\n", fails);
    return fails ? 1 : 0;
}
