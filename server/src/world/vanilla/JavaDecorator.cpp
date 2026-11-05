#include "world/vanilla/JavaDecorator.hpp"
#include <atomic>

#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/vanilla/BiomeZoom.hpp"
#include "world/vanilla/BiomeData.hpp"
#include "world/vanilla/JavaRandom.hpp"

#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <optional>
#include <cstdio>
#include <cstdlib>

namespace perlin::server::vanilla {
    extern const size_t kJavaFeatureJsonSize;
    extern const size_t kJavaFeatureDataSize;
    extern const uint8_t kJavaFeatureData[];

    namespace {
        constexpr int kMaxY = kMinY + kHeight - 1;
        constexpr uint16_t kNone = 0xFFFF;

        struct Pos {
            int x = 0, y = 0, z = 0;
            Pos Off(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
            Pos Up(int n = 1) const { return {x, y + n, z}; }
            Pos Down(int n = 1) const { return {x, y - n, z}; }
            bool operator==(const Pos&) const = default;
        };

        constexpr int kDirX[4] = {0, 1, 0, -1};
        constexpr int kDirZ[4] = {-1, 0, 1, 0};

        class Rng {
        public:
            void SetSeed(int64_t seed) { x = WorldRandom::FromSeed(seed); }
            int64_t NextLong() { return static_cast<int64_t>(x.NextLong()); }
            int NextInt(int bound) { return bound <= 0 ? 0 : x.NextInt(bound); }
            int Between(int lo, int hi) { return hi < lo ? lo : lo + NextInt(hi - lo + 1); }
            float NextFloat() { return x.NextFloat(); }
            double NextDouble() { return x.NextDouble(); }
            bool NextBool() { return (x.NextLong() & 1) != 0; }
            double Gaussian() {
                if (haveSpare) {
                    haveSpare = false;
                    return spare;
                }
                double a, b, s;
                do {
                    a = 2 * NextDouble() - 1;
                    b = 2 * NextDouble() - 1;
                    s = a * a + b * b;
                } while (s >= 1 || s == 0);
                const double m = std::sqrt(-2 * std::log(s) / s);
                spare = b * m;
                haveSpare = true;
                return a * m;
            }

        private:
            WorldRandom x{1, 2};
            bool haveSpare = false;
            double spare = 0;
        };

        class LegacyRandom {
        public:
            explicit LegacyRandom(int64_t seed) : s((static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & kMask) {}
            int Next(int bits) {
                s = (s * 0x5DEECE66Dull + 0xBull) & kMask;
                return static_cast<int>(static_cast<int64_t>(s) >> (48 - bits));
            }
            int NextInt(int bound) {
                if ((bound & -bound) == bound)
                    return static_cast<int>((static_cast<int64_t>(bound) * Next(31)) >> 31);
                int bits, val;
                do {
                    bits = Next(31);
                    val = bits % bound;
                } while (bits - val + (bound - 1) < 0);
                return val;
            }
            double NextDouble() {
                return static_cast<double>((static_cast<int64_t>(Next(26)) << 27) + Next(27)) * 0x1.0p-53;
            }

        private:
            static constexpr uint64_t kMask = (1ull << 48) - 1;
            uint64_t s;
        };

        class Simplex {
        public:
            explicit Simplex(int64_t seed) {
                LegacyRandom r(seed);
                Init(r);
            }
            explicit Simplex(LegacyRandom& r) { Init(r); }
            void Init(LegacyRandom& r) {
                r.NextDouble();
                r.NextDouble();
                r.NextDouble();
                for (int i = 0; i < 256; i++)
                    p[i] = i;
                for (int i = 0; i < 256; i++) {
                    const int j = r.NextInt(256 - i);
                    std::swap(p[i], p[i + j]);
                }
            }
            double Get(double x, double y) const {
                static const double F2 = 0.5 * (std::sqrt(3.0) - 1.0);
                static const double G2 = (3.0 - std::sqrt(3.0)) / 6.0;
                static const int G[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                             {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
                                             {1, 1, 0},  {0, -1, 1},  {-1, 1, 0}, {0, -1, -1}};
                const double d0 = (x + y) * F2;
                const int i = static_cast<int>(std::floor(x + d0));
                const int j = static_cast<int>(std::floor(y + d0));
                const double d1 = (i + j) * G2;
                const double d4 = x - (i - d1), d5 = y - (j - d1);
                const int k = d4 > d5 ? 1 : 0, l = d4 > d5 ? 0 : 1;
                const double d6 = d4 - k + G2, d7 = d5 - l + G2, d8 = d4 - 1 + 2 * G2, d9 = d5 - 1 + 2 * G2;
                const int i1 = i & 255, j1 = j & 255;
                const auto P = [&](int v) { return p[v & 255]; };
                const int a = P(i1 + P(j1)) % 12, b = P(i1 + k + P(j1 + l)) % 12, c = P(i1 + 1 + P(j1 + 1)) % 12;
                const auto corner = [&](int g, double cx, double cy) {
                    double t = 0.5 - cx * cx - cy * cy;
                    if (t < 0)
                        return 0.0;
                    t *= t;
                    return t * t * (G[g][0] * cx + G[g][1] * cy);
                };
                return static_cast<float>(70.0 * (corner(a, d4, d5) + corner(b, d6, d7) + corner(c, d8, d9)));
            }

        private:
            int p[256];
        };

        class FrozenNoise {
        public:
            explicit FrozenNoise(int64_t seed) : r(seed), a(r), b(r), c(r) {}
            float Get(double x, double z) const {
                float v = 0.0f;
                v += 0.14285715f * static_cast<float>(a.Get(x, z));
                v += 0.2857143f * static_cast<float>(b.Get(x * 0.5, z * 0.5));
                v += 0.5714286f * static_cast<float>(c.Get(x * 0.25, z * 0.25));
                return v;
            }

        private:
            LegacyRandom r;
            Simplex a, b, c;
        };

        enum Flag : uint8_t {
            kAir = 1,
            kWater = 2,
            kLava = 4,
            kSolid = 8,
            kLeaves = 16,
            kLog = 32,
            kReplaceable = 64,
            kKnown = 128
        };

        bool Is(const json::Value& v, std::string_view type) {
            const std::string& t = v["type"].text;
            return t.size() == type.size() + 10 ? t.compare(10, std::string::npos, type) == 0 : t == type;
        }
    }

    struct JavaDecorator::Impl {
        json::Value doc;
        std::vector<JavaBiome> biomes;
        std::vector<int> tableToBiome;
        std::array<std::unordered_map<std::string, int>, 11> order;
        std::unordered_map<std::string, std::unordered_set<std::string>> tags;
        int64_t seed = 0;
        int64_t zoomSeed = 0;
        Simplex biomeInfo{2345};
        Simplex temperatureNoise{1234};
        FrozenNoise frozenNoise{3456};
        uint16_t water = 0, ice = 0, snow = 0, vine = 0, air = 0;
        mutable std::unique_ptr<std::atomic<uint8_t>[]> flags{new std::atomic<uint8_t>[65536]()};
        mutable std::vector<std::string> names;

        uint8_t Flags(uint16_t id) const {
            if (const uint8_t known = flags[id].load(std::memory_order_relaxed))
                return known;
            const std::string name = BlockRegistry::Instance().Name(id);
            uint8_t f = kKnown;
            const auto in = [&](const char* tag) {
                const auto it = tags.find(tag);
                return it != tags.end() && it->second.contains(name);
            };
            if (id == 0 || name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air")
                f |= kAir | kReplaceable;
            else if (name == "minecraft:water" || name == "minecraft:flowing_water")
                f |= kWater | kReplaceable;
            else if (name == "minecraft:lava" || name == "minecraft:flowing_lava")
                f |= kLava | kReplaceable;
            else {
                if (in("minecraft:leaves"))
                    f |= kLeaves;
                if (in("minecraft:logs") || in("minecraft:overworld_natural_logs"))
                    f |= kLog;
                const bool plant = in("minecraft:replaceable") || in("minecraft:flowers") || in("minecraft:saplings") ||
                                   in("minecraft:small_flowers") || in("minecraft:crops") ||
                                   in("minecraft:replaceable_by_trees") || name.contains("sapling") ||
                                   name.contains("mushroom") && !name.contains("block") && !name.contains("stem") ||
                                   name.contains("vine") || name.contains("bush") || name.contains("grass") &&
                                   !name.contains("block") || name.contains("fern") || name.contains("torch") ||
                                   name.contains("snow_layer") || name.contains("seagrass") || name.contains("kelp") ||
                                   name.contains("reeds") || name.contains("waterlily") || name.contains("leaf_litter") ||
                                   name.contains("petals") || name.contains("wildflowers") || name.contains("cocoa") ||
                                   name.contains("dead_bush") || name.contains("deadbush") || name.contains("coral_fan") ||
                                   name.contains("lichen") || name.contains("dripleaf") || name.contains("roots") &&
                                   !name.contains("mangrove_roots");
                if (in("minecraft:replaceable") || name == "minecraft:snow_layer")
                    f |= kReplaceable;
                if (!plant)
                    f |= kSolid;
            }
            flags[id].store(f, std::memory_order_relaxed);
            return f;
        }

        bool InTag(const std::string& tag, uint16_t id) const {
            const std::string t = tag.starts_with('#') ? tag.substr(1) : tag;
            const auto it = tags.find(t);
            if (it == tags.end())
                return false;
            return it->second.contains(BlockRegistry::Instance().Name(id));
        }
    };

    namespace {
        struct World {
            const JavaDecorator::Impl& d;
            int cx = 0, cz = 0;
            const ChunkColumn* const* region = nullptr;
            int originX = 0, originZ = 0;
            std::unordered_map<int64_t, uint16_t> overlay;
            std::vector<std::pair<int64_t, uint16_t>> writes;
            mutable std::array<int16_t, 9 * 256 * 3> heights;
            std::string topFeature;
            std::vector<Pos> logs, leaves;
            bool tracking = false;

