#include "world/vanilla/VanillaMaterials.hpp"

#include "core/Json.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Chunk.hpp"
#include "world/vanilla/BiomeData.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_map>

namespace perlin::server::vanilla {

    struct VanillaMaterials::Cond {
        enum class Kind : uint8_t {
            Biome,
            Noise,
            Gradient,
            YAbove,
            Water,
            Temperature,
            Steep,
            Not,
            Hole,
            AbovePreliminary,
            StoneDepth,
            False
        };
        Kind kind = Kind::False;
        std::vector<uint8_t> biomes;
        const NormalNoise* noise = nullptr;
        double lo = 0, hi = 0;
        bool is3d = false;
        WorldRandom::Factory random;
        int a = 0, b = 0, c = 0;
        bool flag = false, ceiling = false;
        int inner = -1;
    };

    struct VanillaMaterials::Rule {
        enum class Kind : uint8_t { Block, Sequence, Condition, Bandlands, OreVein, Nothing };
        Kind kind = Kind::Nothing;
        uint16_t block = 0, ore = 0, raw = 0, filler = 0;
        float rawChance = 0;
        std::vector<int> children;
        int cond = -1;
        DfPtr density, richness, gap;
    };

    namespace {
        constexpr int kMaxY = kMinY + kHeight - 1;
        constexpr int kDepthLimit = 64;

        std::string Strip(const std::string& s) {
            return s.starts_with("minecraft:") ? s.substr(10) : s;
        }

        int FloorF(float v) {
            const int i = static_cast<int>(v);
            return v < static_cast<float>(i) ? i - 1 : i;
        }
        int FloorD(double v) {
            const int i = static_cast<int>(v);
            return v < static_cast<double>(i) ? i - 1 : i;
        }

        int Anchor(const json::Value& v, int bottom, int top) {
            if (v["absolute"].kind != json::Value::Kind::Null)
                return static_cast<int>(v["absolute"].Int());
            if (v["above_bottom"].kind != json::Value::Kind::Null)
                return bottom + static_cast<int>(v["above_bottom"].Int());
            if (v["below_top"].kind != json::Value::Kind::Null)
                return top - static_cast<int>(v["below_top"].Int());
            return 0;
        }

        int BiomeIndex(const std::string& java) {
            const std::string name = Strip(java);
            for (int i = 0; i < kBiomeCount; i++)
                if (name == kBiomes[i].java)
                    return i;
            return -1;
        }
    }

    VanillaMaterials::VanillaMaterials() = default;
    VanillaMaterials::~VanillaMaterials() = default;

    uint16_t VanillaMaterials::BlockOf(const std::string& javaName) {
        static const std::unordered_map<std::string, std::string> kRename = {
            {"terracotta", "hardened_clay"}, {"snow_block", "snow"}};
        std::string name = Strip(javaName);
        if (const auto it = kRename.find(name); it != kRename.end())
            name = it->second;
        if (name == "air")
            return 0;
        return BlockCatalog::Instance().DefaultBlock(name);
    }

