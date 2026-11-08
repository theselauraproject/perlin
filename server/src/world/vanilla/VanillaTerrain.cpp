#include "world/vanilla/VanillaTerrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>

#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/vanilla/BiomeZoom.hpp"

#include <openssl/evp.h>
#include <unordered_map>

#include <vector>

namespace perlin::server::vanilla {

    int64_t ObfuscateBiomeSeed(int64_t seed) {
        uint8_t in[8];
        for (int i = 0; i < 8; i++)
            in[i] = static_cast<uint8_t>(static_cast<uint64_t>(seed) >> (8 * i));
        uint8_t digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        EVP_Digest(in, sizeof(in), digest, &length, EVP_sha256(), nullptr);
        uint64_t out = 0;
        for (int i = 7; i >= 0; i--)
            out = (out << 8) | digest[i];
        return static_cast<int64_t>(out);
    }

    namespace {
        using OverworldFill = std::array<uint8_t, static_cast<size_t>(16 * ChunkColumn::kQuartLayers)>;
        struct FillCache {
            std::unordered_map<uint64_t, std::unique_ptr<OverworldFill>> map;
            uint64_t owner = 0;
        };
        FillCache& Fills(uint64_t owner) {
            thread_local FillCache cache;
            if (cache.owner != owner) {
                cache.map.clear();
                cache.owner = owner;
            }
            if (cache.map.size() >= 2048)
                cache.map.clear();
            return cache;
        }
        uint64_t FillKey(int chunkX, int chunkZ) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) | static_cast<uint32_t>(chunkZ);
        }
    }

    const uint8_t* VanillaCpuBackend::SearchFill(int chunkX, int chunkZ, const int32_t* targets) const {
        constexpr int layers = ChunkColumn::kQuartLayers;
        auto fill = std::make_unique<OverworldFill>();
        int hint = -1;
        for (int section = 0; section < layers / 4; section++)
            for (int qx = 0; qx < 4; qx++)
                for (int sy = 0; sy < 4; sy++)
                    for (int qz = 0; qz < 4; qz++) {
                        const int qy = section * 4 + sy;
                        const int32_t* t = targets + static_cast<size_t>((qx * 4 + qz) * layers + qy) * 6;
                        hint = FindBiomePoint(t, hint);
                        (*fill)[static_cast<size_t>(ChunkColumn::QuartIndex(qx, qy, qz))] = kBiomePointBiome[hint];
                    }
        FillCache& cache = Fills(instance);
        const uint8_t* out = fill->data();
        cache.map[FillKey(chunkX, chunkZ)] = std::move(fill);
        return out;
    }

    void VanillaCpuBackend::PrimeChunkFill(int chunkX, int chunkZ, const int32_t* targets) const {
        if (Fills(instance).map.contains(FillKey(chunkX, chunkZ)))
            return;
        SearchFill(chunkX, chunkZ, targets);
    }

    const uint8_t* VanillaCpuBackend::ChunkFill(int chunkX, int chunkZ) const {
        constexpr int layers = ChunkColumn::kQuartLayers;
        FillCache& cache = Fills(instance);
        if (const auto it = cache.map.find(FillKey(chunkX, chunkZ)); it != cache.map.end())
            return it->second->data();
        std::array<int, layers> ys{};
        for (int qy = 0; qy < layers; qy++)
            ys[static_cast<size_t>(qy)] = ((kMinY >> 2) + qy) * 4;
        std::vector<int32_t> targets(static_cast<size_t>(16 * layers) * 6);
        for (int qx = 0; qx < 4; qx++)
            for (int qz = 0; qz < 4; qz++)
                climate->SampleColumn((chunkX * 4 + qx) * 4, (chunkZ * 4 + qz) * 4, ys.data(), layers,
                                      targets.data() + static_cast<size_t>((qx * 4 + qz) * layers) * 6);
        return SearchFill(chunkX, chunkZ, targets.data());
    }

    int VanillaCpuBackend::NoiseBiome(int quartX, int quartY, int quartZ) const {
        const int qy = std::clamp(quartY - (kMinY >> 2), 0, ChunkColumn::kQuartLayers - 1);
        return ChunkFill(quartX >> 2, quartZ >> 2)[ChunkColumn::QuartIndex(quartX & 3, qy, quartZ & 3)];
    }

    int VanillaCpuBackend::BiomeAt(int x, int y, int z) const {
        const QuartPos3 q = ZoomedQuart(zoomSeed, x, y, z);
        return NoiseBiome(q.x, q.y, q.z);
    }

    void VanillaCpuBackend::QuartBiomes(int chunkX, int chunkZ, ChunkColumn& column) const {
        constexpr int layers = ChunkColumn::kQuartLayers;
        const uint8_t* fill = ChunkFill(chunkX, chunkZ);
        std::vector<uint8_t> network(static_cast<size_t>(16 * layers));
        for (size_t i = 0; i < network.size(); i++)
            network[i] = NetworkBiome(fill[i]);
        column.SetQuartBiomes(fill, network.data());
    }

    std::unique_ptr<VanillaCpuBackend> VanillaCpuBackend::Create(const ServerConfig& config,
                                                                 std::string& error) {
        std::unique_ptr<VanillaCpuBackend> b(new VanillaCpuBackend());
        b->instance = NextBackendInstance();
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
        if (!std::getenv("PERLIN_NO_AQUIFERS"))
            b->aquifers = AquiferFunctions::Load(*b->pack, *b->graph);
        b->surface = std::make_unique<VanillaSurface>();
        b->features = std::make_unique<VanillaFeatures>(static_cast<int64_t>(config.seed));
        if (!std::getenv("PERLIN_NO_DECORATION"))
            b->decorator = std::make_unique<JavaDecorator>(static_cast<int64_t>(config.seed));
        if (b->aquifers.ok && !std::getenv("PERLIN_NO_SURFACE_RULES"))
            b->materials = VanillaMaterials::Load(*b->pack, *b->graph, b->aquifers.seaLevel);
        if (b->materials && !std::getenv("PERLIN_NO_CARVERS"))
            b->carvers = VanillaCarvers::Load(*b->pack, static_cast<int64_t>(config.seed));
        b->zoomSeed = ObfuscateBiomeSeed(static_cast<int64_t>(config.seed));
        if (!std::getenv("PERLIN_NO_3D_BIOMES")) {
            const VanillaCpuBackend* self = b.get();
            b->stages.biome = [self](int x, int y, int z) { return self->BiomeAt(x, y, z); };
        }
        b->stages.aquifers = b->Aquifers();
        b->stages.materials = b->materials.get();
        b->stages.carvers = b->carvers.get();
        if (b->decorator) {
            const JavaDecorator* d = b->decorator.get();
            b->stages.temperature = [d](int biome, int x, int y, int z) { return d->Temperature(biome, x, y, z); };
        }
        return b;
    }

    void VanillaCpuBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        std::vector<float> density(static_cast<size_t>(16 * 16 * kHeight));
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            DfCacheScope cache(true);
            const int bx = positions[n].x * 16, bz = positions[n].z * 16;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < kHeight; y++)
                        density[static_cast<size_t>((x * 16 + z) * kHeight + y)] =
                            finalDensity->Eval(bx + x, kMinY + y, bz + z);
            std::array<uint8_t, 256> biomes;
            BiomesOf(density.data(), positions[n].x, positions[n].z, biomes.data());
            surface->Fill(density.data(), biomes.data(), out[n], Decorates() ? nullptr : features.get(), positions[n].x,
                          positions[n].z, Stages());
            if (stages.biome)
                QuartBiomes(positions[n].x, positions[n].z, out[n]);
        }
    }
}