            static int64_t Key(int x, int y, int z) {
                return (static_cast<int64_t>(x) & 0x3FFFFFF) << 38 | (static_cast<int64_t>(z) & 0x3FFFFFF) << 12 |
                       static_cast<int64_t>(y - kMinY);
            }
            const ChunkColumn* Column(int x, int z) const {
                const int dx = (x >> 4) - cx + 1, dz = (z >> 4) - cz + 1;
                if (dx < 0 || dx > 2 || dz < 0 || dz > 2)
                    return nullptr;
                return region[dz * 3 + dx];
            }
            uint16_t Raw(int x, int y, int z) const {
                if (y < kMinY || y > kMaxY)
                    return 0;
                const ChunkColumn* c = Column(x, z);
                return c ? c->Get(x & 15, y, z & 15) : 0;
            }
            uint16_t Get(const Pos& p) const {
                if (!overlay.empty()) {
                    const auto it = overlay.find(Key(p.x, p.y, p.z));
                    if (it != overlay.end())
                        return it->second;
                }
                return Raw(p.x, p.y, p.z);
            }
            void Set(const Pos& p, uint16_t id) {
                if (id == kNone || p.y < kMinY || p.y > kMaxY || !Column(p.x, p.z))
                    return;
                const int64_t k = Key(p.x, p.y, p.z);
                overlay[k] = id;
                writes.emplace_back(k, id);
                if (tracking) {
                    const uint8_t f = d.Flags(id);
                    if (f & kLog)
                        logs.push_back(p);
                    else if (f & kLeaves)
                        leaves.push_back(p);
                }
            }
            uint8_t F(const Pos& p) const { return d.Flags(Get(p)); }
            bool AirAt(const Pos& p) const { return (F(p) & kAir) != 0; }
            int BiomeIndex(int x, int z) const {
                const ChunkColumn* c = Column(x, z);
                return c ? c->GenBiome(x & 15, z & 15) : 0;
            }
            int BiomeIndex3(int x, int y, int z) const {
                const QuartPos3 q = ZoomedQuart(d.zoomSeed, x, y, z);
                const ChunkColumn* c = Column(q.x * 4, q.z * 4);
                if (!c)
                    return BiomeIndex(x, z);
                if (!c->HasQuartBiomes())
                    return c->GenBiome((q.x * 4) & 15, (q.z * 4) & 15);
                const int qy = std::clamp(q.y - (kMinY >> 2), 0, ChunkColumn::kQuartLayers - 1);
                return c->QuartBiome(q.x & 3, qy, q.z & 3);
            }
            const JavaBiome* BiomeAt3(int x, int y, int z) const {
                const int i = BiomeIndex3(x, y, z);
                return i >= 0 && i < static_cast<int>(d.tableToBiome.size()) && d.tableToBiome[static_cast<size_t>(i)] >= 0
                           ? &d.biomes[static_cast<size_t>(d.tableToBiome[static_cast<size_t>(i)])]
                           : nullptr;
            }
            const JavaBiome* BiomeAt(int x, int z) const {
                const int i = BiomeIndex(x, z);
                return i >= 0 && i < static_cast<int>(d.tableToBiome.size()) && d.tableToBiome[static_cast<size_t>(i)] >= 0
                           ? &d.biomes[static_cast<size_t>(d.tableToBiome[static_cast<size_t>(i)])]
                           : nullptr;
            }
            int Height(const std::string& type, int x, int z) const {
                const ChunkColumn* c = Column(x, z);
                if (!c)
                    return kMinY;
                int kind = 0;
                if (type.starts_with("OCEAN_FLOOR"))
                    kind = 1;
                else if (type.starts_with("MOTION_BLOCKING"))
                    kind = 2;
                const int dx = (x >> 4) - cx + 1, dz = (z >> 4) - cz + 1;
                const size_t slot = static_cast<size_t>(((dz * 3 + dx) * 256 + (x & 15) * 16 + (z & 15)) * 3 + kind);
                if (heights[slot] != INT16_MIN)
                    return heights[slot];
                int y = kMaxY;
                for (; y >= kMinY; y--) {
                    const uint8_t f = d.Flags(c->Get(x & 15, y, z & 15));
                    if (kind == 0 && !(f & kAir))
                        break;
                    if (kind == 1 && (f & kSolid))
                        break;
                    if (kind == 2 && ((f & kSolid) || (f & (kWater | kLava))))
                        break;
                }
                heights[slot] = static_cast<int16_t>(y + 1);
                return y + 1;
            }
        };

        uint16_t StateId(const json::Value& v) {
            if (v.IsObject() && v.members.contains("id") && v["id"].kind == json::Value::Kind::Number)
                return static_cast<uint16_t>(v["id"].Int());
            if (v.IsObject() && v["b"].IsString())
                return BlockCatalog::Instance().Resolve(v["b"].text);
            if (v.IsString())
                return BlockCatalog::Instance().DefaultBlock(v.text);
            return kNone;
        }

        std::string WithState(uint16_t id, const std::string& key, const std::string& value) {
            std::string text = BlockCatalog::Instance().StateText(id);
            const size_t at = text.find(key + "=");
            if (at == std::string::npos)
                return {};
            size_t end = text.find_first_of(",]", at);
            text.replace(at + key.size() + 1, end - (at + key.size() + 1), value);
            return text;
        }

        uint16_t Variant(uint16_t id, const std::string& key, const std::string& value) {
            const std::string t = WithState(id, key, value);
            return t.empty() ? id : BlockCatalog::Instance().Resolve(t);
        }

        class Engine {
        public:
            Engine(const JavaDecorator::Impl& d, World& w, Rng& r) : d(d), w(w), r(r) {}

            int SampleInt(const json::Value& v) {
                if (v.kind == json::Value::Kind::Number)
                    return static_cast<int>(v.Int());
                if (Is(v, "constant"))
                    return static_cast<int>(v["value"].Int());
                if (Is(v, "uniform") || !v.members.contains("type") && v.members.contains("min_inclusive"))
                    return r.Between(static_cast<int>(v["min_inclusive"].Int()), static_cast<int>(v["max_inclusive"].Int()));
                if (Is(v, "biased_to_bottom")) {
                    const int lo = static_cast<int>(v["min_inclusive"].Int()), hi = static_cast<int>(v["max_inclusive"].Int());
                    return lo + r.NextInt(r.NextInt(hi - lo + 1) + 1);
                }
                if (Is(v, "clamped"))
                    return std::clamp(SampleInt(v["source"]), static_cast<int>(v["min_inclusive"].Int()),
                                      static_cast<int>(v["max_inclusive"].Int()));
                if (Is(v, "clamped_normal")) {
                    const double n = v["mean"].Number() + r.Gaussian() * v["deviation"].Number();
                    return static_cast<int>(std::clamp(n, v["min_inclusive"].Number(v["min"].Number()),
                                                       v["max_inclusive"].Number(v["max"].Number())));
                }
                if (Is(v, "trapezoid")) {
                    const int lo = static_cast<int>(v["min"].Int()), hi = static_cast<int>(v["max"].Int());
                    const int plateau = static_cast<int>(v["plateau"].Int());
                    const int span = hi - lo, a = (span - plateau) / 2, b = span - a;
                    return lo + r.Between(0, b) + r.Between(0, a);
                }
                if (Is(v, "weighted_list")) {
                    const json::Value& list = v["distribution"];
                    int64_t total = 0;
                    for (const auto& e : list.items)
                        total += e["weight"].Int(1);
                    if (total <= 0)
                        return 0;
                    int64_t pick = r.NextInt(static_cast<int>(total));
                    for (const auto& e : list.items) {
                        pick -= e["weight"].Int(1);
                        if (pick < 0)
                            return SampleInt(e["data"]);
                    }
                    return 0;
                }
                return static_cast<int>(v["value"].Int(0));
            }

            float SampleFloat(const json::Value& v) {
                if (v.kind == json::Value::Kind::Number)
                    return static_cast<float>(v.Number());
                if (Is(v, "uniform")) {
                    const float lo = static_cast<float>(v["min_inclusive"].Number()),
                                hi = static_cast<float>(v["max_exclusive"].Number(v["max_inclusive"].Number()));
                    return lo + r.NextFloat() * (hi - lo);
                }
                if (Is(v, "clamped_normal"))
                    return static_cast<float>(std::clamp(v["mean"].Number() + r.Gaussian() * v["deviation"].Number(),
                                                         v["min"].Number(), v["max"].Number()));
                if (Is(v, "trapezoid")) {
                    const float lo = static_cast<float>(v["min"].Number()), hi = static_cast<float>(v["max"].Number());
                    const float plateau = static_cast<float>(v["plateau"].Number());
                    const float span = hi - lo, a = (span - plateau) / 2, b = span - a;
                    return lo + r.NextFloat() * b + r.NextFloat() * a;
                }
                return static_cast<float>(v["value"].Number(0));
            }

            int Anchor(const json::Value& v) {
                if (v.members.contains("absolute"))
                    return static_cast<int>(v["absolute"].Int());
                if (v.members.contains("above_bottom"))
                    return kMinY + static_cast<int>(v["above_bottom"].Int());
                if (v.members.contains("below_top"))
                    return kMaxY - static_cast<int>(v["below_top"].Int());
                return kMinY;
            }

            int SampleHeight(const json::Value& v) {
                if (!v.members.contains("type"))
                    return Anchor(v);
                if (Is(v, "constant"))
                    return Anchor(v["value"]);
                const int lo = Anchor(v["min_inclusive"]), hi = Anchor(v["max_inclusive"]);
                if (Is(v, "uniform"))
                    return r.Between(lo, hi);
                if (Is(v, "trapezoid")) {
                    const int plateau = static_cast<int>(v["plateau"].Int());
                    const int span = hi - lo;
                    if (plateau >= span)
                        return r.Between(lo, hi);
                    const int a = (span - plateau) / 2, b = span - a;
                    return lo + r.Between(0, b) + r.Between(0, a);
                }
                if (Is(v, "biased_to_bottom")) {
                    const int inner = static_cast<int>(v["inner"].Int(1));
                    return lo + r.NextInt(r.NextInt(std::max(1, hi - lo - inner + 1)) + inner);
                }
                if (Is(v, "very_biased_to_bottom")) {
                    const int inner = static_cast<int>(v["inner"].Int(1));
                    const int a = r.Between(lo + inner, hi);
                    const int b = r.Between(lo, a - 1);
                    return r.Between(lo, b - 1 + inner);
                }
                return lo;
            }

            Pos OffsetOf(const json::Value& v, const Pos& p) {
                if (v["offset"].IsArray() && v["offset"].Size() == 3)
                    return p.Off(static_cast<int>(v["offset"][0].Int()), static_cast<int>(v["offset"][1].Int()),
                                 static_cast<int>(v["offset"][2].Int()));
                return p;
            }

            bool NameMatches(const json::Value& list, uint16_t id) {
                const std::string name = BlockRegistry::Instance().Name(id);
                const auto one = [&](const json::Value& e) {
                    if (!e.IsString())
                        return false;
                    if (e.text.starts_with('#'))
                        return d.InTag(e.text, id);
                    return e.text == name;
                };
                if (list.IsArray())
                    return std::any_of(list.items.begin(), list.items.end(), one);
                return one(list);
            }

            bool Survives(uint16_t id, const Pos& p) {
                if (id == kNone)
                    return false;
                const std::string name = BlockRegistry::Instance().Name(id);
                const uint16_t below = w.Get(p.Down());
                const uint8_t bf = d.Flags(below);
                const auto belowIn = [&](const char* tag) { return d.InTag(tag, below); };
                const auto adjacentWater = [&](const Pos& q) {
                    for (int i = 0; i < 4; i++)
                        if (w.F(q.Off(kDirX[i], 0, kDirZ[i])) & kWater)
                            return true;
                    return false;
                };
                if (name == "minecraft:cactus") {
                    if (!(belowIn("minecraft:supports_cactus") || belowIn("minecraft:sand") ||
                          BlockRegistry::Instance().Name(below) == "minecraft:cactus"))
                        return false;
                    for (int i = 0; i < 4; i++)
                        if (w.F(p.Off(kDirX[i], 0, kDirZ[i])) & (kSolid | kLava))
                            return false;
                    return true;
                }
                if (name == "minecraft:reeds") {
                    if (BlockRegistry::Instance().Name(below) == "minecraft:reeds")
                        return true;
                    return (belowIn("minecraft:supports_sugar_cane") || belowIn("minecraft:sand")) && adjacentWater(p.Down());
                }
                if (name == "minecraft:waterlily")
                    return (bf & kWater) != 0 && (w.F(p) & kAir);
                if (name == "minecraft:kelp" || name.contains("seagrass") || name.contains("coral"))
                    return (w.F(p) & kWater) && (bf & kSolid);
                if (name == "minecraft:snow_layer")
                    return (bf & kSolid) && !BlockRegistry::Instance().Name(below).contains("ice");
                if (name.contains("deadbush") || name.contains("dry_grass") || name == "minecraft:cactus_flower")
                    return belowIn("minecraft:supports_dry_vegetation");
                if (name.contains("mushroom") && !name.contains("block") && !name.contains("stem"))
                    return (bf & kSolid) != 0;
                if (name == "minecraft:leaf_litter" || name.contains("lichen"))
                    return (bf & kSolid) != 0;
                if (name == "minecraft:bamboo" || name == "minecraft:bamboo_sapling")
                    return belowIn("minecraft:supports_bamboo");
                if (name.contains("sapling") || name.contains("flower") || name.contains("grass") && !name.contains("block") ||
                    name.contains("fern") || name.contains("bush") || name.contains("petals") || name.contains("wildflowers") ||
                    name.contains("tulip") || name.contains("orchid") || name.contains("allium") || name.contains("bluet") ||
                    name.contains("daisy") || name.contains("dandelion") || name.contains("poppy") || name.contains("lily") ||
                    name.contains("rose") || name.contains("lilac") || name.contains("peony") || name.contains("sunflower") ||
                    name.contains("pitcher") || name.contains("torchflower") || name.contains("eyeblossom") ||
                    d.InTag("minecraft:flowers", id) || d.InTag("minecraft:saplings", id))
                    return belowIn("minecraft:supports_vegetation");
                if (d.Flags(id) & kSolid)
                    return true;
                return (bf & kSolid) != 0;
            }

