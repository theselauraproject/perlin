#include "client/player/PerlinCollisionCache.hpp"

#include <bit>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::physics::bds;

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    int cases = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        uint32_t oldCount, newCount, oldPreserve;
        if (!(row >> kind >> std::hex >> oldCount >> newCount >> oldPreserve) || kind != 'K' ||
            oldCount > 16 || newCount > 16 || oldPreserve > 1)
            return 1;
        auto readBox = [&](CollisionBox& box) {
            for (float& value : box) {
                uint32_t word;
                if (!(row >> word))
                    return false;
                value = std::bit_cast<float>(word);
            }
            return true;
        };
        auto readMetadata = [&](CachedBlockMetadata& metadata) {
            if (!(row >> metadata.block))
                return false;
            for (int32_t& value : metadata.position) {
                uint32_t word;
                if (!(row >> word))
                    return false;
                value = std::bit_cast<int32_t>(word);
            }
            return true;
        };
        CollisionBox region;
        if (!readBox(region))
            return 1;
        CollisionCache cache;
        cache.preserveHorizontalMotion = oldPreserve != 0;
        std::vector<CollisionRecord> records;
        for (uint32_t i = 0; i < oldCount + newCount; ++i) {
            CollisionRecord record;
            uint32_t preserve;
            if (!readBox(record.box) || !readMetadata(record.metadata) || !(row >> preserve) || preserve > 1)
                return 1;
            record.preserveHorizontalMotion = preserve != 0;
            if (i < oldCount) {
                cache.shapes.push_back(record.box);
                cache.blocks.push_back(record.metadata);
            } else {
                records.push_back(record);
            }
        }
        uint32_t expectedPreserve;
        if (!(row >> expectedPreserve) || expectedPreserve > 1)
            return 1;
        AppendCollisionRecords(cache, region, records);
        bool equal = cache.preserveHorizontalMotion == bool(expectedPreserve);
        for (int axis = 0; axis < 6; ++axis)
            equal &= std::bit_cast<uint32_t>(cache.region[axis]) == std::bit_cast<uint32_t>(region[axis]);
        for (uint32_t i = 0; i < oldCount + newCount; ++i) {
            CollisionBox expectedBox;
            CachedBlockMetadata expectedMetadata;
            if (!readBox(expectedBox) || !readMetadata(expectedMetadata))
                return 1;
            for (int axis = 0; axis < 6; ++axis)
                equal &= std::bit_cast<uint32_t>(cache.shapes[i][axis]) ==
                         std::bit_cast<uint32_t>(expectedBox[axis]);
            equal &= cache.blocks[i].block == expectedMetadata.block &&
                     cache.blocks[i].position == expectedMetadata.position;
        }
        std::string extra;
        if (row >> extra)
            return 1;
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Collision cache case %d differs from BDS\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 512)
        return 1;
    std::printf("BDS collision cache: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