namespace perlin::server::vanilla {
    void VanillaCpuBackend::BiomesOf(const float* density, int chunkX, int chunkZ, uint8_t* biomes) const {
        struct Seen {
            BiomeSite site;
            uint8_t biome;
        };
        std::vector<Seen> seen;
        seen.reserve(64);
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const int top = VanillaSurface::TopOf(density + static_cast<size_t>((x * 16 + z) * kHeight));
                const BiomeSite site = SiteOf(chunkX * 16 + x, chunkZ * 16 + z, top);
                const auto hit = std::find_if(seen.begin(), seen.end(), [&](const Seen& s) {
                    return s.site.x == site.x && s.site.y == site.y && s.site.z == site.z;
                });
                if (hit != seen.end()) {
                    biomes[x * 16 + z] = hit->biome;
                    continue;
                }
                int32_t target[6];
                climate->Sample(site.x, site.y, site.z, target);
                const uint8_t biome = static_cast<uint8_t>(FindBiome(target));
                seen.push_back({site, biome});
                biomes[x * 16 + z] = biome;
            }
    }
}

namespace perlin::server::vanilla {
    bool VanillaCpuBackend::FindSpawn(int& x, int& y, int& z) {
        DfCacheScope cache;

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

        x = 0;
        z = 0;
        y = kSeaLevel + 1;
        return true;
    }
}

namespace perlin::server::vanilla {
    namespace {

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