            bool Test(const json::Value& v, const Pos& p) {
                if (v.IsNull())
                    return true;
                const Pos q = OffsetOf(v, p);
                if (Is(v, "true"))
                    return true;
                if (Is(v, "not"))
                    return !Test(v["predicate"], p);
                if (Is(v, "all_of")) {
                    for (const auto& e : v["predicates"].items)
                        if (!Test(e, p))
                            return false;
                    return true;
                }
                if (Is(v, "any_of")) {
                    for (const auto& e : v["predicates"].items)
                        if (Test(e, p))
                            return true;
                    return false;
                }
                if (Is(v, "matching_blocks"))
                    return NameMatches(v["blocks"], w.Get(q));
                if (Is(v, "matching_block_tag"))
                    return d.InTag(v["tag"].text, w.Get(q));
                if (Is(v, "matching_fluids")) {
                    const uint8_t f = w.F(q);
                    const json::Value& fl = v["fluids"];
                    const auto one = [&](const json::Value& e) {
                        const std::string& t = e.text;
                        if (t.contains("water"))
                            return (f & kWater) != 0;
                        if (t.contains("lava"))
                            return (f & kLava) != 0;
                        return false;
                    };
                    if (fl.IsArray())
                        return std::any_of(fl.items.begin(), fl.items.end(), one);
                    return one(fl);
                }
                if (Is(v, "solid") || Is(v, "has_sturdy_face"))
                    return (w.F(q) & kSolid) != 0;
                if (Is(v, "replaceable"))
                    return (w.F(q) & kReplaceable) != 0;
                if (Is(v, "would_survive"))
                    return Survives(StateId(v["state"]), q);
                if (Is(v, "inside_world_bounds"))
                    return q.y >= kMinY && q.y <= kMaxY;
                if (Is(v, "unobstructed"))
                    return true;
                return false;
            }

            uint16_t Provide(const json::Value& v, const Pos& p) {
                if (v.IsString()) {
                    const auto& prov = d.doc["providers"];
                    if (prov.members.contains(v.text))
                        return Provide(prov[v.text], p);
                    return BlockCatalog::Instance().DefaultBlock(v.text);
                }
                if (!v.IsObject())
                    return kNone;
                if (v["b"].IsString())
                    return StateId(v);
                if (Is(v, "simple_state_provider"))
                    return Provide(v["state"], p);
                if (Is(v, "weighted") || Is(v, "weighted_state_provider")) {
                    const json::Value& list = v["entries"];
                    int64_t total = 0;
                    for (const auto& e : list.items)
                        total += e["weight"].Int(1);
                    if (total <= 0)
                        return kNone;
                    int64_t pick = r.NextInt(static_cast<int>(total));
                    for (const auto& e : list.items) {
                        pick -= e["weight"].Int(1);
                        if (pick < 0)
                            return Provide(e["data"], p);
                    }
                    return kNone;
                }
                if (Is(v, "rotated")) {
                    const uint16_t base = Provide(v["state"], p);
                    static const char* kAxes[3] = {"x", "y", "z"};
                    return Variant(base, "pillar_axis", kAxes[r.NextInt(3)]);
                }
                if (Is(v, "randomized_int"))
                    return Provide(v["source"], p);
                if (Is(v, "rule_based")) {
                    for (const auto& rule : v["rules"].items)
                        if (Test(rule["if_true"], p))
                            return Provide(rule["then"], p);
                    return v.members.contains("fallback") ? Provide(v["fallback"], p) : kNone;
                }
                if (Is(v, "noise") || Is(v, "dual_noise") || Is(v, "noise_threshold")) {
                    const double scale = v["scale"].Number(0.02);
                    const double n = std::clamp(
                        Simplex(v["seed"].Int(0)).Get(p.x * scale, p.z * scale) * 0.9, -0.999, 0.999);
                    if (Is(v, "noise_threshold")) {
                        if (n < v["threshold"].Number(0)) {
                            const auto& low = v["low_states"].items;
                            return low.empty() ? kNone : Provide(low[static_cast<size_t>(r.NextInt(static_cast<int>(low.size())))], p);
                        }
                        if (r.NextFloat() < v["high_chance"].Number(0)) {
                            const auto& high = v["high_states"].items;
                            return high.empty() ? kNone
                                                : Provide(high[static_cast<size_t>(r.NextInt(static_cast<int>(high.size())))], p);
                        }
                        return Provide(v["default_state"], p);
                    }
                    const auto& states = v["states"].items;
                    if (states.empty())
                        return kNone;
                    if (Is(v, "dual_noise"))
                        return Provide(states[static_cast<size_t>(r.NextInt(static_cast<int>(states.size())))], p);
                    const size_t i = static_cast<size_t>((n + 1) / 2 * static_cast<double>(states.size()));
                    return Provide(states[std::min(i, states.size() - 1)], p);
                }
                if (v.members.contains("state"))
                    return Provide(v["state"], p);
                return kNone;
            }

            bool ValidTree(const Pos& p) {
                const uint16_t id = w.Get(p);
                const uint8_t f = d.Flags(id);
                return (f & kAir) || d.InTag("minecraft:replaceable_by_trees", id) || (f & kReplaceable) && !(f & kWater);
            }
            bool FreeTree(const Pos& p) { return ValidTree(p) || (w.F(p) & kLog); }

            const json::Value* Configured(const json::Value& ref) {
                if (ref.IsString()) {
                    const auto& c = d.doc["configured"];
                    if (c.members.contains(ref.text))
                        return &c[ref.text];
                    return nullptr;
                }
                return &ref;
            }

            bool PlaceRef(const json::Value& ref, const Pos& p) {
                if (ref.IsString()) {
                    const auto& placed = d.doc["placed"];
                    if (placed.members.contains(ref.text))
                        return Placed(placed[ref.text], p);
                    if (const json::Value* c = Configured(ref))
                        return Feature(*c, p);
                    return false;
                }
                if (ref.IsObject() && ref.members.contains("feature") && !ref.members.contains("type"))
                    return Placed(ref, p);
                return Feature(ref, p);
            }

            bool Placed(const json::Value& placed, const Pos& origin) {
                std::vector<Pos> positions{origin};
                for (const auto& m : placed["placement"].items) {
                    std::vector<Pos> next;
                    for (const Pos& p : positions)
                        Modify(m, p, next);
                    positions.swap(next);
                    if (positions.empty())
                        return false;
                }
                bool any = false;
                const json::Value* feature = Configured(placed["feature"]);
                if (!feature)
                    return false;
                for (const Pos& p : positions)
                    any |= Feature(*feature, p);
                return any;
            }

            void Modify(const json::Value& m, const Pos& p, std::vector<Pos>& out) {
                if (Is(m, "count")) {
                    const int n = SampleInt(m["count"]);
                    for (int i = 0; i < n; i++)
                        out.push_back(p);
                } else if (Is(m, "in_square")) {
                    const int x = r.NextInt(16) + p.x;
                    const int z = r.NextInt(16) + p.z;
                    out.push_back({x, p.y, z});
                } else if (Is(m, "heightmap")) {
                    const int y = w.Height(m["heightmap"].text, p.x, p.z);
                    if (y > kMinY)
                        out.push_back({p.x, y, p.z});
                } else if (Is(m, "height_range")) {
                    out.push_back({p.x, SampleHeight(m["height"]), p.z});
                } else if (Is(m, "biome")) {
                    const JavaBiome* b = w.BiomeAt3(p.x, p.y, p.z);
                    if (b && b->placed.contains(w.topFeature))
                        out.push_back(p);
                } else if (Is(m, "rarity_filter")) {
                    if (r.NextFloat() < 1.0f / static_cast<float>(std::max<int64_t>(1, m["chance"].Int(1))))
                        out.push_back(p);
                } else if (Is(m, "surface_water_depth_filter")) {
                    const int floor = w.Height("OCEAN_FLOOR", p.x, p.z);
                    const int surface = w.Height("WORLD_SURFACE", p.x, p.z);
                    if (surface - floor <= m["max_water_depth"].Int())
                        out.push_back(p);
                } else if (Is(m, "block_predicate_filter")) {
                    if (Test(m["predicate"], p))
                        out.push_back(p);
                } else if (Is(m, "offset")) {
                    const int x = SampleInt(m["x"]);
                    const int y = SampleInt(m["y"]);
                    const int z = SampleInt(m["z"]);
                    out.push_back(p.Off(x, y, z));
                } else if (Is(m, "random_offset")) {
                    const int x = SampleInt(m["xz_spread"]);
                    const int y = SampleInt(m["y_spread"]);
                    const int z = SampleInt(m["xz_spread"]);
                    out.push_back(p.Off(x, y, z));
                } else if (Is(m, "surface_relative_threshold_filter")) {
                    const int64_t surface = w.Height(m["heightmap"].text, p.x, p.z);
                    if (surface + m["min_inclusive"].Int(INT32_MIN) <= p.y && p.y <= surface + m["max_inclusive"].Int(INT32_MAX))
                        out.push_back(p);
                } else if (Is(m, "environment_scan")) {
                    const int dir = m["direction_of_search"].text == "up" ? 1 : -1;
                    const int steps = static_cast<int>(m["max_steps"].Int());
                    const json::Value& allowed = m["allowed_search_condition"];
                    Pos q = p;
                    if (!allowed.IsNull() && !Test(allowed, q))
                        return;
                    for (int i = 0; i < steps; i++) {
                        if (Test(m["target_condition"], q)) {
                            out.push_back(q);
                            return;
                        }
                        q.y += dir;
                        if (q.y < kMinY || q.y > kMaxY)
                            return;
                        if (!allowed.IsNull() && !Test(allowed, q))
                            break;
                    }
                    if (Test(m["target_condition"], q))
                        out.push_back(q);
                } else if (Is(m, "noise_threshold_count")) {
                    const double n = d.biomeInfo.Get(p.x / 200.0, p.z / 200.0);
                    const int c = static_cast<int>(n < m["noise_level"].Number() ? m["below_noise"].Int() : m["above_noise"].Int());
                    for (int i = 0; i < c; i++)
                        out.push_back(p);
                } else if (Is(m, "noise_based_count")) {
                    const double f = m["noise_factor"].Number(1);
                    const double n = d.biomeInfo.Get(p.x / f, p.z / f);
                    const int c = static_cast<int>(std::ceil((n + m["noise_offset"].Number(0)) * m["noise_to_count_ratio"].Number()));
                    for (int i = 0; i < c; i++)
                        out.push_back(p);
                } else if (Is(m, "surface_relative_threshold_filter")) {
                    const int h = w.Height(m["heightmap"].text, p.x, p.z);
                    const int64_t lo = m.members.contains("min_inclusive") ? h + m["min_inclusive"].Int() : INT32_MIN;
                    const int64_t hi = m.members.contains("max_inclusive") ? h + m["max_inclusive"].Int() : INT32_MAX;
                    if (p.y >= lo && p.y <= hi)
                        out.push_back(p);
                } else if (Is(m, "fixed_placement")) {
                    for (const auto& e : m["positions"].items)
                        out.push_back({static_cast<int>(e[0].Int()), static_cast<int>(e[1].Int()), static_cast<int>(e[2].Int())});
                }
            }

