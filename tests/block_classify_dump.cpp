#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace perlin {
    struct BlockClassifyProbe {
        using Info = LevelCollision::Info;
        static Info Classify(uint16_t id) { return LevelCollision::ClassifyVanilla(id, nullptr); }
    };
}

using namespace perlin;

const BlockAppearance& BlockLibrary::Look(int) const {
    std::abort();
}

namespace {
    uint64_t Mix(uint64_t hash, uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
        return hash;
    }

    uint64_t MixBits(uint64_t hash, double value) {
        uint64_t bits;
        static_assert(sizeof bits == sizeof value);
        std::copy_n(reinterpret_cast<const char*>(&value), sizeof bits, reinterpret_cast<char*>(&bits));
        return Mix(hash, bits);
    }

    uint64_t HashInfo(uint64_t hash, const BlockClassifyProbe::Info& info) {
        const auto& s = info.shape;
        hash = Mix(hash, static_cast<uint64_t>(info.kind));
        hash = Mix(hash, info.full);
        hash = MixBits(hash, s.friction);
        for (const uint64_t flag : {static_cast<uint64_t>(s.surface), static_cast<uint64_t>(s.fluid),
                                    static_cast<uint64_t>(s.fluidDepth), static_cast<uint64_t>(s.climbable),
                                    static_cast<uint64_t>(s.web), static_cast<uint64_t>(s.powderSnow),
                                    static_cast<uint64_t>(s.berryBush), static_cast<uint64_t>(s.honey),
                                    static_cast<uint64_t>(s.isAir), static_cast<uint64_t>(s.portal),
                                    static_cast<uint64_t>(static_cast<int>(s.bubbleColumn) + 128),
                                    static_cast<uint64_t>(static_cast<int>(s.scaffoldingStability) + 128),
                                    s.jumpProperties, static_cast<uint64_t>(s.randomOffset.has_value())})
            hash = Mix(hash, flag);
        hash = MixBits(hash, s.fluidLevel);
        hash = MixBits(hash, s.speedFactor);
        hash = Mix(hash, s.boxes.size());
        for (const auto& b : s.boxes)
            for (const double v : {b.x0, b.y0, b.z0, b.x1, b.y1, b.z1})
                hash = MixBits(hash, v);
        return hash;
    }
}

int main(int argc, char** argv) {
    if (argc != 5)
        return 2;
    const std::filesystem::path states = argv[1];
    const std::filesystem::path resources = argv[2];
    setenv("PERLIN_RESOURCES", resources.string().c_str(), 1);
    setenv("PERLIN_DATA_DIR", argv[3], 1);
    std::ifstream file(states);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    auto& palette = BlockPalette::Instance();
    if (!palette.LoadServerStatesText(text, "block_states.json"))
        return 3;
    palette.SetSequential(true);
    uint64_t hash = 1469598103934665603ull;
    size_t count = 0;
    for (size_t r = 0; r < palette.StateCount(); r++) {
        const uint16_t id = palette.InternRuntime(static_cast<int32_t>(r));
        hash = HashInfo(Mix(hash, id), BlockClassifyProbe::Classify(id));
        count++;
    }
    char actual[64];
    std::snprintf(actual, sizeof actual, "%zu %016llx", count, static_cast<unsigned long long>(hash));
    std::ifstream goldenFile(argv[4]);
    std::string expected;
    std::getline(goldenFile, expected);
    if (expected != actual) {
        std::fprintf(stderr, "block classification changed: expected '%s', got '%s'\n", expected.c_str(), actual);
        return 1;
    }
    std::printf("block_classify %s\n", actual);
    return 0;
}