    int VanillaMaterials::CondFrom(const json::Value& v, int depth) {
        Cond c;
        if (depth > kDepthLimit) {
            conds.push_back(c);
            return static_cast<int>(conds.size()) - 1;
        }
        if (v.IsString()) {
            json::Value file;
            if (json::Parse(pack->Read("worldgen/material_condition/" + Strip(v.text) + ".json"), file))
                return CondFrom(file, depth + 1);
            conds.push_back(c);
            return static_cast<int>(conds.size()) - 1;
        }
        using K = Cond::Kind;
        const std::string type = Strip(v["type"].String());
        if (type == "biome") {
            c.kind = K::Biome;
            const json::Value& list = v["biome_is"];
            const auto add = [&](const std::string& n) {
                if (const int i = BiomeIndex(n); i >= 0)
                    c.biomes.push_back(static_cast<uint8_t>(i));
            };
            if (list.IsString())
                add(list.text);
            else
                for (const json::Value& e : list.items)
                    add(e.text);
        } else if (type == "noise_threshold") {
            c.kind = K::Noise;
            c.noise = graph->Noise(v["noise"].String());
            c.lo = v["min_threshold"].Number();
            c.hi = v["max_threshold"].Number(1.7976931348623157e308);
            c.is3d = v["is_3d"].Bool();
        } else if (type == "vertical_gradient") {
            c.kind = K::Gradient;
            c.random = graph->Positional().FromHashOf(v["random_name"].String()).ForkPositional();
            c.a = Anchor(v["true_at_and_below"], genMinY, genMaxY);
            c.b = Anchor(v["false_at_and_above"], genMinY, genMaxY);
        } else if (type == "y_above") {
            c.kind = K::YAbove;
            c.a = Anchor(v["anchor"], genMinY, genMaxY);
            c.b = static_cast<int>(v["surface_depth_multiplier"].Int());
            c.flag = v["add_stone_depth"].Bool();
        } else if (type == "water") {
            c.kind = K::Water;
            c.a = static_cast<int>(v["offset"].Int());
            c.b = static_cast<int>(v["surface_depth_multiplier"].Int());
            c.flag = v["add_stone_depth"].Bool();
        } else if (type == "temperature") {
            c.kind = K::Temperature;
        } else if (type == "steep") {
            c.kind = K::Steep;
        } else if (type == "hole") {
            c.kind = K::Hole;
        } else if (type == "above_preliminary_surface") {
            c.kind = K::AbovePreliminary;
        } else if (type == "stone_depth") {
            c.kind = K::StoneDepth;
            c.a = static_cast<int>(v["offset"].Int());
            c.flag = v["add_surface_depth"].Bool();
            c.b = static_cast<int>(v["secondary_depth_range"].Int());
            c.ceiling = Strip(v["surface_type"].String()) == "ceiling";
        } else if (type == "not") {
            c.kind = K::Not;
            const int inner = CondFrom(v["invert"], depth + 1);
            c.inner = inner;
        }
        conds.push_back(std::move(c));
        return static_cast<int>(conds.size()) - 1;
    }

    int VanillaMaterials::RuleFrom(const json::Value& v, int depth) {
        Rule r;
        if (depth > kDepthLimit) {
            rules.push_back(r);
            return static_cast<int>(rules.size()) - 1;
        }
        if (v.IsString()) {
            json::Value file;
            if (json::Parse(pack->Read("worldgen/material_rule/" + Strip(v.text) + ".json"), file))
                return RuleFrom(file, depth + 1);
            rules.push_back(r);
            return static_cast<int>(rules.size()) - 1;
        }
        using K = Rule::Kind;
        const std::string type = Strip(v["type"].String());
        if (type == "block") {
            r.kind = K::Block;
            const json::Value& state = v["result_state"];
            r.block = BlockOf(state.IsString() ? state.text : state["Name"].String());
        } else if (type == "sequence") {
            r.kind = K::Sequence;
            for (const json::Value& e : v["sequence"].items) {
                const int child = RuleFrom(e, depth + 1);
                r.children.push_back(child);
            }
        } else if (type == "condition") {
            r.kind = K::Condition;
            r.cond = CondFrom(v["if_true"], depth + 1);
            const int then = RuleFrom(v["then_run"], depth + 1);
            r.children.push_back(then);
        } else if (type == "bandlands") {
            r.kind = K::Bandlands;
        } else if (type == "ore_vein") {
            r.kind = K::OreVein;
            r.ore = BlockOf(v["ore_block"].String());
            r.raw = BlockOf(v["raw_ore_block"].String());
            r.filler = BlockOf(v["filler_block"].String());
            r.rawChance = static_cast<float>(v["raw_ore_chance"].Number(0.02));
            r.density = graph->Inline(v["density"]);
            r.richness = graph->Inline(v["richness"]);
            r.gap = graph->Inline(v["filler_gap"]);
        }
        rules.push_back(std::move(r));
        return static_cast<int>(rules.size()) - 1;
    }

    std::unique_ptr<VanillaMaterials> VanillaMaterials::Load(const DataPack& pack, DensityGraph& graph, int seaLevel) {
        Options o;
        o.seaLevel = seaLevel;
        o.preliminary = graph.Load("minecraft:overworld/chunk_surface_level");
        return Load(pack, graph, o);
    }