            void PlaceState(const Pos& p, uint16_t id) {
                if (id == kNone)
                    return;
                const std::string upper = WithState(id, "upper_block_bit", "true");
                if (!upper.empty() && !BlockCatalog::Instance().StateText(id).contains("upper_block_bit=true")) {
                    if (!(w.F(p.Up()) & kAir))
                        return;
                    w.Set(p, id);
                    w.Set(p.Up(), BlockCatalog::Instance().Resolve(upper));
                    return;
                }
                w.Set(p, id);
            }

            bool RuleTest(const json::Value& t, uint16_t id, const Pos& p) {
                const std::string type = t["predicate_type"].String();
                const std::string_view name = std::string_view(type).substr(type.starts_with("minecraft:") ? 10 : 0);
                if (name == "always_true")
                    return true;
                if (name == "tag_match")
                    return d.InTag(t["tag"].String(), id);
                if (name == "block_match")
                    return id == StateId(t["block"]) ||
                           BlockRegistry::Instance().Name(id) == BlockRegistry::Instance().Name(StateId(t["block"]));
                if (name == "blockstate_match")
                    return id == StateId(t["block_state"]);
                if (name == "random_block_match")
                    return BlockRegistry::Instance().Name(id) == BlockRegistry::Instance().Name(StateId(t["block"])) &&
                           r.NextFloat() < static_cast<float>(t["probability"].Number());
                if (name == "random_blockstate_match")
                    return id == StateId(t["block_state"]) && r.NextFloat() < static_cast<float>(t["probability"].Number());
                if (name == "height_match")
                    return t["min_inclusive"].Int(INT32_MIN) <= p.y && p.y <= t["max_inclusive"].Int(INT32_MAX);
                if (name == "not")
                    return !RuleTest(t["rule"], id, p);
                if (name == "all_of") {
                    for (const auto& e : t["rules"].items)
                        if (!RuleTest(e, id, p))
                            return false;
                    return true;
                }
                if (name == "any_of") {
                    for (const auto& e : t["rules"].items)
                        if (RuleTest(e, id, p))
                            return true;
                    return false;
                }
                return false;
            }

            bool CanPlaceOre(uint16_t id, const json::Value& target, float discard, const Pos& p) {
                if (!RuleTest(target["target"], id, p))
                    return false;
                if (discard <= 0.0f)
                    return true;
                if (discard < 1.0f && r.NextFloat() >= discard)
                    return true;
                for (const Pos& n : {p.Off(0, -1, 0), p.Off(0, 1, 0), p.Off(0, 0, -1), p.Off(0, 0, 1), p.Off(-1, 0, 0), p.Off(1, 0, 0)})
                    if (w.AirAt(n))
                        return false;
                return true;
            }

            static float TableSin(double v) {
                static const std::vector<float> table = [] {
                    std::vector<float> t(65536);
                    for (size_t i = 0; i < t.size(); i++)
                        t[i] = static_cast<float>(std::sin(static_cast<double>(i) / 10430.378350470453));
                    return t;
                }();
                return table[static_cast<size_t>(static_cast<int64_t>(v * 10430.378350470453) & 0xFFFF)];
            }

            static int CeilF(float v) {
                const int i = static_cast<int>(v);
                return v > static_cast<float>(i) ? i + 1 : i;
            }
            static int FloorD(double v) {
                const int i = static_cast<int>(v);
                return v < static_cast<double>(i) ? i - 1 : i;
            }

            bool Ore(const json::Value& c, const Pos& origin) {
                const int size = static_cast<int>(c["size"].Int());
                const float discard = static_cast<float>(c["discard_chance_on_air_exposure"].Number());
                const auto& targets = c["targets"].items;
                const float dir = r.NextFloat() * 3.14159265358979323846f;
                const float spread = static_cast<float>(size) / 8.0f;
                const int maxRadius = CeilF((static_cast<float>(size) / 16.0f * 2.0f + 1.0f) / 2.0f);
                const double x0 = origin.x + std::sin(static_cast<double>(dir)) * static_cast<double>(spread);
                const double x1 = origin.x - std::sin(static_cast<double>(dir)) * static_cast<double>(spread);
                const double z0 = origin.z + std::cos(static_cast<double>(dir)) * static_cast<double>(spread);
                const double z1 = origin.z - std::cos(static_cast<double>(dir)) * static_cast<double>(spread);
                const double y0 = origin.y + r.NextInt(3) - 2;
                const double y1 = origin.y + r.NextInt(3) - 2;
                const int xStart = origin.x - CeilF(spread) - maxRadius;
                const int yStart = origin.y - 2 - maxRadius;
                const int zStart = origin.z - CeilF(spread) - maxRadius;
                const int sizeXZ = 2 * (CeilF(spread) + maxRadius);
                const int sizeY = 2 * (2 + maxRadius);
                bool reachable = false;
                for (int xp = xStart; xp <= xStart + sizeXZ && !reachable; xp++)
                    for (int zp = zStart; zp <= zStart + sizeXZ; zp++)
                        if (yStart <= w.Height("OCEAN_FLOOR_WG", xp, zp)) {
                            reachable = true;
                            break;
                        }
                if (!reachable)
                    return false;
                std::vector<double> data(static_cast<size_t>(size) * 4);
                for (int i = 0; i < size; i++) {
                    const float step = static_cast<float>(i) / static_cast<float>(size);
                    const double t = static_cast<double>(step);
                    const double xx = x0 + t * (x1 - x0), yy = y0 + t * (y1 - y0), zz = z0 + t * (z1 - z0);
                    const double ss = r.NextDouble() * static_cast<double>(size) / 16.0;
                    const double rad = (static_cast<double>(TableSin(static_cast<double>(3.14159265358979323846f * step)) + 1.0f) * ss + 1.0) / 2.0;
                    data[static_cast<size_t>(i * 4)] = xx;
                    data[static_cast<size_t>(i * 4 + 1)] = yy;
                    data[static_cast<size_t>(i * 4 + 2)] = zz;
                    data[static_cast<size_t>(i * 4 + 3)] = rad;
                }
                for (int a = 0; a < size - 1; a++) {
                    if (data[static_cast<size_t>(a * 4 + 3)] <= 0.0)
                        continue;
                    for (int b = a + 1; b < size; b++) {
                        if (data[static_cast<size_t>(b * 4 + 3)] <= 0.0)
                            continue;
                        const double dr = data[static_cast<size_t>(a * 4 + 3)] - data[static_cast<size_t>(b * 4 + 3)];
                        const double dx = data[static_cast<size_t>(a * 4)] - data[static_cast<size_t>(b * 4)];
                        const double dy = data[static_cast<size_t>(a * 4 + 1)] - data[static_cast<size_t>(b * 4 + 1)];
                        const double dz = data[static_cast<size_t>(a * 4 + 2)] - data[static_cast<size_t>(b * 4 + 2)];
                        if (!(dr * dr > dx * dx + dy * dy + dz * dz))
                            continue;
                        if (dr > 0.0)
                            data[static_cast<size_t>(b * 4 + 3)] = -1.0;
                        else
                            data[static_cast<size_t>(a * 4 + 3)] = -1.0;
                    }
                }
                std::vector<uint8_t> tested(static_cast<size_t>(sizeXZ) * sizeY * sizeXZ + static_cast<size_t>(sizeXZ * sizeY + sizeXZ + 1) * 4, 0);
                int placed = 0;
                for (int i = 0; i < size; i++) {
                    const double rad = data[static_cast<size_t>(i * 4 + 3)];
                    if (rad < 0.0)
                        continue;
                    const double xx = data[static_cast<size_t>(i * 4)], yy = data[static_cast<size_t>(i * 4 + 1)],
                                 zz = data[static_cast<size_t>(i * 4 + 2)];
                    const int xMin = std::max(FloorD(xx - rad), xStart), yMin = std::max(FloorD(yy - rad), yStart),
                              zMin = std::max(FloorD(zz - rad), zStart);
                    const int xMax = std::max(FloorD(xx + rad), xMin), yMax = std::max(FloorD(yy + rad), yMin),
                              zMax = std::max(FloorD(zz + rad), zMin);
                    for (int x = xMin; x <= xMax; x++) {
                        const double xd = (static_cast<double>(x) + 0.5 - xx) / rad;
                        if (!(xd * xd < 1.0))
                            continue;
                        for (int y = yMin; y <= yMax; y++) {
                            const double yd = (static_cast<double>(y) + 0.5 - yy) / rad;
                            if (!(xd * xd + yd * yd < 1.0))
                                continue;
                            for (int z = zMin; z <= zMax; z++) {
                                const double zd = (static_cast<double>(z) + 0.5 - zz) / rad;
                                if (!(xd * xd + yd * yd + zd * zd < 1.0) || y < kMinY || y > kMaxY)
                                    continue;
                                const size_t bit = static_cast<size_t>(x - xStart + (y - yStart) * sizeXZ + (z - zStart) * sizeXZ * sizeY);
                                if (bit >= tested.size() || tested[bit])
                                    continue;
                                tested[bit] = 1;
                                const Pos q{x, y, z};
                                if (!w.Column(x, z))
                                    continue;
                                const uint16_t here = w.Get(q);
                                for (const auto& target : targets)
                                    if (CanPlaceOre(here, target, discard, q)) {
                                        w.Set(q, StateId(target["state"]));
                                        placed++;
                                        break;
                                    }
                            }
                        }
                    }
                }
                return placed > 0;
            }

            bool ScatteredOre(const json::Value& c, const Pos& origin) {
                const int size = static_cast<int>(c["size"].Int());
                const float discard = static_cast<float>(c["discard_chance_on_air_exposure"].Number());
                const int tries = r.NextInt(size + 1);
                const auto axis = [&](int maxDist) {
                    const float v = (r.NextFloat() - r.NextFloat()) * static_cast<float>(maxDist);
                    return static_cast<int>(std::floor(v + 0.5f));
                };
                for (int i = 0; i < tries; i++) {
                    const int m = std::min(i, 7);
                    const int dx = axis(m), dy = axis(m), dz = axis(m);
                    const Pos q = origin.Off(dx, dy, dz);
                    const uint16_t here = w.Get(q);
                    for (const auto& target : c["targets"].items)
                        if (CanPlaceOre(here, target, discard, q)) {
                            w.Set(q, StateId(target["state"]));
                            break;
                        }
                }
                return true;
            }

            bool UnderwaterMagma(const json::Value& c, const Pos& origin) {
                const int range = static_cast<int>(c["floor_search_range"].Int());
                const int radius = static_cast<int>(c["placement_radius_around_floor"].Int());
                const float chance = static_cast<float>(c["placement_probability_per_valid_position"].Number());
                if (!(w.F(origin) & kWater))
                    return false;
                int floor = INT32_MIN;
                for (int dy = 1; dy <= range; dy++) {
                    const Pos q = origin.Down(dy);
                    if (!(w.F(q) & kWater)) {
                        floor = q.y;
                        break;
                    }
                }
                if (floor == INT32_MIN)
                    return false;
                const uint16_t magma = BlockCatalog::Instance().DefaultBlock("magma");
                const auto covered = [&](const Pos& q) { return (w.F(q) & kSolid) != 0; };
                int placed = 0;
                for (int x = origin.x - radius; x <= origin.x + radius; x++)
                    for (int y = floor - radius; y <= floor + radius; y++)
                        for (int z = origin.z - radius; z <= origin.z + radius; z++) {
                            if (!(r.NextFloat() < chance))
                                continue;
                            const Pos q{x, y, z};
                            const uint8_t f = w.F(q);
                            if ((f & kWater) || (f & kAir) || !covered(q.Down()))
                                continue;
                            if (!covered(q.Off(1, 0, 0)) || !covered(q.Off(-1, 0, 0)) || !covered(q.Off(0, 0, 1)) || !covered(q.Off(0, 0, -1)))
                                continue;
                            w.Set(q, magma);
                            placed++;
                        }
                return placed > 0;
            }

