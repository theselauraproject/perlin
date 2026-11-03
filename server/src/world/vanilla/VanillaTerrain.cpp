#include "world/vanilla/VanillaTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>

#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <vector>

namespace perlin::server::vanilla {

    std::unique_ptr<VanillaCpuBackend> VanillaCpuBackend::Create(const ServerConfig& config,
                                                                 std::string& error) {
        std::unique_ptr<VanillaCpuBackend> b(new VanillaCpuBackend());
        b->pack = DataPack::Embedded();
        b->graph = std::make_unique<DensityGraph>(*b->pack, static_cast<int64_t>(config.seed));
        b->finalDensity = b->graph->Load("minecraft:overworld/final_density");
        if (!b->finalDensity) {
            error = "the built-in overworld data has no overworld/final_density (rebuild: "
                    "tools/gen_overworld_shape.py output is damaged)";
            return nullptr;
        }
        b->climate = std::make_unique<VanillaClimate>(*b->graph);
        if (!b->climate->Ok()) {
            error = "the built-in overworld data has no climate functions";
            return nullptr;
        }
        b->surface = std::make_unique<VanillaSurface>();
        b->features = std::make_unique<VanillaFeatures>(static_cast<int64_t>(config.seed));
        if (!std::getenv("PERLIN_NO_DECORATION"))
            b->decorator = std::make_unique<JavaDecorator>(static_cast<int64_t>(config.seed));
        return b;
    }

    void VanillaCpuBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        std::vector<float> density(static_cast<size_t>(16 * 16 * kHeight));
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            DfCacheScope cache;
            const int bx = positions[n].x * 16, bz = positions[n].z * 16;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < kHeight; y++)
                        density[static_cast<size_t>((x * 16 + z) * kHeight + y)] =
                            finalDensity->Eval(bx + x, kMinY + y, bz + z);
            std::array<uint8_t, 256> biomes;
            BiomesOf(density.data(), positions[n].x, positions[n].z, biomes.data());
            surface->Fill(density.data(), biomes.data(), out[n], features.get(), positions[n].x,
                          positions[n].z);
        }
    }
}

namespace perlin::server::vanilla {
    void VanillaCpuBackend::BiomesOf(const float* density, int chunkX, int chunkZ, uint8_t* biomes) const {
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const int top = VanillaSurface::TopOf(density + static_cast<size_t>((x * 16 + z) * kHeight));
                const BiomeSite site = SiteOf(chunkX * 16 + x, chunkZ * 16 + z, top);
                int32_t target[6];
                climate->Sample(site.x, site.y, site.z, target);
                biomes[x * 16 + z] = static_cast<uint8_t>(FindBiome(target));
            }
    }
}

namespace perlin::server::vanilla {
    bool VanillaCpuBackend::FindSpawn(int& x, int& y, int& z) {
        DfCacheScope cache;
        // the first block of solid ground at or above sea level in a column, found by a coarse walk down and a fine one at the hit; -1 when the column is under water
        const auto groundAt = [&](int wx, int wz) {
            for (int wy = kMinY + kHeight - 1; wy > kSeaLevel; wy -= 4) {
                if (finalDensity->Eval(wx, wy, wz) > 0.0f) {
                    for (int fine = std::min(wy + 3, kMinY + kHeight - 1); fine >= wy; fine--)
                        if (finalDensity->Eval(wx, fine, wz) > 0.0f)
                            return fine;
                    return wy;
                }
            }
            return -1;
        };
        // rings of columns 16 blocks apart around the origin, nearest first
        for (int r = 0; r <= 160; r++) {
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r)
                        continue;
                    const int wx = dx * 16, wz = dz * 16;
                    const int ground = groundAt(wx, wz);
                    if (ground > kSeaLevel) {
                        x = wx;
                        z = wz;
                        y = ground + 1;
                        return true;
                    }
                }
        }
        // all ocean for 2500 blocks: stand on the sea
        x = 0;
        z = 0;
        y = kSeaLevel + 1;
        return true;
    }
}