    std::unique_ptr<VanillaMaterials> VanillaMaterials::Load(const DataPack& pack, DensityGraph& graph, const Options& o) {
        json::Value top;
        if (!json::Parse(pack.Read("worldgen/material_rule/" + o.rules + ".json"), top))
            return nullptr;
        std::unique_ptr<VanillaMaterials> m(new VanillaMaterials());
        m->pack = &pack;
        m->graph = &graph;
        m->seaLevel = o.seaLevel;
        m->genMinY = o.minY;
        m->genMaxY = o.minY + o.height - 1;
        m->root = m->RuleFrom(top, 0);
        m->preliminary = o.preliminary;
        m->surfaceNoise = graph.Noise("minecraft:surface");
        m->surfaceSecondary = graph.Noise("minecraft:surface_secondary");
        m->bandOffset = graph.Noise("minecraft:clay_bands_offset");
        m->badlandsPillar = graph.Noise("minecraft:badlands_pillar");
        m->badlandsPillarRoof = graph.Noise("minecraft:badlands_pillar_roof");
        m->badlandsSurface = graph.Noise("minecraft:badlands_surface");
        m->icebergPillar = graph.Noise("minecraft:iceberg_pillar");
        m->icebergPillarRoof = graph.Noise("minecraft:iceberg_pillar_roof");
        m->icebergSurface = graph.Noise("minecraft:iceberg_surface");
        if (!m->preliminary || !m->surfaceNoise || !m->surfaceSecondary || !m->bandOffset)
            return nullptr;
        m->noiseRandom = graph.Positional();
        m->oreRandom = graph.Positional().FromHashOf("minecraft:ore").ForkPositional();
        const BlockCatalog& c = BlockCatalog::Instance();
        m->stone = o.defaultBlock.empty() ? c.DefaultBlock("stone") : m->BlockOf(o.defaultBlock);
        m->water = c.DefaultBlock("water");
        m->lava = c.DefaultBlock("lava");
        m->packedIce = c.DefaultBlock("packed_ice");
        m->snowBlock = c.DefaultBlock("snow");
        m->erodedBadlands = BiomeIndex("eroded_badlands");
        m->frozenOcean = BiomeIndex("frozen_ocean");
        m->deepFrozenOcean = BiomeIndex("deep_frozen_ocean");

        const uint16_t terracotta = m->BlockOf("terracotta"), orange = m->BlockOf("orange_terracotta"),
                       yellow = m->BlockOf("yellow_terracotta"), brown = m->BlockOf("brown_terracotta"),
                       red = m->BlockOf("red_terracotta"), white = m->BlockOf("white_terracotta"),
                       lightGray = m->BlockOf("light_gray_terracotta");
        WorldRandom r = graph.Positional().FromHashOf("minecraft:clay_bands");
        auto& bands = m->bands;
        const int n = static_cast<int>(bands.size());
        bands.fill(terracotta);
        for (int i = 0; i < n; i++) {
            i += r.NextInt(5) + 1;
            if (i < n)
                bands[static_cast<size_t>(i)] = orange;
        }
        const auto makeBands = [&](int baseWidth, uint16_t state) {
            const int count = r.NextIntBetweenInclusive(6, 15);
            for (int i = 0; i < count; i++) {
                const int width = baseWidth + r.NextInt(3);
                const int start = r.NextInt(n);
                for (int p = 0; start + p < n && p < width; p++)
                    bands[static_cast<size_t>(start + p)] = state;
            }
        };
        makeBands(1, yellow);
        makeBands(2, brown);
        makeBands(1, red);
        const int whiteCount = r.NextIntBetweenInclusive(9, 15);
        for (int i = 0, start = 0; i < whiteCount && start < n; i++, start += r.NextInt(16) + 4) {
            bands[static_cast<size_t>(start)] = white;
            if (start - 1 > 0 && r.NextBool())
                bands[static_cast<size_t>(start - 1)] = lightGray;
            if (start + 1 < n && r.NextBool())
                bands[static_cast<size_t>(start + 1)] = lightGray;
        }
        return m;
    }

    struct MaterialColumn {
        const VanillaMaterials& m;
        const VanillaMaterials::Temperature& temperature;
        const VanillaMaterials::BiomeAt& biomeAt;
        int x = 0, z = 0, y = 0;
        int biome = 0;
        int biomeY = INT_MIN;
        int biomeHere = 0;