            bool Feature(const json::Value& f, const Pos& p) {
                const json::Value& c = f.members.contains("config") ? f["config"] : f;
                if (Is(f, "tree"))
                    return Tree(c, p);
                if (Is(f, "ore"))
                    return Ore(c, p);
                if (Is(f, "scattered_ore"))
                    return ScatteredOre(c, p);
                if (Is(f, "underwater_magma"))
                    return UnderwaterMagma(c, p);
                if (Is(f, "simple_block")) {
                    const uint16_t id = Provide(c["to_place"], p);
                    if (id == kNone || !Survives(id, p))
                        return false;
                    PlaceState(p, id);
                    return true;
                }
                if (Is(f, "block_column"))
                    return Column(c, p);
                if (Is(f, "random_selector")) {
                    for (const auto& e : c["features"].items)
                        if (r.NextFloat() < e["chance"].Number())
                            return PlaceRef(e["feature"], p);
                    return PlaceRef(c["default"], p);
                }
                if (Is(f, "simple_random_selector")) {
                    const auto& list = c["features"].items;
                    if (list.empty())
                        return false;
                    return PlaceRef(list[static_cast<size_t>(r.NextInt(static_cast<int>(list.size())))], p);
                }
                if (Is(f, "weighted_random_selector")) {
                    const auto& list = c["features"].items;
                    int64_t total = 0;
                    for (const auto& e : list)
                        total += e["weight"].Int(1);
                    if (total <= 0)
                        return false;
                    int64_t pick = r.NextInt(static_cast<int>(total));
                    for (const auto& e : list) {
                        pick -= e["weight"].Int(1);
                        if (pick < 0)
                            return PlaceRef(e["data"], p);
                    }
                    return false;
                }
                if (Is(f, "random_boolean_selector"))
                    return PlaceRef(r.NextBool() ? c["feature_true"] : c["feature_false"], p);
                if (Is(f, "sequence")) {
                    bool any = false;
                    for (const auto& e : c["features"].items)
                        any |= PlaceRef(e, p);
                    return any;
                }
                if (Is(f, "block_blob"))
                    return Blob(c, p);
                if (Is(f, "disk"))
                    return Disk(c, p);
                if (Is(f, "fallen_tree"))
                    return FallenTree(c, p);
                if (Is(f, "huge_red_mushroom") || Is(f, "huge_brown_mushroom"))
                    return HugeMushroom(c, p, Is(f, "huge_red_mushroom"));
                if (Is(f, "bamboo"))
                    return Bamboo(c, p);
                if (Is(f, "vegetation_patch") || Is(f, "waterlogged_vegetation_patch"))
                    return VegetationPatch(c, p);
                return false;
            }

            bool Column(const json::Value& c, const Pos& origin) {
                const auto& layers = c["layers"].items;
                std::vector<int> heights(layers.size());
                int total = 0;
                for (size_t i = 0; i < layers.size(); i++) {
                    heights[i] = std::max(0, SampleInt(layers[i]["height"]));
                    total += heights[i];
                }
                if (total == 0)
                    return false;
                const int dir = c["direction"].text == "down" ? -1 : 1;
                const bool tip = c["prioritize_tip"].Bool();
                Pos probe = origin.Up(dir);
                for (int l = 0; l < total; l++) {
                    if (!Test(c["allowed_placement"], probe)) {
                        int left = total - l;
                        const int step = tip ? 1 : -1;
                        int k = tip ? 0 : static_cast<int>(heights.size()) - 1;
                        const int end = tip ? static_cast<int>(heights.size()) : -1;
                        for (int m = k; m != end && left > 0; m += step) {
                            const int take = std::min(heights[static_cast<size_t>(m)], left);
                            left -= take;
                            heights[static_cast<size_t>(m)] -= take;
                        }
                        break;
                    }
                    probe = probe.Up(dir);
                }
                Pos at = origin;
                for (size_t i = 0; i < layers.size(); i++)
                    for (int k = 0; k < heights[i]; k++) {
                        const uint16_t id = Provide(layers[i]["provider"], at);
                        if (k == 0 && i == 0 && !Survives(id, at) && !(d.Flags(id) & kSolid))
                            return false;
                        w.Set(at, id);
                        at = at.Up(dir);
                    }
                return true;
            }

            bool Blob(const json::Value& c, Pos p) {
                for (; p.y > kMinY + 3; p = p.Down())
                    if (!w.AirAt(p.Down()) && Test(c["can_place_on"], p.Down()))
                        break;
                if (p.y <= kMinY + 3)
                    return false;
                const uint16_t id = Provide(c["state"], p);
                for (int i = 0; i < 3; i++) {
                    const int j = r.NextInt(2), k = r.NextInt(2), l = r.NextInt(2);
                    const float f = static_cast<float>(j + k + l) * 0.333f + 0.5f;
                    for (int x = -j; x <= j; x++)
                        for (int y = -k; y <= k; y++)
                            for (int z = -l; z <= l; z++)
                                if (static_cast<float>(x * x + y * y + z * z) <= f * f)
                                    w.Set(p.Off(x, y, z), id);
                    p = p.Off(-1 + r.NextInt(2), -r.NextInt(2), -1 + r.NextInt(2));
                }
                return true;
            }

            bool Disk(const json::Value& c, const Pos& p) {
                const int radius = SampleInt(c["radius"]);
                const int half = static_cast<int>(c["half_height"].Int(1));
                bool any = false;
                for (int x = -radius; x <= radius; x++)
                    for (int z = -radius; z <= radius; z++) {
                        if (x * x + z * z > radius * radius)
                            continue;
                        for (int y = half; y >= -half; y--) {
                            const Pos q = p.Off(x, y, z);
                            if (Test(c["target"], q)) {
                                const uint16_t id = Provide(c["state_provider"], q);
                                if (id != kNone) {
                                    w.Set(q, id);
                                    any = true;
                                }
                            }
                        }
                    }
                return any;
            }

            bool FallenTree(const json::Value& c, const Pos& p) {
                if (!(w.F(p.Down()) & kSolid) || !(w.F(p) & kReplaceable))
                    return false;
                const uint16_t log = Provide(c["trunk_provider"], p);
                if (log == kNone)
                    return false;
                const int dir = r.NextInt(4);
                const int length = std::max(1, SampleInt(c["log_length"]) - 2);
                const Pos start = p.Off(kDirX[dir] * (2 + r.NextInt(2)), 0, kDirZ[dir] * (2 + r.NextInt(2)));
                w.Set(p, Variant(log, "pillar_axis", "y"));
                for (int i = 0; i < length; i++) {
                    const Pos q = start.Off(kDirX[dir] * i, 0, kDirZ[dir] * i);
                    if (!(w.F(q) & kReplaceable) || !(w.F(q.Down()) & kSolid))
                        return true;
                }
                const uint16_t lying = Variant(log, "pillar_axis", kDirX[dir] != 0 ? "x" : "z");
                for (int i = 0; i < length; i++) {
                    const Pos q = start.Off(kDirX[dir] * i, 0, kDirZ[dir] * i);
                    w.Set(q, lying);
                    for (const auto& deco : c["log_decorators"].items)
                        if (Is(deco, "attached_to_logs") && r.NextFloat() < deco["probability"].Number() &&
                            (w.F(q.Up()) & kAir))
                            w.Set(q.Up(), Provide(deco["block_provider"], q.Up()));
                }
                return true;
            }

            bool HugeMushroom(const json::Value& c, const Pos& p, bool red) {
                int height = r.NextInt(3) + 4;
                if (r.NextInt(12) == 0)
                    height *= 2;
                if (!Test(c["can_place_on"], p.Down()) && !(w.F(p.Down()) & kSolid))
                    return false;
                const int radius = static_cast<int>(c["foliage_radius"].Int(red ? 2 : 3));
                for (int y = 0; y <= height + 1; y++)
                    for (int x = -radius; x <= radius; x++)
                        for (int z = -radius; z <= radius; z++)
                            if (!(w.F(p.Off(x, y, z)) & (kAir | kLeaves | kReplaceable)))
                                return false;
                const uint16_t stem = Provide(c["stem_provider"], p);
                for (int y = 0; y < height; y++)
                    w.Set(p.Up(y), stem);
                const uint16_t cap = Provide(c["cap_provider"], p);
                if (red) {
                    for (int y = height - 3; y <= height; y++) {
                        const int rr = y < height ? radius : radius - 1;
                        for (int x = -rr; x <= rr; x++)
                            for (int z = -rr; z <= rr; z++) {
                                const bool edgeX = std::abs(x) == rr, edgeZ = std::abs(z) == rr;
                                if (y < height && !(edgeX || edgeZ))
                                    continue;
                                if (edgeX && edgeZ && y < height)
                                    continue;
                                w.Set(p.Off(x, y, z), cap);
                            }
                    }
                } else {
                    for (int x = -radius; x <= radius; x++)
                        for (int z = -radius; z <= radius; z++)
                            if (!(std::abs(x) == radius && std::abs(z) == radius))
                                w.Set(p.Off(x, height, z), cap);
                }
                return true;
            }

            bool Bamboo(const json::Value& c, const Pos& p) {
                if (!(w.F(p) & kAir))
                    return false;
                const uint16_t bamboo = BlockCatalog::Instance().DefaultBlock("bamboo");
                if (!Survives(bamboo, p))
                    return false;
                const int height = r.NextInt(12) + 5;
                if (r.NextFloat() < c["probability"].Number()) {
                    const int rad = r.NextInt(4) + 1;
                    const uint16_t podzol = BlockCatalog::Instance().DefaultBlock("podzol");
                    for (int x = -rad; x <= rad; x++)
                        for (int z = -rad; z <= rad; z++)
                            if (x * x + z * z <= rad * rad) {
                                const Pos g = {p.x + x, w.Height("WORLD_SURFACE", p.x + x, p.z + z) - 1, p.z + z};
                                if (d.InTag("minecraft:substrate_overworld", w.Get(g)))
                                    w.Set(g, podzol);
                            }
                }
                const uint16_t thick = Variant(bamboo, "bamboo_stalk_thickness", "thick");
                int placed = 0;
                for (int i = 0; i < height && (w.F(p.Up(i)) & kAir); i++) {
                    w.Set(p.Up(i), thick);
                    placed++;
                }
                if (placed >= 3) {
                    w.Set(p.Up(placed - 1), Variant(thick, "bamboo_leaf_size", "large_leaves"));
                    w.Set(p.Up(placed - 2), Variant(thick, "bamboo_leaf_size", "large_leaves"));
                    w.Set(p.Up(placed - 3), Variant(thick, "bamboo_leaf_size", "small_leaves"));
                }
                return true;
            }

            bool VegetationPatch(const json::Value& c, const Pos& p) {
                const int rx = SampleInt(c["xz_radius"]) + 1, rz = SampleInt(c["xz_radius"]) + 1;
                const int depth = SampleInt(c["depth"]);
                const int vrange = static_cast<int>(c["vertical_range"].Int(5));
                const bool floor = c["surface"].text != "ceiling";
                bool any = false;
                for (int x = -rx; x <= rx; x++)
                    for (int z = -rz; z <= rz; z++) {
                        const bool edgeX = std::abs(x) == rx, edgeZ = std::abs(z) == rz;
                        if (edgeX && edgeZ)
                            continue;
                        if ((edgeX || edgeZ) && r.NextFloat() > c["extra_edge_column_chance"].Number())
                            continue;
                        Pos q = p.Off(x, 0, z);
                        int k = 0;
                        while (k < vrange && (w.F(q) & kAir)) {
                            q = q.Up(floor ? -1 : 1);
                            k++;
                        }
                        if (!NameMatches(c["replaceable"], w.Get(q)) && !d.InTag(c["replaceable"].text, w.Get(q)))
                            continue;
                        if (!(w.F(q.Up(floor ? 1 : -1)) & kAir))
                            continue;
                        const uint16_t ground = Provide(c["ground_state"], q);
                        for (int dd = 0; dd < depth; dd++)
                            if (!(w.F(q.Up(floor ? -dd : dd)) & kAir))
                                w.Set(q.Up(floor ? -dd : dd), ground);
                        any = true;
                        if (r.NextFloat() < c["vegetation_chance"].Number())
                            PlaceRef(c["vegetation_feature"], q.Up(floor ? 1 : -1));
                    }
                return any;
            }

