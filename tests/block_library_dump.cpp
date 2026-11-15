#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "client/systems/blocks/BlockLibrary.hpp"

#include "client/systems/bindings/Keys.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/resources/TexturePack.hpp"

namespace perlin {
    std::string KeyName(int) {
        return {};
    }
    int KeyFromName(const std::string&) {
        return 0;
    }
    std::string MouseName(int) {
        return {};
    }
    int MouseFromName(const std::string&) {
        return 0;
    }
}
using namespace perlin;

namespace perlin {
    struct MesherTestAccess {
        static void Dump(BlockLibrary& lib, int attempts);
    };
}

static uint64_t Fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

void perlin::MesherTestAccess::Dump(BlockLibrary& lib, int attempts) {
    std::printf("attempts=%d ready=%d complete=%d nextTile=%d fetchFails=%d misses=%d stall=%d\n", attempts,
                lib.ready, lib.complete, lib.nextTile, lib.fetchFails, lib.lastFileMisses, lib.stallStreak);
    std::printf("atlas size=%zu hash=%016llx\n", lib.atlas.size(),
                static_cast<unsigned long long>(Fnv(lib.atlas.data(), lib.atlas.size())));
    std::map<std::string, const BlockAppearance*> sorted;
    for (const auto& [k, v] : lib.appearances)
        sorted[k] = &v;
    auto vec = [](const std::vector<int>& v) {
        std::string s;
        for (int i : v)
            s += std::to_string(i) + ",";
        return s;
    };
    for (const auto& [k, a] : sorted)
        std::printf("A %s %d %d %d v[%s|%s|%s] st[%d %d %d %d] of=%a ot=%d tint=%a %a %a %d%d%d c=%s u=%d "
                    "h=%d crgb=%a %a %a sh=%d co=%d tex=%s|%s|%s\n",
                    k.c_str(), a->top, a->side, a->bottom, vec(a->varTop).c_str(), vec(a->varSide).c_str(),
                    vec(a->varBottom).c_str(), a->sideTile[0], a->sideTile[1], a->sideTile[2], a->sideTile[3],
                    a->overlayFrac, a->overlayTile, a->tintR, a->tintG, a->tintB, a->tintTop, a->tintSide,
                    a->tintBottom, a->carriedTex.c_str(), a->useCarriedColor, a->hasCarriedRGB, a->carriedR,
                    a->carriedG, a->carriedB, static_cast<int>(a->shape), a->cutout, a->texTop.c_str(),
                    a->texSide.c_str(), a->texBottom.c_str());
    for (const auto& [k, v] : std::map<std::string, int>(lib.tileByPng.begin(), lib.tileByPng.end()))
        std::printf("P %s %d\n", k.c_str(), v);
    for (const auto& [k, v] : std::map<int, std::string>(lib.pathByTile.begin(), lib.pathByTile.end()))
        std::printf("T %d %s\n", k, v.c_str());
    for (const auto& [k, v] :
         std::map<int, BlockLibrary::TileAlpha>(lib.alphaByTile.begin(), lib.alphaByTile.end()))
        std::printf("L %d %d%d%d %a %a %a\n", k, v.holes, v.translucent, v.centerHollow, v.saturation,
                    v.frame, v.holeFrac);
    for (int t : std::set<int>(lib.overlayMaskTiles.begin(), lib.overlayMaskTiles.end()))
        std::printf("O %d\n", t);
    for (const auto& [k, v] :
         std::map<std::string, std::array<float, 3>>(lib.baseTints.begin(), lib.baseTints.end()))
        std::printf("B %s %a %a %a\n", k.c_str(), v[0], v[1], v[2]);
    for (int t : lib.synthTiles)
        std::printf("S %d\n", t);
    for (const auto& [k, v] : std::map<int, std::vector<int>>(lib.glassTiles.begin(), lib.glassTiles.end()))
        std::printf("G %d %s\n", k, vec(v).c_str());
    std::printf("glass built=%d lean=%d mode=%d\n", lib.glassBuilt, lib.glassLean, lib.glassBuiltMode);
    std::printf("models loaded=%d tilesHash=%016llx n=%zu\n", lib.models.Loaded(),
                static_cast<unsigned long long>(
                    Fnv(lib.models.tiles.data(), lib.models.tiles.size() * sizeof(lib.models.tiles[0]))),
                lib.models.tiles.size());
    std::printf("geometry size=%zu tried=%d\n", lib.geometry.blocks.size(), lib.geometryTried);
    for (const auto& [id, b] : std::map<std::string, BlockGeometry::Block>(lib.geometry.blocks.begin(),
                                                                           lib.geometry.blocks.end())) {
        std::printf("GB %s perms=%zu", id.c_str(), b.perms.size());
        for (const auto& [t, v] : std::map<std::string, int>(b.base.tiles.begin(), b.base.tiles.end()))
            std::printf(" %s=%d", t.c_str(), v);
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: block_library_dump <fixture root> <perlin dir> [pack]\n");
        return 2;
    }
    const std::string root = argv[1];
    BlockLibrary lib;
    if (argc > 3) {
        std::string err;
        const bool opened = TexturePack::Instance().Open(root + "/packs", "Fake", err);
        std::printf("pack opened=%d active=%d\n", opened, TexturePack::Instance().Active());
        lib.SetTexturePack(true);
    }
    int attempts = 0;
    while (attempts < 12 && !lib.EnsureLoaded(argv[2], root + "/vanilla/resource_pack"))
        attempts++;
    MesherTestAccess::Dump(lib, attempts);
    return 0;
}