        int Biome() {
            if (!biomeAt)
                return biome;
            if (biomeY != y) {
                biomeHere = biomeAt(x, y, z);
                biomeY = y;
            }
            return biomeHere;
        }
        int gradientX = 0, gradientZ = 0;
        int surfaceDepth = 0;
        bool haveSecondary = false;
        double secondary = 0;
        bool haveMinSurface = false;
        int minSurface = 0;
        int stoneAbove = 0, stoneBelow = 0, waterHeight = INT_MIN;

        double Secondary() {
            if (!haveSecondary) {
                secondary = static_cast<double>(m.surfaceSecondary->Get(x, 0.0, z));
                haveSecondary = true;
            }
            return secondary;
        }
        int MinSurface() {
            if (!haveMinSurface) {
                minSurface = FloorF(m.preliminary->Eval(x, 0, z)) + surfaceDepth - 8;
                haveMinSurface = true;
            }
            return minSurface;
        }

        bool Test(int index) {
            using K = VanillaMaterials::Cond::Kind;
            const VanillaMaterials::Cond& c = m.conds[static_cast<size_t>(index)];
            switch (c.kind) {
            case K::Biome:
                return std::find(c.biomes.begin(), c.biomes.end(), static_cast<uint8_t>(Biome())) != c.biomes.end();
            case K::Noise: {
                const double v = static_cast<double>(c.is3d ? c.noise->Get(x, y, z) : c.noise->Get(x, 0.0, z));
                return v >= c.lo && v <= c.hi;
            }
            case K::Gradient: {
                if (y <= c.a)
                    return true;
                if (y >= c.b)
                    return false;
                const double probability = 1.0 + (static_cast<double>(y) - c.a) / (static_cast<double>(c.b) - c.a) * (0.0 - 1.0);
                WorldRandom r = c.random.At(x, y, z);
                return static_cast<double>(r.NextFloat()) < probability;
            }
            case K::YAbove:
                return y + (c.flag ? stoneAbove : 0) >= c.a + surfaceDepth * c.b;
            case K::Water:
                return waterHeight == INT_MIN || y + (c.flag ? stoneAbove : 0) >= waterHeight + c.a + surfaceDepth * c.b;
            case K::Temperature:
                return temperature && temperature(Biome(), x, y, z) < 0.15f;
            case K::Steep:
                return gradientX <= -4 || gradientZ >= 4;
            case K::Not:
                return c.inner >= 0 && !Test(c.inner);
            case K::Hole:
                return surfaceDepth <= 0;
            case K::AbovePreliminary:
                return y >= MinSurface();
            case K::StoneDepth: {
                const int depth = c.ceiling ? stoneBelow : stoneAbove;
                const int add = c.flag ? surfaceDepth : 0;
                const int secondaryDepth =
                    c.b == 0 ? 0 : static_cast<int>(0.0 + (Secondary() + 1.0) / 2.0 * static_cast<double>(c.b));
                return depth <= 1 + c.a + add + secondaryDepth;
            }
            case K::False:
                return false;
            }
            return false;
        }

        bool Apply(int index, uint16_t& out) {
            using K = VanillaMaterials::Rule::Kind;
            const VanillaMaterials::Rule& r = m.rules[static_cast<size_t>(index)];
            switch (r.kind) {
            case K::Block:
                out = r.block;
                return true;
            case K::Sequence:
                for (const int child : r.children)
                    if (Apply(child, out))
                        return true;
                return false;
            case K::Condition:
                return Test(r.cond) && Apply(r.children[0], out);
            case K::Bandlands: {
                const float offsetNoise = m.bandOffset->Get(x, 0.0, z) * 4.0f;
                const int offset = static_cast<int>(std::floor(offsetNoise + 0.5f));
                const int n = static_cast<int>(m.bands.size());
                out = m.bands[static_cast<size_t>(((y + offset + n) % n + n) % n)];
                return true;
            }
            case K::OreVein: {
                const float density = r.density->Eval(x, y, z);
                if (density <= 0.0f)
                    return false;
                WorldRandom random = m.oreRandom.At(x, y, z);
                if (random.NextFloat() > density)
                    return false;
                const float richness = r.richness->Eval(x, y, z);
                if (random.NextFloat() < richness && r.gap->Eval(x, y, z) < 0.0f) {
                    out = random.NextFloat() < r.rawChance ? r.raw : r.ore;
                    return true;
                }
                out = r.filler;
                return true;
            }
            case K::Nothing:
                return false;
            }
            return false;
        }
    };