            struct Attachment {
                Pos pos;
                int radiusOffset = 0;
                bool doubleTrunk = false;
            };

            bool Tree(const json::Value& c, const Pos& pos) {
                const json::Value& tp = c["trunk_placer"];
                const json::Value& fp = c["foliage_placer"];
                const int height = static_cast<int>(tp["base_height"].Int()) + r.NextInt(static_cast<int>(tp["height_rand_a"].Int()) + 1) +
                                   r.NextInt(static_cast<int>(tp["height_rand_b"].Int()) + 1);
                int foliageHeight = 0;
                if (Is(fp, "spruce_foliage_placer"))
                    foliageHeight = std::max(4, height - SampleInt(fp["trunk_height"]));
                else if (Is(fp, "mega_pine_foliage_placer"))
                    foliageHeight = SampleInt(fp["crown_height"]);
                else if (Is(fp, "acacia_foliage_placer"))
                    foliageHeight = 0;
                else if (Is(fp, "dark_oak_foliage_placer"))
                    foliageHeight = 4;
                else if (Is(fp, "random_spread_foliage_placer"))
                    foliageHeight = SampleInt(fp["foliage_height"]);
                else
                    foliageHeight = SampleInt(fp["height"]);
                const int radius = SampleInt(fp["radius"]);
                if (pos.y < kMinY + 1 || pos.y + height + 1 > kMaxY + 1)
                    return false;
                const json::Value& size = c["minimum_size"];
                const bool three = Is(size, "three_layers_feature_size");
                const auto sizeAt = [&](int i) {
                    if (three) {
                        const int limit = static_cast<int>(size["limit"].Int(1)), upperLimit = static_cast<int>(size["upper_limit"].Int(1));
                        if (i < limit)
                            return static_cast<int>(size["lower_size"].Int(0));
                        if (i >= height - upperLimit)
                            return static_cast<int>(size["upper_size"].Int(1));
                        return static_cast<int>(size["middle_size"].Int(1));
                    }
                    return i < size["limit"].Int(1) ? static_cast<int>(size["lower_size"].Int(0))
                                                    : static_cast<int>(size["upper_size"].Int(1));
                };
                int free = height;
                for (int i = 0; i <= height + 1 && free == height; i++) {
                    const int s = sizeAt(i);
                    for (int x = -s; x <= s && free == height; x++)
                        for (int z = -s; z <= s; z++)
                            if (!FreeTree(pos.Off(x, i, z))) {
                                free = i - 2;
                                break;
                            }
                }
                const int64_t minClipped = size["min_clipped_height"].Int(-1);
                if (free < height && (minClipped < 0 || free < minClipped))
                    return false;
                if (!Survives(BlockCatalog::Instance().DefaultBlock("oak_sapling"), pos) &&
                    !(w.F(pos.Down()) & kSolid))
                    return false;
                w.logs.clear();
                w.leaves.clear();
                w.tracking = true;
                const std::vector<Attachment> attachments = Trunk(c, tp, free, pos);
                const int offset = SampleInt(fp["offset"]);
                for (const Attachment& a : attachments)
                    Foliage(c, fp, free, a, foliageHeight, radius, offset);
                for (const auto& deco : c["decorators"].items)
                    Decorator(deco, pos);
                w.tracking = false;
                return true;
            }

            void Below(const json::Value& c, const Pos& p) {
                const json::Value& prov = c.members.contains("below_trunk_provider") ? c["below_trunk_provider"] : c["dirt_provider"];
                if (prov.IsNull()) {
                    const uint16_t ground = w.Get(p);
                    if (d.InTag("minecraft:substrate_overworld", ground) && BlockRegistry::Instance().Name(ground) != "minecraft:dirt")
                        w.Set(p, BlockCatalog::Instance().DefaultBlock("dirt"));
                    return;
                }
                const uint16_t id = Provide(prov, p);
                if (id != kNone)
                    w.Set(p, id);
            }

            bool Log(const json::Value& c, const Pos& p, const char* axis = "y") {
                if (!ValidTree(p))
                    return false;
                uint16_t id = Provide(c["trunk_provider"], p);
                if (id == kNone)
                    return false;
                if (axis[0] != 'y')
                    id = Variant(id, "pillar_axis", axis);
                w.Set(p, id);
                return true;
            }

            bool Limb(const json::Value& c, const Pos& a, const Pos& b, bool place) {
                const int dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                const int steps = std::max({std::abs(dx), std::abs(dy), std::abs(dz)});
                if (steps == 0)
                    return place ? (Log(c, a), true) : FreeTree(a);
                const float fx = static_cast<float>(dx) / steps, fy = static_cast<float>(dy) / steps,
                            fz = static_cast<float>(dz) / steps;
                for (int j = 0; j <= steps; j++) {
                    const Pos q = a.Off(static_cast<int>(std::floor(0.5f + j * fx)), static_cast<int>(std::floor(0.5f + j * fy)),
                                        static_cast<int>(std::floor(0.5f + j * fz)));
                    if (place) {
                        const int ax = std::abs(q.x - a.x), az = std::abs(q.z - a.z);
                        const int m = std::max(ax, az);
                        Log(c, q, m > 0 ? (ax == m ? "x" : "z") : "y");
                    } else if (!FreeTree(q)) {
                        return false;
                    }
                }
                return true;
            }

            std::vector<Attachment> Trunk(const json::Value& c, const json::Value& tp, int height, const Pos& pos) {
                std::vector<Attachment> out;
                if (Is(tp, "giant_trunk_placer") || Is(tp, "mega_jungle_trunk_placer")) {
                    for (int x = 0; x < 2; x++)
                        for (int z = 0; z < 2; z++)
                            Below(c, pos.Off(x, -1, z));
                    for (int i = 0; i < height; i++)
                        for (int x = 0; x < 2; x++)
                            for (int z = 0; z < 2; z++)
                                Log(c, pos.Off(x, i, z));
                    out.push_back({pos.Up(height), 0, true});
                    if (Is(tp, "mega_jungle_trunk_placer"))
                        for (int i = height - 2 - r.NextInt(4); i > height / 2; i -= 2 + r.NextInt(4)) {
                            const float f = r.NextFloat() * 6.2831855f;
                            int j = 0, k = 0;
                            for (int l = 0; l < 5; l++) {
                                j = static_cast<int>(1.5f + std::cos(f) * l);
                                k = static_cast<int>(1.5f + std::sin(f) * l);
                                Log(c, pos.Off(j, i - 3 + l / 2, k));
                            }
                            out.push_back({pos.Off(j, i, k), -2, false});
                        }
                    return out;
                }
                if (Is(tp, "dark_oak_trunk_placer")) {
                    for (int x = 0; x < 2; x++)
                        for (int z = 0; z < 2; z++)
                            Below(c, pos.Off(x, -1, z));
                    const int dir = r.NextInt(4);
                    const int bend = height - r.NextInt(4);
                    int steps = 2 - r.NextInt(3);
                    int x = pos.x, z = pos.z;
                    const int top = pos.y + height - 1;
                    for (int i = 0; i < height; i++) {
                        if (i >= bend && steps > 0) {
                            x += kDirX[dir];
                            z += kDirZ[dir];
                            steps--;
                        }
                        const Pos q = {x, pos.y + i, z};
                        if (w.F(q) & (kAir | kLeaves)) {
                            Log(c, q);
                            Log(c, q.Off(1, 0, 0));
                            Log(c, q.Off(0, 0, 1));
                            Log(c, q.Off(1, 0, 1));
                        }
                    }
                    out.push_back({{x, top, z}, 0, true});
                    for (int a = -1; a <= 2; a++)
                        for (int b = -1; b <= 2; b++)
                            if ((a < 0 || a > 1 || b < 0 || b > 1) && r.NextInt(3) <= 0) {
                                const int len = r.NextInt(3) + 2;
                                for (int k = 0; k < len; k++)
                                    Log(c, {pos.x + a, top - k - 1, pos.z + b});
                                out.push_back({{pos.x + a, top, pos.z + b}, 0, false});
                            }
                    return out;
                }
                Below(c, pos.Down());
                if (Is(tp, "forking_trunk_placer")) {
                    const int dir = r.NextInt(4);
                    const int fork = height - r.NextInt(4) - 1;
                    int steps = 3 - r.NextInt(3);
                    int x = pos.x, z = pos.z;
                    std::optional<int> top;
                    for (int i = 0; i < height; i++) {
                        const int y = pos.y + i;
                        if (i >= fork && steps > 0) {
                            x += kDirX[dir];
                            z += kDirZ[dir];
                            steps--;
                        }
                        if (Log(c, {x, y, z}))
                            top = y + 1;
                    }
                    if (top)
                        out.push_back({{x, *top, z}, 1, false});
                    x = pos.x;
                    z = pos.z;
                    const int dir2 = r.NextInt(4);
                    if (dir2 != dir) {
                        const int start = fork - r.NextInt(2) - 1;
                        int k = 1 + r.NextInt(3);
                        top.reset();
                        for (int l = start; l < height && k > 0; l++, k--)
                            if (l >= 1) {
                                const int y = pos.y + l;
                                x += kDirX[dir2];
                                z += kDirZ[dir2];
                                if (Log(c, {x, y, z}))
                                    top = y + 1;
                            }
                        if (top)
                            out.push_back({{x, *top, z}, 0, false});
                    }
                    return out;
                }
                if (Is(tp, "fancy_trunk_placer")) {
                    const int i = height + 2;
                    const int j = static_cast<int>(std::floor(i * 0.618));
                    const int l = pos.y + j;
                    struct Coords {
                        Pos at;
                        int base;
                    };
                    std::vector<Coords> coords{{pos.Up(i - 5), l}};
                    const auto shape = [&](int layer) {
                        if (layer < i * 0.3f)
                            return -1.0f;
                        const float f = i / 2.0f, f1 = f - layer;
                        float f2 = std::sqrt(f * f - f1 * f1);
                        if (f1 == 0)
                            f2 = f;
                        else if (std::abs(f1) >= f)
                            return 0.0f;
                        return f2 * 0.5f;
                    };
                    for (int i1 = i - 5; i1 >= 0; i1--) {
                        const float f = shape(i1);
                        if (f < 0)
                            continue;
                        const double d1 = f * (r.NextFloat() + 0.328);
                        const double d2 = r.NextFloat() * 2.0 * 3.141592653589793;
                        const Pos p1 = pos.Off(static_cast<int>(std::floor(d1 * std::sin(d2) + 0.5)), i1 - 1,
                                               static_cast<int>(std::floor(d1 * std::cos(d2) + 0.5)));
                        if (!Limb(c, p1, p1.Up(5), false))
                            continue;
                        const int k1 = pos.x - p1.x, l1 = pos.z - p1.z;
                        const double d5 = p1.y - std::sqrt(static_cast<double>(k1 * k1 + l1 * l1)) * 0.381;
                        const int i2 = d5 > l ? l : static_cast<int>(d5);
                        const Pos p3 = {pos.x, i2, pos.z};
                        if (Limb(c, p3, p1, false))
                            coords.push_back({p1, p3.y});
                    }
                    Limb(c, pos, pos.Up(j), true);
                    for (const Coords& co : coords) {
                        const Pos base = {pos.x, co.base, pos.z};
                        if (!(base == co.at) && co.base - pos.y >= i * 0.2)
                            Limb(c, base, co.at, true);
                        out.push_back({co.at, 0, false});
                    }
                    return out;
                }
                if (Is(tp, "bending_trunk_placer")) {
                    const int dir = r.NextInt(4);
                    const int bendLength = SampleInt(tp["bend_length"]);
                    Pos q = pos;
                    for (int i = 0; i < height - bendLength; i++) {
                        Log(c, q);
                        q = q.Up();
                    }
                    for (int i = 0; i < bendLength; i++) {
                        q = q.Off(kDirX[dir], 0, kDirZ[dir]);
                        Log(c, q, kDirX[dir] ? "x" : "z");
                    }
                    out.push_back({q.Up(), 0, false});
                    return out;
                }
                for (int i = 0; i < height; i++)
                    Log(c, pos.Up(i));
                if (Is(tp, "cherry_trunk_placer")) {
                    const int branches = std::max(1, SampleInt(tp["branch_count"]));
                    const int startOff = SampleInt(tp["branch_start_offset_from_top"].members.contains("type")
                                                       ? tp["branch_start_offset_from_top"]
                                                       : tp["branch_start_offset_from_top"]);
                    for (int b = 0; b < branches; b++) {
                        const int dir = r.NextInt(4);
                        const int len = SampleInt(tp["branch_horizontal_length"]);
                        const int endOff = SampleInt(tp["branch_end_offset_from_top"]);
                        Pos q = pos.Up(std::max(1, height + std::min(startOff, -1)));
                        for (int k = 1; k <= len; k++) {
                            q = q.Off(kDirX[dir], k > len / 2 ? 1 : 0, kDirZ[dir]);
                            Log(c, q, kDirX[dir] ? "x" : "z");
                        }
                        out.push_back({q.Up(1 + std::max(0, endOff)), 0, false});
                    }
                    if (branches < 3)
                        out.push_back({pos.Up(height), 0, false});
                    return out;
                }
                if (Is(tp, "upwards_branching_trunk_placer")) {
                    const float chance = static_cast<float>(tp["place_branch_per_log_probability"].Number(0.5));
                    for (int i = 2; i < height - 1; i++)
                        if (r.NextFloat() < chance) {
                            const int dir = r.NextInt(4);
                            Pos q = pos.Up(i);
                            const int steps = SampleInt(tp["extra_branch_steps"]);
                            for (int s = 0; s < steps; s++) {
                                q = q.Off(kDirX[dir], 1, kDirZ[dir]);
                                if (!Log(c, q))
                                    break;
                            }
                            out.push_back({q.Up(), 0, false});
                        }
                }
                if (Is(tp, "poplar_trunk_placer")) {
                    const int branches = SampleInt(tp["branch_amount"]);
                    for (int b = 0; b < branches; b++) {
                        const int dir = r.NextInt(4);
                        const Pos q = pos.Up(height / 2 + r.NextInt(std::max(1, height / 2 - 1))).Off(kDirX[dir], 0, kDirZ[dir]);
                        Log(c, q, kDirX[dir] ? "x" : "z");
                    }
                }
                out.push_back({pos.Up(height), 0, false});
                return out;
            }

