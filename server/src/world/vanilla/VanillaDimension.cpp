#include "world/vanilla/VanillaDimension.hpp"

#include "world/BlockCatalog.hpp"
#include "world/vanilla/BiomeData.hpp"
#include "world/vanilla/BiomeZoom.hpp"
#include "world/vanilla/VanillaSurface.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace perlin::server::vanilla {

    namespace {
        constexpr int kLevelHeight = 256;
        constexpr int kLevelQuarts = kLevelHeight / 4;
        constexpr int kStorageQuartOffset = -(kMinY >> 2);

        int BiomeIndexOf(const char* java) {
            for (int i = 0; i < kBiomeCount; i++)
                if (std::string(java) == kBiomes[i].java)
                    return i;
            return 0;
        }

        int32_t Quantize(float v) {
            const float scaled = v * 10000.0f;
            return scaled != scaled ? 0 : static_cast<int32_t>(std::clamp(scaled, -2.0e9f, 2.0e9f));
        }

        int SectionOf(int block) { return block >> 4; }
    }

    std::unique_ptr<VanillaDimensionBackend> VanillaDimensionBackend::Create(const ServerConfig& config, int dimension,
                                                                             std::string& error) {
        std::unique_ptr<VanillaDimensionBackend> b(new VanillaDimensionBackend());
        b->instance = NextBackendInstance();
        b->dimension = dimension;
        b->pack = DataPack::Embedded();
        const std::string name = dimension == kNether ? "nether" : "end";
        json::Value settings;
        if (!json::Parse(b->pack->Read("worldgen/noise_settings/" + name + ".json"), settings)) {
            error = "the built-in data has no noise_settings/" + name + ".json";
            return nullptr;
        }
        const int64_t seed = static_cast<int64_t>(config.seed);
        b->graph = std::make_unique<DensityGraph>(*b->pack, seed, settings["legacy_random_source"].Bool());
        const json::Value& router = settings["noise_router"];
        b->finalDensity = b->graph->Inline(router["final_density"]);
        b->temperature = b->graph->Inline(router["temperature"]);
        b->vegetation = b->graph->Inline(router["vegetation"]);
        b->erosion = b->graph->Inline(router["erosion"]);
        json::Value zero;
        zero.kind = json::Value::Kind::Number;
        zero.number = 0.0;
        b->zero = b->graph->Inline(zero);
        if (!b->finalDensity) {
            error = "noise_settings/" + name + ".json has no final_density";
            return nullptr;
        }
        b->seaLevel = static_cast<int>(settings["sea_level"].Int(dimension == kNether ? 32 : 0));
        b->genMinY = static_cast<int>(settings["noise"]["min_y"].Int(0));
        b->genHeight = static_cast<int>(settings["noise"]["height"].Int(128));
        const BlockCatalog& catalog = BlockCatalog::Instance();
        const std::string block = settings["default_block"]["Name"].String(settings["default_block"].String());
        const std::string fluid = settings["default_fluid"]["Name"].String(settings["default_fluid"].String());
        b->defaultBlock = catalog.DefaultBlock(block.starts_with("minecraft:") ? block.substr(10) : block);
        b->lava = catalog.DefaultBlock("lava");
        b->aquifers.disabled = true;
        b->aquifers.ok = true;
        b->aquifers.seaLevel = b->seaLevel;
        b->aquifers.defaultFluid = static_cast<uint8_t>(fluid.ends_with("lava")    ? VanillaAquifer::Substance::Lava
                                                        : fluid.ends_with("water") ? VanillaAquifer::Substance::Water
                                                                                   : VanillaAquifer::Substance::Air);
        VanillaMaterials::Options options;
        options.rules = name;
        options.seaLevel = b->seaLevel;
        options.minY = b->genMinY;
        options.height = b->genHeight;
        options.defaultBlock = block;
        options.preliminary = b->graph->Inline(router["chunk_surface_level"]);
        b->materials = VanillaMaterials::Load(*b->pack, *b->graph, options);
        if (dimension == kNether)
            b->carvers = VanillaCarvers::Load(*b->pack, seed, {"nether_cave"}, b->genMinY, b->genHeight);
        b->zoomSeed = ObfuscateBiomeSeed(seed);
        b->netherPoints = {{0, 0, 0, BiomeIndexOf("nether_wastes")},
                           {4000, 0, 0, BiomeIndexOf("crimson_forest")},
                           {0, -5000, 0, BiomeIndexOf("soul_sand_valley")},
                           {-5000, 0, 1750, BiomeIndexOf("basalt_deltas")},
                           {0, 5000, 3750, BiomeIndexOf("warped_forest")}};
        b->endCenter = BiomeIndexOf("the_end");
        b->endHighlands = BiomeIndexOf("end_highlands");
        b->endMidlands = BiomeIndexOf("end_midlands");
        b->endIslands = BiomeIndexOf("small_end_islands");
        b->endBarrens = BiomeIndexOf("end_barrens");
        return b;
    }

    int VanillaDimensionBackend::SearchNether(const int32_t target[2], int& hint) const {
        const auto distance = [&](const NetherPoint& p) {
            const int64_t dt = static_cast<int64_t>(target[0]) - p.temperature;
            const int64_t dh = static_cast<int64_t>(target[1]) - p.humidity;
            return dt * dt + dh * dh + static_cast<int64_t>(p.offset) * p.offset;
        };
        int best = hint;
        int64_t bestDistance = hint >= 0 ? distance(netherPoints[static_cast<size_t>(hint)]) : INT64_MAX;
        for (size_t i = 0; i < netherPoints.size(); i++) {
            const int64_t d = distance(netherPoints[i]);
            if (bestDistance > d) {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
        hint = best;
        return netherPoints[static_cast<size_t>(best)].biome;
    }

    int VanillaDimensionBackend::EndBiome(int quartX, int quartY, int quartZ) const {
        const int blockX = quartX * 4, blockY = quartY * 4, blockZ = quartZ * 4;
        const int64_t sx = SectionOf(blockX), sz = SectionOf(blockZ);
        if (sx * sx + sz * sz <= 4096)
            return endCenter;
        const int wx = (SectionOf(blockX) * 2 + 1) * 8, wz = (SectionOf(blockZ) * 2 + 1) * 8;
        const double h = static_cast<double>(erosion->Eval(wx, blockY, wz));
        if (h > 0.25)
            return endHighlands;
        if (h >= -0.0625)
            return endMidlands;
        if (h < -0.21875)
            return endIslands;
        return endBarrens;
    }

    const uint8_t* VanillaDimensionBackend::ChunkFill(int chunkX, int chunkZ, const int32_t* primed) const {
        constexpr int layers = ChunkColumn::kQuartLayers;
        using Fill = std::array<uint8_t, static_cast<size_t>(16 * layers)>;
        thread_local std::unordered_map<uint64_t, std::unique_ptr<Fill>> cache;
        thread_local uint64_t owner = 0;
        if (owner != instance) {
            cache.clear();
            owner = instance;
        }
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) | static_cast<uint32_t>(chunkZ);
        if (const auto it = cache.find(key); it != cache.end())
            return it->second->data();
        if (cache.size() >= 2048)
            cache.clear();
        auto fill = std::make_unique<Fill>();
        int hint = -1;
        (void)primed;
        for (int section = 0; section < kLevelQuarts / 4; section++)
            for (int qx = 0; qx < 4; qx++)
                for (int sy = 0; sy < 4; sy++)
                    for (int qz = 0; qz < 4; qz++) {
                        const int qy = section * 4 + sy;
                        const int wqx = chunkX * 4 + qx, wqz = chunkZ * 4 + qz;
                        int biome;
                        if (dimension == kNether) {
                            int32_t target[2];
                            if (primed) {
                                const int32_t* t = primed + static_cast<size_t>((qx * 4 + qz) * kLevelQuarts + qy) * 6;
                                target[0] = t[0];
                                target[1] = t[1];
                            } else {
                                target[0] = Quantize(temperature->Eval(wqx * 4, qy * 4, wqz * 4));
                                target[1] = Quantize(vegetation->Eval(wqx * 4, qy * 4, wqz * 4));
                            }
                            biome = SearchNether(target, hint);
                        } else {
                            biome = EndBiome(wqx, qy, wqz);
                        }
                        (*fill)[static_cast<size_t>(ChunkColumn::QuartIndex(qx, qy + kStorageQuartOffset, qz))] =
                            static_cast<uint8_t>(biome);
                    }
        for (int qy = 0; qy < layers; qy++) {
            const int source = std::clamp(qy, kStorageQuartOffset, kStorageQuartOffset + kLevelQuarts - 1);
            if (source == qy)
                continue;
            for (int qx = 0; qx < 4; qx++)
                for (int qz = 0; qz < 4; qz++)
                    (*fill)[static_cast<size_t>(ChunkColumn::QuartIndex(qx, qy, qz))] =
                        (*fill)[static_cast<size_t>(ChunkColumn::QuartIndex(qx, source, qz))];
        }
        const uint8_t* out = fill->data();
        cache.emplace(key, std::move(fill));
        return out;
    }

    int VanillaDimensionBackend::NoiseBiome(int quartX, int quartY, int quartZ) const {
        const int qy = std::clamp(quartY, 0, kLevelQuarts - 1) + kStorageQuartOffset;
        return ChunkFill(quartX >> 2, quartZ >> 2)[ChunkColumn::QuartIndex(quartX & 3, qy, quartZ & 3)];
    }

    int VanillaDimensionBackend::BiomeAt(int x, int y, int z) const {
        const QuartPos3 q = ZoomedQuart(zoomSeed, x, y, z);
        return NoiseBiome(q.x, q.y, q.z);
    }

    void VanillaDimensionBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        std::vector<float> density(static_cast<size_t>(256 * genHeight));
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            DfCacheScope cache(true);
            const int cx = positions[n].x, cz = positions[n].z;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < genHeight; y++)
                        density[static_cast<size_t>((x * 16 + z) * genHeight + y)] =
                            finalDensity->Eval(cx * 16 + x, genMinY + y, cz * 16 + z);
            Build(cx, cz, density.data(), out[n]);
        }
    }

    void VanillaDimensionBackend::PrimeChunkFill(int chunkX, int chunkZ, const int32_t* targets) const {
        if (dimension != kNether)
            return;
        ChunkFill(chunkX, chunkZ, targets);
    }

    void VanillaDimensionBackend::Build(int cx, int cz, const float* density, ChunkColumn& column) const {
        thread_local std::vector<uint16_t> blocks;
        blocks.assign(static_cast<size_t>(16 * 16 * kHeight), 0);
        const std::function<int(int, int, int)> biomeAt = [this](int x, int y, int z) { return BiomeAt(x, y, z); };
        {
            VanillaAquifer aquifer(aquifers, cx, cz, genMinY, genHeight);
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    uint16_t* col = blocks.data() + static_cast<size_t>((x * 16 + z) * kHeight);
                    for (int y = genMinY; y < genMinY + genHeight; y++) {
                        const float d = density[static_cast<size_t>((x * 16 + z) * genHeight + (y - genMinY))];
                        const VanillaAquifer::Substance s = aquifer.Compute(cx * 16 + x, y, cz * 16 + z, static_cast<double>(d));
                        col[y - kMinY] = s == VanillaAquifer::Substance::Solid ? defaultBlock
                                         : s == VanillaAquifer::Substance::Lava ? lava
                                                                                : static_cast<uint16_t>(0);
                    }
                }
            std::array<uint8_t, 256> biomes{};
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    biomes[static_cast<size_t>(x * 16 + z)] =
                        static_cast<uint8_t>(BiomeAt(cx * 16 + x, genMinY + genHeight / 2, cz * 16 + z));
            if (materials)
                materials->Apply(cx, cz, biomes.data(), blocks.data(), nullptr, biomeAt);
            if (carvers)
                carvers->Carve(cx, cz, blocks.data(), aquifer, materials.get(), biomes.data(), nullptr, biomeAt);
            std::array<uint16_t, kCells> cells;
            for (int sec = 0; sec < kSections; sec++) {
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++)
                        for (int y = 0; y < 16; y++)
                            cells[static_cast<size_t>(CellIndex(x, y, z))] =
                                blocks[static_cast<size_t>((x * 16 + z) * kHeight + sec * 16 + y)];
                column.SetSection(sec, cells.data());
            }
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    const uint8_t b = biomes[static_cast<size_t>(x * 16 + z)];
                    column.SetBiome(x, z, b, NetworkBiome(b));
                }
            const uint8_t* fill = ChunkFill(cx, cz);
            std::vector<uint8_t> network(static_cast<size_t>(16 * ChunkColumn::kQuartLayers));
            for (size_t i = 0; i < network.size(); i++)
                network[i] = NetworkBiome(fill[i]);
            column.SetQuartBiomes(fill, network.data());
        }
    }

    bool VanillaDimensionBackend::LocateBiome(int biome, int fromX, int fromZ, int radius, const std::atomic<bool>& cancel,
                                              int& x, int& y, int& z) {
        constexpr int kStep = 16;
        const int height = dimension == kNether ? 64 : 60;
        for (int r = 0; r <= radius / kStep; r++) {
            if (cancel.load())
                return false;
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r)
                        continue;
                    const int px = fromX + dx * kStep, pz = fromZ + dz * kStep;
                    if (BiomeAt(px, height, pz) == biome) {
                        x = px;
                        y = height;
                        z = pz;
                        return true;
                    }
                }
        }
        return false;
    }
}