    bool VanillaMaterials::TopMaterial(int chunkX, int chunkZ, int x, int z, int y, const uint8_t* biomes,
                                       const uint16_t* blocks, bool underFluid, const Temperature& temperature,
                                       const BiomeAt& biomeAt, uint16_t& out) const {
        const auto heightAt = [&](int hx, int hz) {
            const uint16_t* col = blocks + static_cast<size_t>((hx * 16 + hz) * kHeight);
            for (int i = kHeight - 1; i >= 0; i--)
                if (col[i] != 0)
                    return kMinY + i + 1;
            return kMinY;
        };
        MaterialColumn ctx{*this, temperature, biomeAt};
        ctx.x = chunkX * 16 + x;
        ctx.z = chunkZ * 16 + z;
        ctx.biome = biomes[x * 16 + z];
        ctx.gradientX = heightAt(std::min(x + 1, 15), z) - heightAt(std::max(x - 1, 0), z);
        ctx.gradientZ = heightAt(x, std::min(z + 1, 15)) - heightAt(x, std::max(z - 1, 0));
        WorldRandom depthRandom = noiseRandom.At(ctx.x, 0, ctx.z);
        ctx.surfaceDepth = static_cast<int>(static_cast<double>(surfaceNoise->Get(ctx.x, 0.0, ctx.z)) * 2.75 + 3.0 +
                                            depthRandom.NextDouble() * 0.25);
        ctx.y = y;
        ctx.stoneAbove = 1;
        ctx.stoneBelow = 1;
        ctx.waterHeight = underFluid ? y + 1 : INT_MIN;
        return ctx.Apply(root, out);
    }