            void Leaf(const json::Value& c, const Pos& p) {
                if (!ValidTree(p))
                    return;
                const uint16_t id = Provide(c["foliage_provider"], p);
                if (id != kNone)
                    w.Set(p, id);
            }

            void Row(const json::Value& c, const json::Value& fp, const Pos& center, int range, int localY, bool large) {
                const int extra = large ? 1 : 0;
                for (int dx = -range; dx <= range + extra; dx++)
                    for (int dz = -range; dz <= range + extra; dz++) {
                        int ax = std::abs(dx), az = std::abs(dz);
                        if (large) {
                            ax = std::min(std::abs(dx), std::abs(dx - 1));
                            az = std::min(std::abs(dz), std::abs(dz - 1));
                        }
                        if (Is(fp, "dark_oak_foliage_placer")) {
                            if (localY == 0 && large && (dx == -range || dx >= range) && (dz == -range || dz >= range))
                                continue;
                            if (localY == -1 && !large && ax == range && az == range)
                                continue;
                            if (localY == 1 && ax + az > range * 2 - 2)
                                continue;
                        } else if (Skip(fp, ax, localY, az, range)) {
                            continue;
                        }
                        Leaf(c, center.Off(dx, localY, dz));
                    }
            }

            bool Skip(const json::Value& fp, int dx, int localY, int dz, int range) {
                if (Is(fp, "blob_foliage_placer") || Is(fp, "poplar_foliage_placer") || Is(fp, "cherry_foliage_placer"))
                    return dx == range && dz == range && (r.NextInt(2) == 0 || localY == 0);
                if (Is(fp, "fancy_foliage_placer"))
                    return (dx + 0.5f) * (dx + 0.5f) + (dz + 0.5f) * (dz + 0.5f) > static_cast<float>(range * range);
                if (Is(fp, "spruce_foliage_placer") || Is(fp, "pine_foliage_placer"))
                    return dx == range && dz == range && range > 0;
                if (Is(fp, "jungle_foliage_placer") || Is(fp, "mega_pine_foliage_placer"))
                    return dx + dz >= 7 || dx * dx + dz * dz > range * range;
                if (Is(fp, "acacia_foliage_placer")) {
                    if (localY == 0)
                        return (dx > 1 || dz > 1) && dx != 0 && dz != 0;
                    return dx == range && dz == range && range > 0;
                }
                if (Is(fp, "bush_foliage_placer"))
                    return dx == range && dz == range && r.NextInt(2) == 0;
                return false;
            }

            void Foliage(const json::Value& c, const json::Value& fp, int treeHeight, const Attachment& a, int foliageHeight,
                         int radius, int offset) {
                (void)treeHeight;
                const int rr = radius + a.radiusOffset;
                const Pos& p = a.pos;
                if (Is(fp, "blob_foliage_placer") || Is(fp, "poplar_foliage_placer")) {
                    for (int y = offset; y >= offset - foliageHeight; y--)
                        Row(c, fp, p, std::max(rr - 1 - y / 2, 0), y, a.doubleTrunk);
                } else if (Is(fp, "fancy_foliage_placer") || Is(fp, "cherry_foliage_placer")) {
                    for (int y = offset; y >= offset - foliageHeight; y--)
                        Row(c, fp, p, rr + (y != offset && y != offset - foliageHeight ? 1 : 0), y, a.doubleTrunk);
                } else if (Is(fp, "spruce_foliage_placer")) {
                    int i = r.NextInt(2), j = 1, k = 0;
                    for (int l = offset; l >= -foliageHeight; l--) {
                        Row(c, fp, p, i, l, a.doubleTrunk);
                        if (i >= j) {
                            i = k;
                            k = 1;
                            j = std::min(j + 1, rr);
                        } else {
                            i++;
                        }
                    }
                } else if (Is(fp, "pine_foliage_placer")) {
                    int i = 0;
                    for (int j = offset; j >= offset - foliageHeight; j--) {
                        Row(c, fp, p, i, j, a.doubleTrunk);
                        if (i >= 1 && j == offset - foliageHeight + 1)
                            i--;
                        else if (i < rr)
                            i++;
                    }
                } else if (Is(fp, "mega_pine_foliage_placer")) {
                    const int base = p.y;
                    int last = 0;
                    for (int k = base - foliageHeight + offset; k <= base + offset; k++) {
                        const int l = base - k;
                        const int m = rr + static_cast<int>(std::floor(static_cast<float>(l) / foliageHeight * 3.5f));
                        const int n = l > 0 && m == last && (k & 1) == 0 ? m + 1 : m;
                        Row(c, fp, {p.x, k, p.z}, n, 0, a.doubleTrunk);
                        last = m;
                    }
                } else if (Is(fp, "jungle_foliage_placer")) {
                    const int i = a.doubleTrunk ? foliageHeight : 1 + r.NextInt(2);
                    for (int j = offset; j >= offset - i; j--)
                        Row(c, fp, p, rr + 1 - j, j, a.doubleTrunk);
                } else if (Is(fp, "acacia_foliage_placer")) {
                    const Pos q = p.Up(offset);
                    Row(c, fp, q, rr, -1 - foliageHeight, a.doubleTrunk);
                    Row(c, fp, q, rr - 1, -foliageHeight, a.doubleTrunk);
                    Row(c, fp, q, rr - 1, 0, a.doubleTrunk);
                } else if (Is(fp, "bush_foliage_placer")) {
                    for (int y = offset; y >= offset - foliageHeight; y--)
                        Row(c, fp, p, rr - y, y, a.doubleTrunk);
                } else if (Is(fp, "dark_oak_foliage_placer")) {
                    const Pos q = p.Up(offset);
                    if (a.doubleTrunk) {
                        Row(c, fp, q, rr + 2, -1, true);
                        Row(c, fp, q, rr + 3, 0, true);
                        Row(c, fp, q, rr + 2, 1, true);
                        if (r.NextBool())
                            Row(c, fp, q, rr, 2, true);
                    } else {
                        Row(c, fp, q, rr + 2, -1, false);
                        Row(c, fp, q, rr + 1, 0, false);
                    }
                } else if (Is(fp, "random_spread_foliage_placer")) {
                    const int attempts = static_cast<int>(fp["leaf_placement_attempts"].Int(64));
                    for (int i = 0; i < attempts; i++)
                        Leaf(c, p.Off(r.NextInt(std::max(1, rr)) - r.NextInt(std::max(1, rr)),
                                      r.NextInt(std::max(1, foliageHeight)) - r.NextInt(std::max(1, foliageHeight)),
                                      r.NextInt(std::max(1, rr)) - r.NextInt(std::max(1, rr))));
                } else {
                    for (int y = offset; y >= offset - foliageHeight; y--)
                        Row(c, fp, p, std::max(rr - 1 - y / 2, 0), y, a.doubleTrunk);
                }
            }

            void HangingVine(const Pos& start, int bits) {
                const uint16_t v = BlockCatalog::Instance().Resolve("minecraft:vine[vine_direction_bits=" + std::to_string(bits) + "]");
                Pos q = start;
                for (int i = 0; i < 5 && (w.F(q) & kAir); i++) {
                    w.Set(q, v);
                    q = q.Down();
                }
            }