namespace perlin::server::vanilla {
    namespace {
        // the heights a biome's climate is sampled at while searching: surface biomes near sea level, mountain biomes up high, cave biomes down low.
        // a surface hit is then confirmed against the real column biome, so these only decide where to look
        std::vector<int> SearchHeights(const char* java) {
            const std::string name = java;
            if (name == "lush_caves" || name == "dripstone_caves" || name == "deep_dark" ||
                name == "sulfur_caves")
                return {-48, -16, 24};
            if (name == "jagged_peaks" || name == "frozen_peaks" || name == "stony_peaks" ||
                name == "snowy_slopes" || name == "grove" || name == "meadow" || name == "cherry_grove" ||
                name == "windswept_hills" || name == "windswept_gravelly_hills" || name == "windswept_forest")
                return {64, 128, 192, 256};
            return {64};
        }
        bool IsCaveBiome(const char* java) {
            const std::string name = java;
            return name == "lush_caves" || name == "dripstone_caves" || name == "deep_dark" ||
                   name == "sulfur_caves";
        }
    }

    int VanillaCpuBackend::ColumnBiome(int x, int z, int* groundY) const {
        DfCacheScope cache;
        int top = -1;
        // coarse walk down, then a fine one at the hit
        for (int wy = kMinY + kHeight - 1; wy >= kMinY && top < 0; wy -= 4) {
            if (finalDensity->Eval(x, wy, z) > 0.0f) {
                for (int fine = std::min(wy + 3, kMinY + kHeight - 1); fine >= wy; fine--)
                    if (finalDensity->Eval(x, fine, z) > 0.0f) {
                        top = fine - kMinY;
                        break;
                    }
                if (top < 0)
                    top = wy - kMinY;
            }
        }
        if (groundY)
            *groundY = kMinY + (top < 0 ? kSeaLevel - kMinY : top) + 1;
        const BiomeSite site = SiteOf(x, z, top);
        int32_t target[6];
        climate->Sample(site.x, site.y, site.z, target);
        return FindBiome(target);
    }

    bool VanillaCpuBackend::LocateBiome(int biome, int fromX, int fromZ, int radius,
                                        const std::atomic<bool>& cancel, int& outX, int& outY, int& outZ) {
        if (biome < 0 || biome >= kBiomeCount)
            return false;
        const std::vector<int> heights = SearchHeights(kBiomes[biome].java);
        const bool cave = IsCaveBiome(kBiomes[biome].java);
        constexpr int kStep = 32;
        const unsigned workers = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        for (int r = 0; r <= radius / kStep; r++) {
            if (cancel.load())
                return false;
            std::vector<std::pair<int, int>> points;
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++)
                    if (std::max(std::abs(dx), std::abs(dz)) == r)
                        points.emplace_back(fromX + dx * kStep, fromZ + dz * kStep);
            // the ring is split across the workers; each keeps its best (nearest) hit
            struct Hit {
                bool found = false;
                int x = 0, y = 0, z = 0;
                double distance = 0;
            };
            std::vector<Hit> hits(workers);
            std::vector<std::thread> threads;
            for (unsigned w = 0; w < workers; w++)
                threads.emplace_back([&, w] {
                    DfCacheScope cache;
                    for (size_t i = w; i < points.size() && !cancel.load(); i += workers) {
                        const auto [px, pz] = points[i];
                        for (int y : heights) {
                            int32_t target[6];
                            climate->Sample(px, y, pz, target);
                            if (FindBiome(target) != biome)
                                continue;
                            int hitY = y;
                            if (!cave) {
                                // confirm with the biome the world really has in this column, and report the ground there
                                int ground = y;
                                if (ColumnBiome(px, pz, &ground) != biome)
                                    continue;
                                hitY = ground;
                            }
                            const double d =
                                std::hypot(static_cast<double>(px - fromX), static_cast<double>(pz - fromZ));
                            if (!hits[w].found || d < hits[w].distance)
                                hits[w] = {true, px, hitY, pz, d};
                            break;
                        }
                    }
                });
            for (std::thread& t : threads)
                t.join();
            const Hit* best = nullptr;
            for (const Hit& h : hits)
                if (h.found && (!best || h.distance < best->distance))
                    best = &h;
            if (best) {
                outX = best->x;
                outY = best->y;
                outZ = best->z;
                return true;
            }
        }
        return false;
    }
}