    void VanillaMaterials::Apply(int chunkX, int chunkZ, const uint8_t* biomes, uint16_t* blocks,
                                 const Temperature& temperature, const BiomeAt& biomeAt) const {
        const auto at = [&](int x, int z) { return blocks + static_cast<size_t>((x * 16 + z) * kHeight); };
        const auto isFluid = [&](uint16_t b) { return b == water || b == lava; };
        std::array<int, 256> heights{};
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                const uint16_t* col = at(x, z);
                int h = kMinY;
                for (int i = kHeight - 1; i >= 0; i--)
                    if (col[i] != 0) {
                        h = kMinY + i + 1;
                        break;
                    }
                heights[static_cast<size_t>(x * 16 + z)] = h;
            }
        const auto height = [&](int x, int z) { return heights[static_cast<size_t>(x * 16 + z)]; };
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                uint16_t* col = at(x, z);
                const auto get = [&](int wy) -> uint16_t {
                    return wy < kMinY || wy > kMaxY ? static_cast<uint16_t>(0) : col[wy - kMinY];
                };
                const auto set = [&](int wy, uint16_t b) {
                    if (wy >= kMinY && wy <= kMaxY)
                        col[wy - kMinY] = b;
                };
                const int bx = chunkX * 16 + x, bz = chunkZ * 16 + z;
                const int starting = height(x, z);
                const int biome = biomeAt ? biomeAt(bx, starting, bz) : biomes[x * 16 + z];
                if (biome == erodedBadlands && badlandsSurface && badlandsPillar && badlandsPillarRoof) {
                    const double buffer = std::min(std::abs(static_cast<double>(badlandsSurface->Get(bx, 0.0, bz)) * 8.25),
                                                   static_cast<double>(badlandsPillar->Get(bx * 0.2, 0.0, bz * 0.2) * 15.0f));
                    if (buffer > 0.0) {
                        const double floorNoise =
                            std::abs(static_cast<double>(badlandsPillarRoof->Get(bx * 0.75, 0.0, bz * 0.75)) * 1.5);
                        const double top = 64.0 + std::min(buffer * buffer * 2.5, std::ceil(floorNoise * 50.0) + 24.0);
                        const int startY = FloorD(top);
                        if (starting <= startY) {
                            bool blocked = false;
                            int wy = startY;
                            for (; wy >= kMinY && get(wy) != stone; wy--)
                                if (get(wy) == water) {
                                    blocked = true;
                                    break;
                                }
                            if (!blocked) {
                                for (wy = startY; wy >= kMinY && get(wy) == 0; wy--)
                                    set(wy, stone);
                                for (int i = kHeight - 1; i >= 0; i--)
                                    if (col[i] != 0) {
                                        heights[static_cast<size_t>(x * 16 + z)] = kMinY + i + 1;
                                        break;
                                    }
                            }
                        }
                    }
                }
                MaterialColumn ctx{*this, temperature, biomeAt};
                ctx.x = bx;
                ctx.z = bz;
                ctx.biome = biome;
                ctx.gradientX = height(std::min(x + 1, 15), z) - height(std::max(x - 1, 0), z);
                ctx.gradientZ = height(x, std::min(z + 1, 15)) - height(x, std::max(z - 1, 0));
                WorldRandom depthRandom = noiseRandom.At(bx, 0, bz);
                ctx.surfaceDepth = static_cast<int>(static_cast<double>(surfaceNoise->Get(bx, 0.0, bz)) * 2.75 + 3.0 +
                                                    depthRandom.NextDouble() * 0.25);
                const int columnHeight = height(x, z);
                int stoneAbove = 0, waterHeight = INT_MIN, nextCeiling = INT_MAX;
                for (int wy = columnHeight; wy >= kMinY; wy--) {
                    const uint16_t old = get(wy);
                    if (old == 0) {
                        stoneAbove = 0;
                        waterHeight = INT_MIN;
                        continue;
                    }
                    if (isFluid(old)) {
                        if (waterHeight == INT_MIN)
                            waterHeight = wy + 1;
                        continue;
                    }
                    if (nextCeiling >= wy) {
                        nextCeiling = -32512;
                        for (int look = wy - 1; look >= kMinY - 1; look--) {
                            const uint16_t next = get(look);
                            if (next != 0 && !isFluid(next) && look >= kMinY)
                                continue;
                            nextCeiling = look + 1;
                            break;
                        }
                    }
                    ctx.y = wy;
                    ctx.stoneAbove = ++stoneAbove;
                    ctx.stoneBelow = wy - nextCeiling + 1;
                    ctx.waterHeight = waterHeight;
                    uint16_t result;
                    if (ctx.Apply(root, result))
                        set(wy, result);
                }
                if ((biome == frozenOcean || biome == deepFrozenOcean) && icebergSurface && icebergPillar &&
                    icebergPillarRoof) {
                    const double iceberg = std::min(std::abs(static_cast<double>(icebergSurface->Get(bx, 0.0, bz)) * 8.25),
                                                    static_cast<double>(icebergPillar->Get(bx * 1.28, 0.0, bz * 1.28) * 15.0f));
                    if (iceberg <= 1.8)
                        continue;
                    const double roof = std::abs(static_cast<double>(icebergPillarRoof->Get(bx * 1.17, 0.0, bz * 1.17)) * 1.5);
                    double top = std::min(iceberg * iceberg * 1.2, std::ceil(roof * 40.0) + 14.0);
                    if (temperature && temperature(biome, bx, seaLevel, bz) > 0.1f)
                        top -= 2.0;
                    if (top <= 2.0)
                        continue;
                    const double bottom = static_cast<double>(seaLevel) - top - 7.0;
                    top += static_cast<double>(seaLevel);
                    WorldRandom random = noiseRandom.At(bx, 0, bz);
                    const int maxSnowDepth = 2 + random.NextInt(4);
                    const int minSnowHeight = seaLevel + 18 + random.NextInt(10);
                    int snowDepth = 0;
                    const int minSurface = ctx.MinSurface();
                    for (int wy = std::max(starting, static_cast<int>(top) + 1); wy >= minSurface; wy--) {
                        const uint16_t b = get(wy);
                        const bool fillAir = b == 0 && wy < static_cast<int>(top) && random.NextDouble() > 0.01;
                        const bool fillWater = !fillAir && b == water && wy > static_cast<int>(bottom) && wy < seaLevel &&
                                               random.NextDouble() > 0.15;
                        if (!fillAir && !fillWater)
                            continue;
                        if (snowDepth <= maxSnowDepth && wy > minSnowHeight) {
                            set(wy, snowBlock);
                            snowDepth++;
                            continue;
                        }
                        set(wy, packedIce);
                    }
                }
            }
    }
}