            void Decorator(const json::Value& deco, const Pos& base) {
                static const int kVineBits[4] = {1, 2, 4, 8};
                if (Is(deco, "trunk_vine")) {
                    const std::vector<Pos> logs = w.logs;
                    for (const Pos& p : logs)
                        for (int i = 0; i < 4; i++)
                            if (r.NextInt(3) > 0) {
                                const Pos q = p.Off(kDirX[i], 0, kDirZ[i]);
                                if (w.F(q) & kAir)
                                    w.Set(q, BlockCatalog::Instance().Resolve("minecraft:vine[vine_direction_bits=" +
                                                                              std::to_string(kVineBits[(i + 2) % 4]) + "]"));
                            }
                } else if (Is(deco, "leave_vine")) {
                    const float chance = static_cast<float>(deco["probability"].Number(0.25));
                    const std::vector<Pos> leaves = w.leaves;
                    for (const Pos& p : leaves)
                        for (int i = 0; i < 4; i++)
                            if (r.NextFloat() < chance) {
                                const Pos q = p.Off(kDirX[i], 0, kDirZ[i]);
                                if (w.F(q) & kAir)
                                    HangingVine(q, kVineBits[(i + 2) % 4]);
                            }
                } else if (Is(deco, "cocoa")) {
                    if (r.NextFloat() >= deco["probability"].Number())
                        return;
                    if (w.logs.empty())
                        return;
                    int lowest = w.logs[0].y;
                    for (const Pos& p : w.logs)
                        lowest = std::min(lowest, p.y);
                    const std::vector<Pos> logs = w.logs;
                    for (const Pos& p : logs)
                        if (p.y - lowest <= 2)
                            for (int i = 0; i < 4; i++)
                                if (r.NextFloat() <= 0.25f) {
                                    const Pos q = p.Off(kDirX[i], 0, kDirZ[i]);
                                    if (w.F(q) & kAir)
                                        w.Set(q, BlockCatalog::Instance().Resolve(
                                                     "minecraft:cocoa[age=" + std::to_string(r.NextInt(3)) +
                                                     ",direction=" + std::to_string((i + 2) % 4) + "]"));
                                }
                } else if (Is(deco, "alter_ground")) {
                    const json::Value& prov = deco["provider"];
                    const auto at = [&](const Pos& p) {
                        for (int i = 2; i >= -3; i--) {
                            const Pos q = p.Up(i);
                            if (d.InTag("minecraft:substrate_overworld", w.Get(q))) {
                                const uint16_t id = Provide(prov, q);
                                if (id != kNone)
                                    w.Set(q, id);
                                break;
                            }
                            if (!(w.F(q) & kAir) && i < 0)
                                break;
                        }
                    };
                    const auto circle = [&](const Pos& p) {
                        for (int x = -2; x <= 2; x++)
                            for (int z = -2; z <= 2; z++)
                                if (std::abs(x) != 2 || std::abs(z) != 2)
                                    at(p.Off(x, 0, z));
                    };
                    circle(base.Off(-1, 0, -1));
                    circle(base.Off(2, 0, -1));
                    circle(base.Off(-1, 0, 2));
                    circle(base.Off(2, 0, 2));
                    for (int i = 0; i < 5; i++) {
                        const int j = r.NextInt(64);
                        circle(base.Off(j % 8 - 2, 0, j / 8 - 2));
                    }
                } else if (Is(deco, "place_on_ground") || Is(deco, "attached_to_leaves")) {
                    const int tries = static_cast<int>(deco["tries"].Int(32));
                    const int rad = static_cast<int>(deco["radius"].Int(2));
                    const json::Value& prov = deco["block_provider"];
                    if (Is(deco, "attached_to_leaves")) {
                        const std::vector<Pos> leaves = w.leaves;
                        for (const Pos& p : leaves)
                            if (r.NextFloat() < deco["probability"].Number() && (w.F(p.Down()) & kAir))
                                w.Set(p.Down(), Provide(prov, p.Down()));
                        return;
                    }
                    for (int i = 0; i < tries; i++) {
                        const Pos q = base.Off(r.Between(-rad, rad), r.Between(-1, 1), r.Between(-rad, rad));
                        if ((w.F(q) & kAir) && (w.F(q.Down()) & kSolid)) {
                            const uint16_t id = Provide(prov, q);
                            if (Survives(id, q))
                                w.Set(q, id);
                        }
                    }
                }
            }

        private:
            const JavaDecorator::Impl& d;
            World& w;
            Rng& r;
        };

        void ResolveStates(json::Value& v) {
            if (v.IsObject()) {
                if (v["b"].IsString()) {
                    json::Value id;
                    id.kind = json::Value::Kind::Number;
                    id.number = BlockCatalog::Instance().Resolve(v["b"].text);
                    v.members["id"] = id;
                    return;
                }
                for (auto& [k, m] : v.members)
                    ResolveStates(m);
            } else if (v.IsArray()) {
                for (auto& m : v.items)
                    ResolveStates(m);
            }
        }
    }

    JavaDecorator::JavaDecorator(int64_t seed) : impl(std::make_unique<Impl>()) {
        impl->seed = seed;
        impl->zoomSeed = ObfuscateBiomeSeed(seed);
        std::string text(kJavaFeatureJsonSize, '\0');
        mz_ulong size = static_cast<mz_ulong>(text.size());
        if (mz_uncompress(reinterpret_cast<unsigned char*>(text.data()), &size, kJavaFeatureData,
                          static_cast<mz_ulong>(kJavaFeatureDataSize)) != MZ_OK)
            return;
        if (!json::Parse(text, impl->doc))
            return;
        for (const auto& [name, list] : impl->doc["tags"].members) {
            auto& set = impl->tags[name];
            for (const auto& e : list.items)
                set.insert(e.text);
        }
        ResolveStates(impl->doc.members["placed"]);
        ResolveStates(impl->doc.members["configured"]);
        ResolveStates(impl->doc.members["providers"]);
        for (int step = 0; step < 11; step++) {
            const auto& list = impl->doc["order"][static_cast<size_t>(step)].items;
            for (size_t i = 0; i < list.size(); i++)
                impl->order[static_cast<size_t>(step)][list[i].text] = static_cast<int>(i);
        }
        std::unordered_map<std::string, int> byName;
        for (const auto& [name, b] : impl->doc["biomes"].members) {
            JavaBiome jb;
            jb.name = name;
            jb.temperature = static_cast<float>(b["temperature"].Number(0.5));
            jb.frozen = b["frozen"].Bool();
            jb.precipitation = b["precipitation"].Bool(true);
            for (size_t s = 0; s < 11 && s < b["features"].Size(); s++)
                for (const auto& f : b["features"][s].items) {
                    jb.steps[s].push_back(f.text);
                    jb.placed.insert(f.text);
                }
            jb.spawns = &b["spawns"];
            byName[name] = static_cast<int>(impl->biomes.size());
            impl->biomes.push_back(std::move(jb));
        }
        impl->tableToBiome.assign(static_cast<size_t>(kBiomeCount), -1);
        for (int i = 0; i < kBiomeCount; i++)
            if (const auto it = byName.find(kBiomes[i].java); it != byName.end())
                impl->tableToBiome[static_cast<size_t>(i)] = it->second;
        const BlockCatalog& c = BlockCatalog::Instance();
        impl->water = c.DefaultBlock("water");
        impl->ice = c.DefaultBlock("ice");
        impl->snow = c.DefaultBlock("snow_layer");
        placedCount = impl->doc["placed"].Size();
        ok = placedCount > 0;
    }

    JavaDecorator::~JavaDecorator() = default;

    const JavaBiome* JavaDecorator::Biome(int tableIndex) const {
        if (tableIndex < 0 || tableIndex >= static_cast<int>(impl->tableToBiome.size()))
            return nullptr;
        const int i = impl->tableToBiome[static_cast<size_t>(tableIndex)];
        return i < 0 ? nullptr : &impl->biomes[static_cast<size_t>(i)];
    }

    float JavaDecorator::Temperature(int tableIndex, int x, int y, int z) const {
        const JavaBiome* b = Biome(tableIndex);
        float t = b ? b->temperature : 0.8f;
        if (b && b->frozen) {
            const double frozen = static_cast<double>(impl->frozenNoise.Get(x * 0.05, z * 0.05) * 7.0f);
            const double n = impl->biomeInfo.Get(x * 0.2, z * 0.2);
            if (frozen + n < 0.3 && impl->biomeInfo.Get(x * 0.09, z * 0.09) < 0.8)
                t = 0.2f;
        }
        if (y > 80) {
            const float v = static_cast<float>(impl->temperatureNoise.Get(static_cast<double>(static_cast<float>(x) / 8.0f),
                                                                           static_cast<double>(static_cast<float>(z) / 8.0f))) *
                            8.0f;
            t -= (v + static_cast<float>(y) - 80.0f) * 0.05f / 40.0f;
        }
        return t;
    }

    const json::Value& JavaDecorator::Tags(const std::string& tag) const {
        return impl->doc["tags"][tag];
    }

    bool JavaDecorator::InTag(const std::string& tag, const std::string& name) const {
        const auto it = impl->tags.find(tag);
        return it != impl->tags.end() && it->second.contains(name);
    }

    void JavaDecorator::Decorate(int chunkX, int chunkZ, const ChunkColumn* const region[9], ChunkColumn& out) const {
        if (!ok)
            return;
        const Impl& d = *impl;
        std::vector<std::pair<int64_t, uint16_t>> merged;
        World world{d};
        world.cx = chunkX;
        world.cz = chunkZ;
        world.region = region;
        world.heights.fill(INT16_MIN);
        for (int oz = -1; oz <= 1; oz++)
            for (int ox = -1; ox <= 1; ox++) {
                const ChunkColumn* own = region[(oz + 1) * 3 + ox + 1];
                if (!own)
                    continue;
                const int originX = (chunkX + ox) * 16, originZ = (chunkZ + oz) * 16;
                world.overlay.clear();
                world.writes.clear();
                std::unordered_set<int> present;
                for (int i = 0; i < 256; i++)
                    present.insert(own->GenBiome(i >> 4, i & 15));
                if (own->HasQuartBiomes())
                    for (int qy = 0; qy < ChunkColumn::kQuartLayers; qy++)
                        for (int qz = 0; qz < 4; qz++)
                            for (int qx = 0; qx < 4; qx++)
                                present.insert(own->QuartBiome(qx, qy, qz));
                std::unordered_set<std::string> features;
                for (const int b : present)
                    if (const JavaBiome* jb = Biome(b))
                        for (const auto& f : jb->placed)
                            features.insert(f);
                Rng rng;
                rng.SetSeed(d.seed);
                const int64_t a = rng.NextLong() | 1, b = rng.NextLong() | 1;
                const int64_t decoration = (static_cast<int64_t>(originX) * a + static_cast<int64_t>(originZ) * b) ^ d.seed;
                Engine engine(d, world, rng);
                for (int step = 0; step < 11; step++) {
                    const auto& list = d.doc["order"][static_cast<size_t>(step)].items;
                    for (size_t index = 0; index < list.size(); index++) {
                        const std::string& name = list[index].text;
                        if (!features.contains(name))
                            continue;
                        rng.SetSeed(decoration + static_cast<int64_t>(index) + 10000ll * step);
                        world.topFeature = name;
                        const bool placedOk = engine.Placed(d.doc["placed"][name], {originX, kMinY, originZ});
                        if (std::getenv("PERLIN_DECO_DEBUG"))
                            std::fprintf(stderr, "deco %d,%d %s %d\n", originX >> 4, originZ >> 4, name.c_str(), placedOk ? 1 : 0);
                    }
                }
                for (const auto& [key, id] : world.writes) {
                    const int x = static_cast<int>(key >> 38);
                    const int z = static_cast<int>((key << 26) >> 38);
                    if ((x >> 4) == chunkX && (z >> 4) == chunkZ)
                        merged.emplace_back(key, id);
                }
            }
        for (const auto& [key, id] : merged) {
            const int x = static_cast<int>(key >> 38);
            const int z = static_cast<int>((key << 26) >> 38);
            const int y = static_cast<int>(key & 0xFFF) + kMinY;
            out.Set(x & 15, y, z & 15, id);
        }
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++) {
                int y = kMaxY;
                for (; y > kMinY; y--) {
                    const uint8_t f = d.Flags(out.Get(x, y, z));
                    if ((f & kSolid) || (f & (kWater | kLava)) || (f & kLeaves))
                        break;
                }
                const int wx = chunkX * 16 + x, wz = chunkZ * 16 + z;
                const int biome = out.GenBiome(x, z);
                const JavaBiome* jb = Biome(biome);
                if (!jb || !jb->precipitation)
                    continue;
                if (Temperature(biome, wx, y + 1, wz) >= 0.15f)
                    continue;
                const uint8_t f = d.Flags(out.Get(x, y, z));
                if (f & kWater) {
                    out.Set(x, y, z, d.ice);
                    continue;
                }
                if (y + 1 <= kMaxY && (d.Flags(out.Get(x, y + 1, z)) & kAir) && (f & (kSolid | kLeaves)) &&
                    !BlockRegistry::Instance().Name(out.Get(x, y, z)).contains("ice"))
                    out.Set(x, y + 1, z, d.snow);
            }
    }
}
