#include <unordered_set>
#include <cstring>
#include <mutex>
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "util/Log.hpp"
#include "world/level/Level.hpp"
#include "client/systems/blocks/BiomeTints.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/blocks/BlockModels.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/gfx/Tessellator.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "client/systems/player/BlockCollision.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

#if defined(_MSC_VER)
#define PERLIN_HOT __forceinline
#else
#define PERLIN_HOT inline __attribute__((always_inline))
#endif

namespace perlin {
    static constexpr float kShadeTop = 1.0f;
    static constexpr float kShadeBottom = 0.5f;
    static constexpr float kShadeX = 0.6f;
    static constexpr float kShadeZ = 0.8f;
    static constexpr float kAoSolid = 0.2f;

    static bool TexHas(const std::string& tex, const char* sub) {
        return tex.contains(sub);
    }
    static bool IsWaterTex(const std::string& t) {
        return TexHas(t, "still_water") || TexHas(t, "water_flow") || TexHas(t, "flowing_water") ||
               TexHas(t, "water_still");
    }
    static bool IsLavaTex(const std::string& t) {
        return TexHas(t, "lava_") || TexHas(t, "lava");
    }

    static bool IsLavaName(uint16_t id) {
        const std::string name = BlockPalette::Instance().Name(id);
        return name == "minecraft:lava" || name == "minecraft:flowing_lava";
    }

    struct GeomInfo {
        bool slab = false;
        bool slabTop = false;
        bool dblPlant = false;
        bool upper = false;
        int snowH = -1;
        int liquidDepth = -1;
        int growth = -1;
        bool stair = false;
        int stairDir = 0;
        bool stairTop = false;
        int stairCorner = 0;
        bool fence = false;
        bool fenceArm[4] = {false, false, false, false};
        bool wall = false;
        int wallArm[4] = {0, 0, 0, 0};
        bool wallPost = false;
        int cakeBites = -1;
        bool fire = false;
        bool kelp = false;
        bool flammable = false;
    };
    const GeomInfo& GeomFor(uint16_t id) {
        struct Slot {
            GeomInfo info;
            bool ready = false;
        };
        constexpr size_t kPageBits = 8;
        constexpr size_t kPageSize = size_t(1) << kPageBits;
        static thread_local std::vector<std::unique_ptr<Slot[]>> pages(65536 >> kPageBits);

        std::unique_ptr<Slot[]>& page = pages[id >> kPageBits];
        if (!page)
            page = std::make_unique<Slot[]>(kPageSize);
        Slot& slot = page[id & (kPageSize - 1)];
        if (slot.ready)
            return slot.info;

        GeomInfo g;
        {
            auto& pal = BlockPalette::Instance();
            {
                const std::string nm = pal.Name(id);
                g.fire = nm == "minecraft:fire";
                g.kelp = nm == "minecraft:kelp" || nm == "minecraft:kelp_plant";
                static const char* kBurn[] = {
                    "planks",        "_log",        "_wood",          "leaves",
                    "wool",          "carpet",      "hay_block",      "bookshelf",
                    "tnt",           "vine",        "coal_block",     "dried_kelp_block",
                    "bamboo_block",  "scaffolding", "azalea",         "lectern",
                    "loom",          "composter",   "beehive",        "bee_nest",
                    "target",        "wooden_",     "fence",          "stripped_",
                    "bamboo_mosaic", "moss_",       "hanging_roots",  "glow_lichen",
                    "cherry",        "mangrove",    "crafting_table", "chiseled_bookshelf"};
                for (const char* k : kBurn)
                    if (nm.contains(k) && !nm.contains("nether") && !nm.contains("crimson") &&
                        !nm.contains("warped")) {
                        g.flammable = true;
                        break;
                    }
            }
            if (pal.HasState(id, "minecraft:vertical_half") && !pal.Name(id).contains("double")) {
                g.slab = true;
                g.slabTop = pal.StateString(id, "minecraft:vertical_half", "bottom") == "top";
            }
            if (pal.HasState(id, "upper_block_bit")) {
                g.dblPlant = true;
                g.upper = pal.StateInt(id, "upper_block_bit", 0) != 0;
            }
            if (pal.HasState(id, "height"))
                g.snowH = pal.StateInt(id, "height", -1);
            if (pal.HasState(id, "liquid_depth"))
                g.liquidDepth = pal.StateInt(id, "liquid_depth", 0);
            if (pal.HasState(id, "growth"))
                g.growth = pal.StateInt(id, "growth", -1);
            if (pal.HasState(id, "weirdo_direction")) {
                g.stair = true;
                g.stairDir = pal.StateInt(id, "weirdo_direction", 0) & 3;
                g.stairTop = pal.StateInt(id, "upside_down_bit", 0) != 0;
                std::string c = pal.StateString(id, "minecraft:corner", "none");
                g.stairCorner = (c == "inner_left")    ? 1
                                : (c == "inner_right") ? 2
                                : (c == "outer_left")  ? 3
                                : (c == "outer_right") ? 4
                                                       : 0;
            }
            if (pal.HasState(id, "minecraft:connection_north")) {
                g.fence = true;
                g.fenceArm[0] = pal.StateInt(id, "minecraft:connection_east", 0) != 0;
                g.fenceArm[1] = pal.StateInt(id, "minecraft:connection_west", 0) != 0;
                g.fenceArm[2] = pal.StateInt(id, "minecraft:connection_south", 0) != 0;
                g.fenceArm[3] = pal.StateInt(id, "minecraft:connection_north", 0) != 0;
            }
            if (pal.HasState(id, "wall_connection_type_east")) {
                g.wall = true;
                auto arm = [&](const char* k) {
                    std::string v = pal.StateString(id, k, "none");
                    return v == "tall" ? 2 : (v == "short" ? 1 : 0);
                };
                g.wallArm[0] = arm("wall_connection_type_east");
                g.wallArm[1] = arm("wall_connection_type_west");
                g.wallArm[2] = arm("wall_connection_type_south");
                g.wallArm[3] = arm("wall_connection_type_north");
                g.wallPost = pal.StateInt(id, "wall_post_bit", 0) != 0;
            }
            if (pal.HasState(id, "bite_counter"))
                g.cakeBites = pal.StateInt(id, "bite_counter", -1);
        }
        slot.info = g;
        slot.ready = true;
        return slot.info;
    }

    static uint32_t PosHash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x * 374761393 + y * 668265263 + z * 1440662683u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    static void PlantOffset(int x, int z, const std::string& tex, bool doublePlant, float& dx, float& dy,
                            float& dz) {
        dx = dy = dz = 0.0f;
        static const char* const names[] = {"grass",      "fern",      "flower",   "poppy",       "dandelion",
                                            "tulip",      "orchid",    "allium",   "azure",       "daisy",
                                            "cornflower", "lily_of",   "rose",     "peony",       "lilac",
                                            "sunflower",  "dead_bush", "deadbush", "double_plant"};
        bool offset = false;
        for (const char* name : names)
            if (tex.contains(name))
                offset = true;
        if (!offset)
            return;
        int64_t seed = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u)) ^
                       static_cast<int64_t>(z) * 116129781LL;
        seed = seed * seed * 42317861LL + seed * 11LL;
        seed >>= 16;
        dx = std::clamp((static_cast<float>(seed & 15) / 15.0f - 0.5f) * 0.5f, -0.25f, 0.25f);
        dz = std::clamp((static_cast<float>((seed >> 8) & 15) / 15.0f - 0.5f) * 0.5f, -0.25f, 0.25f);
        if (!doublePlant && (tex.contains("tallgrass") || tex.contains("fern")))
            dy = (static_cast<float>((seed >> 4) & 15) / 15.0f - 1.0f) * 0.2f;
    }

    static int PickTile(const std::vector<int>& variants, int fallback, uint32_t h) {
        if (variants.empty())
            return fallback;
        return variants[h % variants.size()];
    }

    struct CellLight {
        float block = 0;
        float sky = 15;
    };

    static void EmitBox(Tessellator& tess, const CellLight& light, float bx, float by, float bz, float x0,
                        float y0, float z0, float x1, float y1, float z1, int tTop, int tSide, int tBottom) {
        const float e = 0.002f;
        x0 += e;
        y0 += e;
        z0 += e;
        x1 -= e;
        y1 -= e;
        z1 -= e;
        struct BF {
            int axis;
            float dir;
            int tile;
            float shade;
        };
        const BF faces[6] = {{1, 1, tTop, kShadeTop}, {1, -1, tBottom, kShadeBottom},
                             {0, 1, tSide, kShadeX},  {0, -1, tSide, kShadeX},
                             {2, 1, tSide, kShadeZ},  {2, -1, tSide, kShadeZ}};
        for (auto [axis, dir, tile, shade] : faces) {
            if (!tile)
                continue;
            float u0, v0, u1, v1;
            BlockLibrary::TileUV(tile, u0, v0, u1, v1);
            float v[4][3];
            if (axis == 0) {
                float X = dir > 0 ? x1 : x0;
                v[0][0] = X;
                v[0][1] = y0;
                v[0][2] = z0;
                v[1][0] = X;
                v[1][1] = y0;
                v[1][2] = z1;
                v[2][0] = X;
                v[2][1] = y1;
                v[2][2] = z1;
                v[3][0] = X;
                v[3][1] = y1;
                v[3][2] = z0;
            } else if (axis == 1) {
                float Y = dir > 0 ? y1 : y0;
                v[0][0] = x0;
                v[0][1] = Y;
                v[0][2] = z0;
                v[1][0] = x1;
                v[1][1] = Y;
                v[1][2] = z0;
                v[2][0] = x1;
                v[2][1] = Y;
                v[2][2] = z1;
                v[3][0] = x0;
                v[3][1] = Y;
                v[3][2] = z1;
            } else {
                float Z = dir > 0 ? z1 : z0;
                v[0][0] = x0;
                v[0][1] = y0;
                v[0][2] = Z;
                v[1][0] = x1;
                v[1][1] = y0;
                v[1][2] = Z;
                v[2][0] = x1;
                v[2][1] = y1;
                v[2][2] = Z;
                v[3][0] = x0;
                v[3][1] = y1;
                v[3][2] = Z;
            }
            for (auto& i : v) {
                i[0] += bx;
                i[1] += by;
                i[2] += bz;
            }
            float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            float col[4][3] = {
                {shade, shade, shade}, {shade, shade, shade}, {shade, shade, shade}, {shade, shade, shade}};
            tess.AddQuad(v, col, uv, false, light.block, light.sky);
        }
    }

    static int64_t JavaPosSeed(int x, int y, int z) {
        int64_t l = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u)) ^
                    (static_cast<int64_t>(z) * 116129781LL) ^ static_cast<int64_t>(y);
        l = static_cast<int64_t>(static_cast<uint64_t>(l) * static_cast<uint64_t>(l) * 42317861ULL +
                                 static_cast<uint64_t>(l) * 11ULL);
        return l >> 16;
    }

    struct JavaRandom {
        int64_t seed;
        explicit JavaRandom(int64_t s) : seed((s ^ 0x5DEECE66DLL) & ((1LL << 48) - 1)) {}
        int Next(int bits) {
            seed = (seed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
            return static_cast<int>(seed >> (48 - bits));
        }
        int NextInt(int bound) {
            int r = Next(31);
            const int m = bound - 1;
            if ((bound & m) == 0)
                return static_cast<int>((static_cast<int64_t>(bound) * static_cast<int64_t>(r)) >> 31);
            for (int u = r;
                 static_cast<int32_t>(static_cast<uint32_t>(u) - static_cast<uint32_t>(r = u % bound) +
                                      static_cast<uint32_t>(m)) < 0;
                 u = Next(31)) {}
            return r;
        }
    };

    static void ModelOffset(int x, int z, int kind, float& dx, float& dy, float& dz) {
        dx = dy = dz = 0.0f;
        const int64_t seed = JavaPosSeed(x, 0, z);
        const float maxH = kind == 3 ? 0.125f : 0.25f;
        dx = std::clamp((static_cast<float>(seed & 15) / 15.0f - 0.5f) * 0.5f, -maxH, maxH);
        dz = std::clamp((static_cast<float>((seed >> 8) & 15) / 15.0f - 0.5f) * 0.5f, -maxH, maxH);
        if (kind == 2)
            dy = (static_cast<float>((seed >> 4) & 15) / 15.0f - 1.0f) * 0.2f;
    }

    struct IdMemo {
        std::vector<const BlockAppearance*> app = std::vector<const BlockAppearance*>(65536, nullptr);
        std::vector<int8_t> solid = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> water = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> translucent = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> ao = std::vector<int8_t>(65536, -1);
        std::vector<int8_t> leaves = std::vector<int8_t>(65536, -1);
        std::vector<const ModelEntry*> model = std::vector<const ModelEntry*>(65536, nullptr);
        std::vector<int8_t> modelKnown = std::vector<int8_t>(65536, 0);
        std::vector<uint8_t> opacity = std::vector<uint8_t>(65536, 0);
        std::vector<uint8_t> opacityKnown = std::vector<uint8_t>(65536, 0);
        std::vector<uint32_t> stamp = std::vector<uint32_t>(65536, 0);
        uint32_t epoch = 1;
        uint32_t seenGlobal = 0;

        void Reset(uint32_t global) {
            if (seenGlobal != global) {
                seenGlobal = global;
                epoch++;
            }
        }
        void Touch(uint16_t id) {
            if (stamp[id] == epoch)
                return;
            stamp[id] = epoch;
            app[id] = nullptr;
            solid[id] = -1;
            water[id] = -1;
            translucent[id] = -1;
            ao[id] = -1;
            leaves[id] = -1;
            modelKnown[id] = 0;
            model[id] = nullptr;
            opacityKnown[id] = 0;
        }
    };
    static std::atomic<uint32_t> gMemoGlobal{1};
    void MesherInvalidateCaches() {
        gMemoGlobal++;
    }
    static IdMemo& Memo() {
        static thread_local IdMemo m;
        return m;
    }
    static const BlockAppearance* LookupApp(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        const BlockAppearance*& slot = m.app[id];
        if (!slot)
            slot = &library->Look(id);
        return slot;
    }

    static uint16_t ResolveDoor(uint16_t self, uint16_t other, bool selfUpper) {
        thread_local std::unordered_map<uint32_t, uint16_t> memo;
        const uint32_t key =
            (static_cast<uint32_t>(self) << 17) | (static_cast<uint32_t>(other) << 1) | (selfUpper ? 1u : 0u);
        if (const auto it = memo.find(key); it != memo.end())
            return it->second;
        BlockPalette& pal = BlockPalette::Instance();
        uint16_t result = self;
        const std::string name = pal.Name(self);
        if (pal.Name(other) == name) {
            const uint16_t lower = selfUpper ? other : self;
            const uint16_t upper = selfUpper ? self : other;
            auto states = pal.States(self);
            const auto lowerStates = pal.States(lower);
            const auto upperStates = pal.States(upper);
            const auto find = [](const std::vector<std::pair<std::string, BlockPalette::StateValue>>& v,
                                 const char* k) -> const BlockPalette::StateValue* {
                for (const auto& e : v)
                    if (e.first == k)
                        return &e.second;
                return nullptr;
            };
            for (auto& e : states) {
                const auto& src = e.first == "door_hinge_bit" ? upperStates : lowerStates;
                if (e.first == "door_hinge_bit" || e.first == "open_bit" ||
                    e.first == "minecraft:cardinal_direction")
                    if (const auto* v = find(src, e.first.c_str()))
                        e.second = *v;
            }
            std::ranges::sort(states, [](const auto& a, const auto& b) { return a.first < b.first; });
            result = pal.Intern(BlockPalette::HashState(name, states));
        }
        memo[key] = result;
        return result;
    }

    struct KindMemo {
        std::array<std::atomic<int8_t>, 65536> v;
        KindMemo() {
            for (auto& e : v)
                e.store(-1, std::memory_order_relaxed);
        }
    };

    static int SpecialKind(uint16_t id) {
        static KindMemo memo;
        const int8_t known = memo.v[id].load(std::memory_order_relaxed);
        if (known >= 0)
            return known;
        int8_t m = -1;
        {
            const std::string name = BlockPalette::Instance().Name(id);
            if (name.empty())
                return 0;
            const std::string shortName = name.starts_with("minecraft:") ? name.substr(10) : name;
            const bool sign =
                shortName.size() > 5 && shortName.compare(shortName.size() - 5, 5, "_sign") == 0;
            const auto ends = [&](const char* suffix) {
                const size_t n = std::strlen(suffix);
                return shortName.size() > n && shortName.compare(shortName.size() - n, n, suffix) == 0;
            };
            const bool skull = ends("_skull") ||
                               (ends("_head") && shortName != "piston_head" && !shortName.contains("bed")) ||
                               shortName.contains("copper_golem_statue") || shortName == "conduit" ||
                               shortName == "standing_banner" || shortName == "wall_banner";
            const bool chest = shortName == "chest" || shortName == "trapped_chest" ||
                               shortName == "ender_chest" || ends("copper_chest");
            m = shortName == "bed"             ? 1
                : sign                         ? 2
                : skull                        ? 3
                : shortName == "bubble_column" ? 4
                : chest                        ? 5
                                               : 0;
        }
        memo.v[id].store(m, std::memory_order_relaxed);
        return m;
    }

    static bool IsDoorId(uint16_t id) {
        static KindMemo memo;
        int8_t m = memo.v[id].load(std::memory_order_relaxed);
        if (m < 0) {
            BlockPalette& pal = BlockPalette::Instance();
            const std::string name = pal.Name(id);
            m = name.size() > 5 && name.compare(name.size() - 5, 5, "_door") == 0 &&
                        pal.BlockHasProperty(name, "door_hinge_bit")
                    ? 1
                    : 0;
            memo.v[id].store(m, std::memory_order_relaxed);
        }
        return m == 1;
    }

    static const ModelEntry* ModelFor(BlockLibrary* library, uint16_t id) {
        if (!library)
            return nullptr;
        IdMemo& m = Memo();
        m.Touch(id);
        if (!m.modelKnown[id]) {
            m.modelKnown[id] = 1;
            const ModelEntry* geo = library->Geometry().Empty() ? nullptr : library->Geometry().Find(id);
            if (!geo && !GeomFor(id).kelp && library->Models().Loaded()) {
                const BlockPalette& pal = BlockPalette::Instance();
                geo = library->Models().Find(pal.NetworkId(id));
                if (!geo)
                    if (const int32_t current = pal.CurrentHash(id); current != pal.NetworkId(id))
                        geo = library->Models().Find(current);
            }
            m.model[id] = geo;
        }
        return m.model[id];
    }

    static int LiquidKind(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        int8_t& slot = m.water[id];
        if (slot < 0) {
            const BlockAppearance* a = library ? LookupApp(library, id) : nullptr;
            slot = a && (IsWaterTex(a->texTop) || IsWaterTex(a->texSide)) ? 1 : (a && IsLavaName(id) ? 2 : 0);
            if (!slot && BlockPalette::Instance().Name(id) == "minecraft:bubble_column")
                slot = 1;
        }
        return slot;
    }
    static bool IsLiquidId(BlockLibrary* library, uint16_t id) {
        return LiquidKind(library, id) != 0;
    }

    static bool IsTranslucentId(BlockLibrary* library, uint16_t id) {
        IdMemo& m = Memo();
        m.Touch(id);
        int8_t& slot = m.translucent[id];
        if (slot < 0) {
            slot = 0;
            if (library) {
                const BlockAppearance& a = *LookupApp(library, id);
                const int tiles[3] = {a.top, a.side, a.bottom};
                for (int tile : tiles)
                    if (tile && library->AlphaOf(tile).translucent)
                        slot = 1;
            }
        }
        return slot != 0;
    }

    static bool LightsByShape(uint16_t id) {
        std::string n = BlockPalette::Instance().Name(id);
        if (n.starts_with("minecraft:"))
            n.erase(0, 10);
        auto ends = [&](const char* s) {
            const size_t l = std::strlen(s);
            return n.size() >= l && n.compare(n.size() - l, l, s) == 0;
        };
        if (n.contains("double"))
            return false;
        return ends("_stairs") || ends("_slab") || n == "farmland" || n == "grass_path" || n == "dirt_path" ||
               n == "snow_layer";
    }

    bool BlockFaceOcclusion(uint16_t id, FaceOcclusion& out) {
        if (!LightsByShape(id))
            return false;
        const physics::BlockShape& s = LevelCollision::ShapeOf(id);
        constexpr double eps = 1e-6;
        for (int dir = 0; dir < 6; dir++) {
            const int axis = dir < 2 ? 1 : (dir < 4 ? 2 : 0);
            const bool high = dir & 1;
            for (size_t b = 0; b < s.boxes.size(); b++) {
                const physics::Box& bx = s.boxes[b];
                const double lo[3] = {bx.x0, bx.y0, bx.z0}, hi[3] = {bx.x1, bx.y1, bx.z1};
                if (high ? hi[axis] < 1.0 - eps : lo[axis] > eps)
                    continue;
                const int a1 = axis == 0 ? 1 : 0;
                const int a2 = axis == 2 ? 1 : 2;
                for (int r = 0; r < 16; r++)
                    for (int q = 0; q < 16; q++) {
                        if (lo[a1] <= q / 16.0 + eps && hi[a1] >= (q + 1) / 16.0 - eps &&
                            lo[a2] <= r / 16.0 + eps && hi[a2] >= (r + 1) / 16.0 - eps) {
                            const int bit = r * 16 + q;
                            out.bits[dir][bit >> 6] |= 1ull << (bit & 63);
                        }
                    }
            }
        }
        return true;
    }

    int BlockLightOpacity(BlockLibrary& library, uint16_t id) {
        if (SpecialKind(id) == 3 || SpecialKind(id) == 5)
            return 0;
        {
            FaceOcclusion f;
            if (BlockFaceOcclusion(id, f)) {
                bool full = true;
                for (int d = 0; d < 6; d++)
                    full = full && f.Full(d);
                return full ? kMaxLight : 0;
            }
        }
        const BlockAppearance& a = library.Look(id);
        if ((a.top | a.side | a.bottom) == 0)
            return 0;
        if (a.shape != BlockAppearance::Shape::Cube)
            return 0;
        const GeomInfo& g = GeomFor(id);
        if (g.stair || g.slab) {
            FaceOcclusion f;
            if (BlockFaceOcclusion(id, f)) {
                bool full = true;
                for (int d = 0; d < 6; d++)
                    full = full && f.Full(d);
                return full ? kMaxLight : 0;
            }
            return kMaxLight;
        }
        if (g.fence || g.wall || g.cakeBites >= 0)
            return 0;
        if (IsWaterTex(a.texTop) || IsWaterTex(a.texSide) || IsLavaName(id))
            return 1;
        const int tiles[3] = {a.top, a.side, a.bottom};
        for (int i = 0; i < 3; i++)
            if (tiles[i] && library.AlphaOf(tiles[i]).holes &&
                !(i == 1 && a.overlayFrac > 0 && a.overlayTile))
                return 1;
        {
            const physics::BlockShape& shape = LevelCollision::ShapeOf(id);
            bool full = false;
            for (size_t b = 0; b < shape.boxes.size() && !full; b++) {
                const physics::Box& bx = shape.boxes[b];
                full = bx.x0 <= 1e-6 && bx.y0 <= 1e-6 && bx.z0 <= 1e-6 && bx.x1 >= 1.0 - 1e-6 &&
                       bx.y1 >= 1.0 - 1e-6 && bx.z1 >= 1.0 - 1e-6;
            }
            if (!full)
                return 0;
        }
        return kMaxLight;
    }

    struct TintKey {
        const void* app;
        int tile;
        size_t texHash;
        int biome;
        bool operator==(const TintKey& o) const {
            return app == o.app && tile == o.tile && texHash == o.texHash && biome == o.biome;
        }
    };
    struct TintKeyHash {
        size_t operator()(const TintKey& k) const {
            size_t h = std::hash<const void*>()(k.app);
            h = h * 1000003u ^ static_cast<size_t>(k.tile);
            h = h * 1000003u ^ k.texHash;
            h = h * 1000003u ^ static_cast<size_t>(k.biome);
            return h;
        }
    };

    struct BlockCache {
        std::vector<uint16_t> cells;
        std::vector<uint8_t> solid;
        std::vector<uint8_t> biome;
        std::vector<uint16_t> liquid;
        bool anyLiquid = false;
        int yBase = 0, yRows = 0;

        PERLIN_HOT size_t Idx(int x, int y, int z) const {
            return (static_cast<size_t>(y - yBase) * 18 + static_cast<size_t>(z + 1)) * 18 +
                   static_cast<size_t>(x + 1);
        }
        PERLIN_HOT bool InRange(int x, int y, int z) const {
            return x >= -1 && x <= 16 && z >= -1 && z <= 16 && y >= yBase && y < yBase + yRows;
        }

        void FillRaw(Level& level, const ChunkPos& pos, int yLo, int yHi) {
            yBase = yLo;
            yRows = yHi - yLo + 1;
            const size_t n = static_cast<size_t>(18) * static_cast<size_t>(yRows) * 18;
            cells.assign(n, 0);
            liquid.assign(n, 0);
            anyLiquid = false;
            solid.assign(n, 0);
            biome.assign(n, 1);
            aoCell.assign(n, 0xFF);
            const Chunk* around[3][3];
            for (int dz = 0; dz < 3; dz++)
                for (int dx = 0; dx < 3; dx++) {
                    const auto it = level.chunks.find({pos.x + dx - 1, pos.z + dz - 1});
                    around[dz][dx] = it == level.chunks.end() ? nullptr : &it->second;
                }

            for (int y = yLo; y <= yHi; y++)
                for (int z = -1; z <= 16; z++) {
                    const Chunk* const* row = around[z < 0 ? 0 : (z > 15 ? 2 : 1)];
                    const int zz = z & 15;
                    const size_t base = Idx(0, y, z);
                    if (row[1])
                        row[1]->CopyRun(y, zz, 0, 16, &cells[base], &biome[base], &liquid[base]);
                    if (row[0])
                        row[0]->CopyRun(y, zz, 15, 1, &cells[base - 1], &biome[base - 1], &liquid[base - 1]);
                    if (row[2])
                        row[2]->CopyRun(y, zz, 0, 1, &cells[base + 16], &biome[base + 16],
                                        &liquid[base + 16]);
                }
        }

        void Classify(BlockLibrary* library) {
            IdMemo& m = Memo();
            const size_t n = cells.size();
            for (size_t ci = 0; ci < n; ci++) {
                uint16_t id = cells[ci];
                if (id == 0)
                    continue;
                m.Touch(id);
                int8_t sv = m.solid[id];
                if (sv < 0) {
                    sv = ComputeSolid(library, id) ? 1 : 0;
                    m.solid[id] = sv;
                }
                solid[ci] = static_cast<uint8_t>(sv);
            }
        }

        static bool ComputeSolid(BlockLibrary* library, uint16_t id) {
            if (!library)
                return true;
            if (SpecialKind(id) == 1 || SpecialKind(id) == 3 || SpecialKind(id) == 5)
                return false;
            if (const ModelEntry* me = ModelFor(library, id); me && !me->cube)
                return false;
            const BlockAppearance& a = *LookupApp(library, id);
            if ((a.top | a.side | a.bottom) == 0)
                return false;
            if (a.shape != BlockAppearance::Shape::Cube)
                return false;
            const GeomInfo& g = GeomFor(id);
            if (g.stair || g.fence || g.wall || g.cakeBites >= 0)
                return false;
            BlockLibrary::TileAlpha at = library->AlphaOf(a.top);
            BlockLibrary::TileAlpha as = library->AlphaOf(a.side);
            BlockLibrary::TileAlpha ab = library->AlphaOf(a.bottom);
            if ((a.top && at.translucent) || (a.side && as.translucent) || (a.bottom && ab.translucent))
                return false;
            if (IsWaterTex(a.texTop) || IsWaterTex(a.texSide) || IsLavaName(id))
                return false;
            const bool sideHoles = a.side && as.holes && !(a.overlayFrac > 0 && a.overlayTile);
            bool holes = (a.top && at.holes) || sideHoles || (a.bottom && ab.holes);
            return !holes;
        }

        PERLIN_HOT uint16_t Get(int x, int y, int z) const {
            return InRange(x, y, z) ? cells[Idx(x, y, z)] : static_cast<uint16_t>(0);
        }
        PERLIN_HOT uint16_t Liquid(int x, int y, int z) const {
            return InRange(x, y, z) ? liquid[Idx(x, y, z)] : static_cast<uint16_t>(0);
        }
        PERLIN_HOT uint8_t Occ(int x, int y, int z) const {
            return InRange(x, y, z) ? solid[Idx(x, y, z)] : static_cast<uint8_t>(0);
        }
        mutable std::vector<uint8_t> aoCell;
        PERLIN_HOT uint8_t Biome(int x, int y, int z) const {
            return InRange(x, y, z) ? biome[Idx(x, y, z)] : static_cast<uint8_t>(1);
        }
    };

    static uint16_t EffLiquid(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
        const uint16_t id = cache.Get(x, y, z);
        if (id != 0 && IsLiquidId(library, id))
            return id;
        const uint16_t l = cache.Liquid(x, y, z);
        return l != 0 && IsLiquidId(library, l) ? l : 0;
    }

    static uint16_t ColumnWater(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
        static const int dirs[6][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const auto& d : dirs) {
            const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            const uint16_t layer = cache.Liquid(nx, ny, nz);
            if (layer != 0 && IsLiquidId(library, layer) && SpecialKind(layer) != 4)
                return layer;
            const uint16_t block = cache.Get(nx, ny, nz);
            if (block != 0 && SpecialKind(block) != 4 && LiquidKind(library, block) == 1)
                return block;
        }
        return 0;
    }

    static float WaterCornerHeight(const BlockCache& cache, BlockLibrary* library, int kind, int y, int cx,
                                   int cz) {
        float total = 0.0f;
        int weight = 0;
        for (int i = 0; i < 4; i++) {
            const int px = cx - 1 + (i & 1), pz = cz - 1 + (i >> 1);
            const uint16_t above = EffLiquid(cache, library, px, y + 1, pz);
            if (above && LiquidKind(library, above) == kind)
                return 1.0f;
            uint16_t id = EffLiquid(cache, library, px, y, pz);
            if (id != 0 && LiquidKind(library, id) != kind)
                id = 0;
            if (id != 0) {
                const int depth = GeomFor(id).liquidDepth;
                const float height = static_cast<float>(8 - (depth >= 0 ? (depth & 7) : 0)) / 9.0f;
                if (height >= 0.8f) {
                    total += height * 10.0f;
                    weight += 10;
                } else {
                    total += height;
                    weight++;
                }
            } else if (!cache.Occ(px, y, pz)) {
                weight++;
            }
        }
        return weight ? total / static_cast<float>(weight) : 0.0f;
    }

    static float AoShadeUncached(const BlockCache& cache, BlockLibrary* library, int x, int y, int z);
    static std::atomic<bool> gCullInnerLeaves{false};
    void MesherSetCullInnerLeaves(bool on) {
        gCullInnerLeaves.store(on, std::memory_order_relaxed);
    }
    static bool IsLeafBlock(uint16_t id) {
        if (id == 0)
            return false;
        IdMemo& m = Memo();
        m.Touch(id);
        if (m.leaves[id] < 0)
            m.leaves[id] = BlockPalette::Instance().Name(id).contains("leaves") ? 1 : 0;
        return m.leaves[id] == 1;
    }
    PERLIN_HOT static float AoShade(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
        if (!cache.InRange(x, y, z))
            return 1.0f;
        uint8_t& known = cache.aoCell[cache.Idx(x, y, z)];
        if (known == 0xFF)
            known = AoShadeUncached(cache, library, x, y, z) == 1.0f ? 0 : 1;
        return known ? 0.2f : 1.0f;
    }

    static float AoShadeUncached(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
        const uint16_t id = cache.Get(x, y, z);
        if (id == 0)
            return 1.0f;
        if (cache.Occ(x, y, z))
            return 0.2f;
        IdMemo& m = Memo();
        m.Touch(id);
        if (m.ao[id] < 0) {
            const std::string name = BlockPalette::Instance().Name(id);
            const BlockAppearance* a = library ? LookupApp(library, id) : nullptr;
            const bool cube =
                a && a->shape == BlockAppearance::Shape::Cube && (a->top | a->side | a->bottom) != 0;
            m.ao[id] = cube &&
                               (name.contains("leaves") || name == "minecraft:ice" ||
                                name == "minecraft:frosted_ice") &&
                               !IsLiquidId(library, id)
                           ? 1
                           : 0;
        }
        return m.ao[id] == 1 ? 0.2f : 1.0f;
    }

    static uint16_t SectionConnectivity(const BlockCache& cache, int sy) {
        const int y0 = sy * 16;
        static thread_local std::vector<uint8_t> seen;
        static thread_local std::vector<uint16_t> stack;
        seen.assign(4096, 0);
        int opaque = 0;
        for (int i = 0; i < 4096; i++) {
            const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
            if (cache.Occ(x, y0 + y, z)) {
                seen[i] = 1;
                opaque++;
            }
        }
        if (opaque == 0)
            return 0x7FFF;
        if (opaque == 4096)
            return 0;
        uint16_t conn = 0;
        for (int start = 0; start < 4096; start++) {
            if (seen[start])
                continue;
            int mask = 0;
            stack.clear();
            stack.push_back(static_cast<uint16_t>(start));
            seen[start] = 1;
            while (!stack.empty()) {
                const int i = stack.back();
                stack.pop_back();
                const int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
                if (y == 0)
                    mask |= 1;
                if (y == 15)
                    mask |= 2;
                if (z == 0)
                    mask |= 4;
                if (z == 15)
                    mask |= 8;
                if (x == 0)
                    mask |= 16;
                if (x == 15)
                    mask |= 32;
                auto push = [&](int ni) {
                    if (!seen[ni]) {
                        seen[ni] = 1;
                        stack.push_back(static_cast<uint16_t>(ni));
                    }
                };
                if (x > 0)
                    push(i - 1);
                if (x < 15)
                    push(i + 1);
                if (z > 0)
                    push(i - 16);
                if (z < 15)
                    push(i + 16);
                if (y > 0)
                    push(i - 256);
                if (y < 15)
                    push(i + 256);
            }
            for (int a = 0; a < 6; a++)
                if (mask & (1 << a))
                    for (int b = a + 1; b < 6; b++)
                        if (mask & (1 << b))
                            conn |= static_cast<uint16_t>((1u << (a * (11 - a) / 2 + (b - a - 1))));
            if (conn == 0x7FFF)
                break;
        }
        return conn;
    }

    namespace {
        std::atomic<uint64_t> gProf[5];
        std::atomic<uint64_t> gProfChunks{0};
        uint64_t NowUs() {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch());
            return static_cast<uint64_t>(elapsed.count());
        }
    }

    void MeshProfilePrint() {
        const uint64_t n = gProfChunks.load();
        if (!n)
            return;
        std::printf("[meshprof] %llu chunks: fill+classify %.2f ms, connectivity %.2f ms, lighting %.2f ms, "
                    "quads %.2f ms, total %.2f ms (avg)\n",
                    (unsigned long long)n, gProf[0] / 1000.0 / n, gProf[1] / 1000.0 / n,
                    gProf[2] / 1000.0 / n, gProf[3] / 1000.0 / n, gProf[4] / 1000.0 / n);
    }

    namespace {
        constexpr int kFaceNormal[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                           {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
        constexpr float kFaceShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
        constexpr int kVertexOrder[4] = {0, 3, 2, 1};

        struct PackedLight {
            int block, sky;
        };

        struct CornerShade {
            float ao, block, sky;
        };

        PackedLight BlendCorner(PackedLight n1, PackedLight n2, PackedLight n3, PackedLight c) {
            if (c.sky > 2 * 16 - 1 || c.block > 2 * 16 - 1) {
                for (PackedLight* n : {&n1, &n2, &n3}) {
                    if (n->block == 0 && n->sky == 0)
                        *n = c;
                    else if (n->sky == 0)
                        n->sky = c.sky;
                }
            }
            return PackedLight{(n1.block + n2.block + n3.block + c.block) >> 2,
                               (n1.sky + n2.sky + n3.sky + c.sky) >> 2};
        }

        void FaceAxes(const int* normal, int& axis1, int& axis2) {
            axis1 = normal[0] != 0 ? 1 : 0;
            axis2 = normal[2] != 0 ? 1 : 2;
        }

        void FirstCornerOffset(const int* normal, int (&first)[3]) {
            first[0] = first[1] = first[2] = 0;
            if (normal[1] < 0)
                first[0] = -1;
            else if (normal[1] > 0)
                first[0] = 1;
            else if (normal[2] < 0)
                first[1] = 1;
            else if (normal[2] > 0)
                first[0] = -1;
            else if (normal[0] < 0)
                first[1] = 1;
            else
                first[1] = -1;
        }

        class BlockMesher {
        public:
            BlockMesher(Tessellator& tess, const BlockCache& cache, BlockLibrary* library, BiomeTints* tints,
                        const ChunkLighting::Result& lighting, ChunkPos pos)
                : tess(tess), cache(cache), library(library), tints(tints), lighting(lighting), pos(pos),
                  registry(LightRegistry::Instance()), textured(library && library->Ready()),
                  haveTints(tints && tints->Ready()), baseX(pos.x * 16), baseZ(pos.z * 16), biomeNames(256) {}

            void MeshCell(int x, int y, int z, int pass);

        private:
            struct BlockCtx {
                int x, y, z, pass;
                uint16_t id;
                const BlockAppearance* app;
                const ModelEntry* modelEntry;
                CellLight cellLight;
                float bx, by, bz;
                int biomeId;
                const std::string* biomeName;
                const GeomInfo* gi;
                uint32_t bhash;
                int blockUniform = -1;
                int blockCentre = 0;
            };

            std::array<int, 2> Sample(int lx, int ly, int lz) const;
            PackedLight LightAt(const int p[3]) const;
            CornerShade ShadeCorner(const int center[3], const int normal[3], int axis1, int axis2, int d1,
                                    int d2, const int first[3], float shadeCenter,
                                    PackedLight lightCenter) const;
            void ShadeFace(const int center[3], const int normal[3], int axis1, int axis2, const int first[3],
                           float shadeCenter, PackedLight lightCenter, CornerShade out[2][2]) const;
            CellLight OwnLight(int lx, int ly, int lz, uint16_t cellId) const;
            std::array<float, 4> BiomeTintForRaw(const BlockCtx& blk, const std::string& biomeName,
                                                 const std::string& tex, int tile, bool isTop);
            std::array<float, 4> BiomeTintFor(BlockCtx& blk, const std::string& tex, int tile, bool isTop);
            void EmitStairLike(BlockCtx& blk);
            class ModelEmitter;
            class CubeEmitter;
            void EmitModel(BlockCtx& blk);
            void EmitFlat(BlockCtx& blk);
            void EmitWall(BlockCtx& blk);
            void EmitFire(BlockCtx& blk);
            void EmitCross(BlockCtx& blk);
            void EmitCubeOrLiquid(BlockCtx& blk);

            Tessellator& tess;
            const BlockCache& cache;
            BlockLibrary* library;
            BiomeTints* tints;
            const ChunkLighting::Result& lighting;
            ChunkPos pos;
            const LightRegistry& registry;
            const bool textured;
            const bool haveTints;
            const int baseX;
            const int baseZ;
            std::vector<std::string> biomeNames;
            std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> tintMemo;
            std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> blendMemo;
            std::unordered_set<uint16_t> askedFor;
        };

        const std::string kPlainsName = "minecraft:plains";

        std::array<int, 2> BlockMesher::Sample(int lx, int ly, int lz) const {
            const uint8_t packed = lighting.At(lx, ly, lz);
            return {ChunkLighting::Result::Block(packed), ChunkLighting::Result::Sky(packed)};
        }

        CellLight BlockMesher::OwnLight(int lx, int ly, int lz, uint16_t cellId) const {
            std::array<int, 2> l = Sample(lx, ly, lz);
            if (!cache.Occ(lx, ly, lz) && registry.Resolve(cellId).opacity >= kMaxLight) {
                static constexpr int kOffsets[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                                       {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
                for (const auto& o : kOffsets) {
                    const std::array<int, 2> n = Sample(lx + o[0], ly + o[1], lz + o[2]);
                    l[0] = std::max(l[0], n[0]);
                    l[1] = std::max(l[1], n[1]);
                }
            }
            return CellLight{static_cast<float>(l[0]), static_cast<float>(l[1])};
        }

        std::array<float, 4> BlockMesher::BiomeTintForRaw(const BlockCtx& blk, const std::string& biomeName,
                                                          const std::string& tex, int tile, bool isTop) {
            const BlockAppearance* app = blk.app;
            (void)isTop;
            if (app) {
                static const char* kGrassKeys[] = {
                    "grass_top",          "grass_carried", "tallgrass",  "tall_grass", "short_grass",
                    "double_plant_grass", "fern",          "large_fern", "reeds",      "sugar_cane"};
                std::string tpath = library ? library->TilePathOf(tile) : std::string();
                bool grassLike = false;
                for (const char* k : kGrassKeys)
                    if (TexHas(tex, k) || TexHas(tpath, k)) {
                        grassLike = true;
                        break;
                    }
                if (grassLike) {
                    std::array<float, 3> c =
                        haveTints ? tints->Grass(biomeName) : std::array<float, 3>{0.4745f, 0.7529f, 0.3529f};
                    static int logged = 0;
                    if (logged < 6) {
                        logged++;
                        printf("[tint] grass-like tex='%s' path='%s' tile=%d haveTints=%d "
                               "biome=%s -> (%.2f,%.2f,%.2f)\n",
                               tex.c_str(), tpath.c_str(), tile, static_cast<int>(haveTints),
                               biomeName.c_str(), c[0], c[1], c[2]);
                    }
                    return {c[0], c[1], c[2], 1};
                }
            }
            if (!haveTints || !app)
                return {app ? app->tintR : 1, app ? app->tintG : 1, app ? app->tintB : 1, 0};
            if (IsWaterTex(tex)) {
                auto c = tints->Water(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            if (IsLavaTex(tex))
                return {1, 1, 1, 1};
            if (TexHas(tex, "leaf_litter")) {
                const auto c = tints->DryFoliage(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            if (library && library->SaturationOf(tile) > 0.25f)
                return {1, 1, 1, 1};
            if (TexHas(tex, "vine") && !TexHas(tex, "cave_vines") && !TexHas(tex, "twisting") &&
                !TexHas(tex, "weeping")) {
                auto c = tints->Foliage(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            if (TexHas(tex, "leaves") || TexHas(tex, "leaf")) {
                if (TexHas(tex, "birch"))
                    return {tints->MapAverage("birch")[0], tints->MapAverage("birch")[1],
                            tints->MapAverage("birch")[2], 1};
                if (TexHas(tex, "spruce") || TexHas(tex, "evergreen"))
                    return {tints->MapAverage("evergreen")[0], tints->MapAverage("evergreen")[1],
                            tints->MapAverage("evergreen")[2], 1};
                if (TexHas(tex, "mangrove"))
                    return {tints->MapAverage("mangrove_swamp_foliage")[0],
                            tints->MapAverage("mangrove_swamp_foliage")[1],
                            tints->MapAverage("mangrove_swamp_foliage")[2], 1};
                auto c = tints->Foliage(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            if (TexHas(tex, "grass_carried") || tex == "grass_top" || TexHas(tex, "grass_top") ||
                TexHas(tex, "tallgrass") || TexHas(tex, "tall_grass") || TexHas(tex, "short_grass") ||
                TexHas(tex, "double_plant_grass") || tex == "fern" || TexHas(tex, "fern_carried")) {
                auto c = tints->Grass(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            if (app->useCarriedColor) {
                auto c = tints->Foliage(biomeName);
                return {c[0], c[1], c[2], 1};
            }
            return {app->tintR, app->tintG, app->tintB, 0};
        }

        std::array<float, 4> BlockMesher::BiomeTintFor(BlockCtx& blk, const std::string& tex, int tile,
                                                       bool isTop) {
            const int x = blk.x, y = blk.y, z = blk.z;
            const uint16_t id = blk.id;
            const BlockAppearance* app = blk.app;
            if (TexHas(tex, "redstone_dust")) {
                const float f =
                    static_cast<float>(BlockPalette::Instance().StateInt(id, "redstone_signal", 0)) / 15.0f;
                return {f * 0.6f + (f > 0.0f ? 0.4f : 0.3f), std::clamp(f * f * 0.7f - 0.5f, 0.0f, 1.0f),
                        std::clamp(f * f * 0.6f - 0.7f, 0.0f, 1.0f), 1.0f};
            }
            const size_t texHash = std::hash<std::string>()(tex);
            std::array<float, 4> sum{0, 0, 0, 0};
            std::array<float, 4> own{0, 0, 0, 0};
            int count = 0;
            if (blk.blockUniform < 0) {
                blk.blockCentre = cache.Biome(std::clamp(x, -1, 16), y, std::clamp(z, -1, 16));
                blk.blockUniform = 1;
                for (int dz = -2; dz <= 2 && blk.blockUniform; dz++)
                    for (int dx = -2; dx <= 2; dx++)
                        if (cache.Biome(std::clamp(x + dx, -1, 16), y, std::clamp(z + dz, -1, 16)) !=
                            blk.blockCentre) {
                            blk.blockUniform = 0;
                            break;
                        }
            }
            const int centre = blk.blockCentre;
            const bool uniform = blk.blockUniform == 1;
            if (uniform) {
                TintKey k{app, tile, texHash, centre};
                auto mit = tintMemo.find(k);
                if (mit == tintMemo.end()) {
                    std::string& bn = biomeNames[static_cast<size_t>(centre) & 255];
                    if (bn.empty() && haveTints)
                        bn = tints->NameForId(centre);
                    mit =
                        tintMemo
                            .emplace(k, BiomeTintForRaw(blk, haveTints ? bn : kPlainsName, tex, tile, isTop))
                            .first;
                }
                return mit->second;
            }
            TintKey blendKey{app, tile, texHash,
                             (x & 31) | ((z & 31) << 5) | (((y - cache.yBase) & 1023) << 10)};
            auto bit = blendMemo.find(blendKey);
            if (bit != blendMemo.end())
                return bit->second;
            for (int dz = -2; dz <= 2; dz++)
                for (int dx = -2; dx <= 2; dx++) {
                    const int sx = std::clamp(x + dx, -1, 16), sz = std::clamp(z + dz, -1, 16);
                    const int sample = cache.Biome(sx, y, sz);
                    TintKey k{app, tile, texHash, sample};
                    auto mit = tintMemo.find(k);
                    if (mit == tintMemo.end()) {
                        std::string& bn = biomeNames[static_cast<size_t>(sample) & 255];
                        if (bn.empty() && haveTints)
                            bn = tints->NameForId(sample);
                        mit = tintMemo
                                  .emplace(
                                      k, BiomeTintForRaw(blk, haveTints ? bn : kPlainsName, tex, tile, isTop))
                                  .first;
                    }
                    for (int c = 0; c < 3; c++)
                        sum[c] += mit->second[c];
                    if (dx == 0 && dz == 0)
                        own = mit->second;
                    count++;
                }
            std::array<float, 4> r = {sum[0] / count, sum[1] / count, sum[2] / count, own[3]};
            blendMemo.emplace(blendKey, r);
            return r;
        }

        void BlockMesher::MeshCell(int x, int y, int z, int pass) {
            uint16_t id = cache.Get(x, y, z);
            if (pass == 1 && y > cache.yBase && y < cache.yBase + cache.yRows - 1) {
                const size_t c = cache.Idx(x, y, z);
                if (cache.solid[c] && cache.solid[c - 1] && cache.solid[c + 1] && cache.solid[c - 18] &&
                    cache.solid[c + 18] && cache.solid[c - 324] && cache.solid[c + 324])
                    return;
            }
            if (pass == 0) {
                const uint16_t here = cache.Get(x, y, z);
                const bool column = here != 0 && SpecialKind(here) == 4;
                id = cache.Liquid(x, y, z);
                if (column && library && (id == 0 || !IsLiquidId(library, id)))
                    id = ColumnWater(cache, library, x, y, z);
                if (id == 0 || !library || !IsLiquidId(library, id) || (!column && IsLiquidId(library, here)))
                    return;
            }
            if (id == 0)
                return;
            if (pass == 1 && textured) {
                if (SpecialKind(id) == 4)
                    return;
                if (const int special = SpecialKind(id); special && special != 4) {
                    tess.AddSpecial(baseX + x, y, baseZ + z, id);
                    if (special == 1 || special == 3 || special == 5)
                        return;
                }
            }
            if (pass == 1 && textured && IsDoorId(id)) {
                const bool upperHalf = BlockPalette::Instance().StateInt(id, "upper_block_bit", 0) != 0;
                const int oy = upperHalf ? y - 1 : y + 1;
                if (oy >= cache.yBase && oy < cache.yBase + cache.yRows)
                    id = ResolveDoor(id, cache.Get(x, oy, z), upperHalf);
            }
            tess.SetLayer(RenderLayer::Cutout);
            const CellLight cellLight = OwnLight(x, y, z, id);
            const BlockAppearance* app = nullptr;
            if (textured)
                app = LookupApp(library, id);
            const ModelEntry* modelEntry = textured ? ModelFor(library, id) : nullptr;
            if (app && !modelEntry && (app->top | app->side | app->bottom) == 0) {

                if (askedFor.insert(id).second) {
                    const std::string& wantName = BlockPalette::Instance().Name(id);
                    if (!wantName.empty() && !wantName.starts_with("minecraft:"))
                        ServerBlocks::Instance().Want(wantName, pos.x, pos.z);
                }
                static std::mutex logMutex;
                static std::unordered_set<uint16_t> logged;
                std::lock_guard<std::mutex> lk(logMutex);
                if (logged.insert(id).second)
                    printf("[blocks] hidden untextured block id=%u name='%s' at (%d,%d,%d)\n",
                           static_cast<unsigned>(id), BlockPalette::Instance().Name(id).c_str(), baseX + x, y,
                           baseZ + z);
                return;
            }
            float bx = static_cast<float>(baseX + x), by = static_cast<float>(y),
                  bz = static_cast<float>(baseZ + z);
            tess.SetBlock(id, bx, by, bz);

            const int biomeId = cache.Biome(x, y, z);
            const std::string* biomeNamePtr = &kPlainsName;
            if (haveTints) {
                std::string& bn = biomeNames[static_cast<size_t>(biomeId) & 255];
                if (bn.empty())
                    bn = tints->NameForId(biomeId);
                biomeNamePtr = &bn;
            }
            const std::string& biomeName = *biomeNamePtr;
            const GeomInfo& gi = GeomFor(id);
            uint32_t bhash = PosHash(baseX + x, y, baseZ + z);

            BlockCtx blk{x,  y,  z,  pass,    id,         app, modelEntry, cellLight,
                         bx, by, bz, biomeId, &biomeName, &gi, bhash};
            if (app && !modelEntry && (gi.stair || gi.fence || gi.wall || gi.cakeBites >= 0)) {
                EmitStairLike(blk);
                return;
            }
            if (modelEntry) {
                EmitModel(blk);
                return;
            }
            if (app && app->shape == BlockAppearance::Shape::Flat) {
                EmitFlat(blk);
                return;
            }
            if (app && app->shape == BlockAppearance::Shape::Wall) {
                EmitWall(blk);
                return;
            }
            if (gi.fire && app) {
                EmitFire(blk);
                return;
            }
            if (app && app->shape == BlockAppearance::Shape::Cross) {
                EmitCross(blk);
                return;
            }
            EmitCubeOrLiquid(blk);
        }

        void BlockMesher::EmitStairLike(BlockCtx& blk) {
            const BlockAppearance* app = blk.app;
            const CellLight& cellLight = blk.cellLight;
            const float bx = blk.bx;
            const float by = blk.by;
            const float bz = blk.bz;
            const GeomInfo& gi = *blk.gi;
            const uint32_t bhash = blk.bhash;

            int ct = PickTile(app->varTop, app->top, bhash);
            int cs = PickTile(app->varSide, app->side, bhash);
            int cb = PickTile(app->varBottom, app->bottom, bhash);
            auto rotXZ = [&](float lx, float lz, int dir, float& ox, float& oz) {
                switch (dir & 3) {
                case 0:
                    ox = lx;
                    oz = lz;
                    break;
                case 1:
                    ox = 1 - lx;
                    oz = 1 - lz;
                    break;
                case 2:
                    ox = 1 - lz;
                    oz = lx;
                    break;
                default:
                    ox = lz;
                    oz = 1 - lx;
                    break;
                }
            };
            auto emitRot = [&](float lx0, float ly0, float lz0, float lx1, float ly1, float lz1, int dir,
                               bool flipY) {
                float ax0 = lx0, az0 = lz0, ax1 = lx1, az1 = lz1;
                float ox0, oz0, ox1, oz1;
                rotXZ(ax0, az0, dir, ox0, oz0);
                rotXZ(ax1, az1, dir, ox1, oz1);
                float rx0 = ox0 < ox1 ? ox0 : ox1, rx1 = ox0 < ox1 ? ox1 : ox0;
                float rz0 = oz0 < oz1 ? oz0 : oz1, rz1 = oz0 < oz1 ? oz1 : oz0;
                float ry0 = flipY ? 1 - ly1 : ly0, ry1 = flipY ? 1 - ly0 : ly1;
                EmitBox(tess, cellLight, bx, by, bz, rx0, ry0, rz0, rx1, ry1, rz1, ct, cs, cb);
            };
            if (gi.stair) {
                float yA0 = gi.stairTop ? 0.5f : 0.0f;
                float yA1 = gi.stairTop ? 1.0f : 0.5f;
                float yB0 = gi.stairTop ? 0.0f : 0.5f;
                float yB1 = gi.stairTop ? 0.5f : 1.0f;
                emitRot(0, yA0, 0, 1, yA1, 1, gi.stairDir, false);
                switch (gi.stairCorner) {
                case 1:
                    emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                    emitRot(0, yB0, 0, 0.5f, yB1, 0.5f, gi.stairDir, false);
                    break;
                case 2:
                    emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                    emitRot(0, yB0, 0.5f, 0.5f, yB1, 1, gi.stairDir, false);
                    break;
                case 3:
                    emitRot(0.5f, yB0, 0, 1, yB1, 0.5f, gi.stairDir, false);
                    break;
                case 4:
                    emitRot(0.5f, yB0, 0.5f, 1, yB1, 1, gi.stairDir, false);
                    break;
                default:
                    emitRot(0.5f, yB0, 0, 1, yB1, 1, gi.stairDir, false);
                    break;
                }
            } else if (gi.fence) {
                EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, 0, 6 / 16.0f, 10 / 16.0f, 1, 10 / 16.0f, ct,
                        cs, cb);
                const float armY0 = 11 / 16.0f, armY1 = 15 / 16.0f;
                if (gi.fenceArm[0])
                    EmitBox(tess, cellLight, bx, by, bz, 10 / 16.0f, armY0, 6 / 16.0f, 1, armY1, 10 / 16.0f,
                            ct, cs, cb);
                if (gi.fenceArm[1])
                    EmitBox(tess, cellLight, bx, by, bz, 0, armY0, 6 / 16.0f, 6 / 16.0f, armY1, 10 / 16.0f,
                            ct, cs, cb);
                if (gi.fenceArm[2])
                    EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 10 / 16.0f, 1, armY1, 10 / 16.0f,
                            ct, cs, cb);
                if (gi.fenceArm[3])
                    EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 0, 10 / 16.0f, armY1, 6 / 16.0f,
                            ct, cs, cb);
            } else if (gi.wall) {
                EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 5 / 16.0f, 11 / 16.0f, 1, 11 / 16.0f, ct,
                        cs, cb);
                const int h[4] = {gi.wallArm[0] >= 2 ? 16 : 14, gi.wallArm[1] >= 2 ? 16 : 14,
                                  gi.wallArm[2] >= 2 ? 16 : 14, gi.wallArm[3] >= 2 ? 16 : 14};
                if (gi.wallArm[0])
                    EmitBox(tess, cellLight, bx, by, bz, 11 / 16.0f, 0, 5 / 16.0f, 1, h[0] / 16.0f,
                            11 / 16.0f, ct, cs, cb);
                if (gi.wallArm[1])
                    EmitBox(tess, cellLight, bx, by, bz, 0, 0, 5 / 16.0f, 5 / 16.0f, h[1] / 16.0f, 11 / 16.0f,
                            ct, cs, cb);
                if (gi.wallArm[2])
                    EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 11 / 16.0f, 1, h[2] / 16.0f,
                            11 / 16.0f, ct, cs, cb);
                if (gi.wallArm[3])
                    EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 0, 11 / 16.0f, h[3] / 16.0f,
                            11 / 16.0f, ct, cs, cb);
            } else {
                float x0 = (1 + 2 * gi.cakeBites) / 16.0f;
                if (x0 < 0)
                    x0 = 0;
                if (x0 > 15 / 16.0f)
                    x0 = 15 / 16.0f;
                EmitBox(tess, cellLight, bx, by, bz, x0, 0, 1 / 16.0f, 15 / 16.0f, 8 / 16.0f, 15 / 16.0f, ct,
                        cs, cb);
            }
        }

        PackedLight BlockMesher::LightAt(const int p[3]) const {
            const std::array<int, 2> l = Sample(p[0], p[1], p[2]);
            return PackedLight{l[0] * 16, l[1] * 16};
        }

        CornerShade BlockMesher::ShadeCorner(const int center[3], const int normal[3], int axis1, int axis2,
                                             int d1, int d2, const int first[3], float shadeCenter,
                                             PackedLight lightCenter) const {
            int e1[3] = {center[0], center[1], center[2]};
            int e2[3] = {center[0], center[1], center[2]};
            int corner[3] = {center[0], center[1], center[2]};
            e1[axis1] += d1;
            e2[axis2] += d2;
            corner[axis1] += d1;
            corner[axis2] += d2;
            const bool open1 = !cache.Occ(e1[0] + normal[0], e1[1] + normal[1], e1[2] + normal[2]);
            const bool open2 = !cache.Occ(e2[0] + normal[0], e2[1] + normal[1], e2[2] + normal[2]);
            const int fallback[3] = {center[0] + first[0], center[1] + first[1], center[2] + first[2]};
            const int* diagonal = (!open1 && !open2) ? fallback : corner;
            const float cornerShade = AoShade(cache, library, diagonal[0], diagonal[1], diagonal[2]);
            const PackedLight cornerLight = LightAt(diagonal);
            const float a1 = AoShade(cache, library, e1[0], e1[1], e1[2]);
            const float a2 = AoShade(cache, library, e2[0], e2[1], e2[2]);
            const PackedLight blended = BlendCorner(LightAt(e1), LightAt(e2), cornerLight, lightCenter);
            return CornerShade{(a1 + a2 + cornerShade + shadeCenter) * 0.25f,
                               static_cast<float>(blended.block) / 16.0f,
                               static_cast<float>(blended.sky) / 16.0f};
        }

        void BlockMesher::ShadeFace(const int center[3], const int normal[3], int axis1, int axis2,
                                    const int first[3], float shadeCenter, PackedLight lightCenter,
                                    CornerShade out[2][2]) const {
            struct Tap {
                float ao;
                PackedLight light;
            };
            const auto tap = [&](const int p[3]) {
                return Tap{AoShade(cache, library, p[0], p[1], p[2]), LightAt(p)};
            };
            bool open1[2], open2[2];
            Tap t1[2], t2[2];
            for (int s = 0; s < 2; s++) {
                const int d = s ? 1 : -1;
                int e1[3] = {center[0], center[1], center[2]};
                int e2[3] = {center[0], center[1], center[2]};
                e1[axis1] += d;
                e2[axis2] += d;
                open1[s] = !cache.Occ(e1[0] + normal[0], e1[1] + normal[1], e1[2] + normal[2]);
                open2[s] = !cache.Occ(e2[0] + normal[0], e2[1] + normal[1], e2[2] + normal[2]);
                t1[s] = tap(e1);
                t2[s] = tap(e2);
            }
            bool haveFallback = false;
            Tap fallback{};
            for (int a = 0; a < 2; a++)
                for (int b = 0; b < 2; b++) {
                    Tap diagonal;
                    if (!open1[a] && !open2[b]) {
                        if (!haveFallback) {
                            const int f[3] = {center[0] + first[0], center[1] + first[1], center[2] + first[2]};
                            fallback = tap(f);
                            haveFallback = true;
                        }
                        diagonal = fallback;
                    } else {
                        int corner[3] = {center[0], center[1], center[2]};
                        corner[axis1] += a ? 1 : -1;
                        corner[axis2] += b ? 1 : -1;
                        diagonal = tap(corner);
                    }
                    const PackedLight blended = BlendCorner(t1[a].light, t2[b].light, diagonal.light, lightCenter);
                    out[a][b] = CornerShade{(t1[a].ao + t2[b].ao + diagonal.ao + shadeCenter) * 0.25f,
                                            static_cast<float>(blended.block) / 16.0f,
                                            static_cast<float>(blended.sky) / 16.0f};
                }
        }

        class BlockMesher::ModelEmitter {
        public:
            ModelEmitter(BlockMesher& mesher, BlockCtx& blk, const ModelEntry& entry)
                : m(mesher), blk(blk), entry(entry), models(mesher.library->Models()) {
                if (entry.offset)
                    ModelOffset(m.baseX + blk.x, m.baseZ + blk.z, entry.offset, offset[0], offset[1],
                                offset[2]);
            }

            void EmitAll() {
                if (entry.direct) {
                    for (const ModelQuad& q : entry.quads)
                        EmitQuad(q);
                    return;
                }
                JavaRandom rng(JavaPosSeed(m.baseX + blk.x, blk.y, m.baseZ + blk.z));
                for (const auto& group : entry.groups)
                    if (const ModelEntry::Choice* pick = PickVariant(group, rng))
                        for (const ModelQuad& q : models.Variant(pick->variant))
                            EmitQuad(q);
            }

        private:
            struct Corners {
                float ao[2][2];
                float block[2][2];
                float sky[2][2];
            };

            struct QuadTexture {
                int tile = 0;
                int baseTile = 0;
                BlockLibrary::TileAlpha alpha;
                bool glass = false;
                float uv[4][2];
            };

            static const ModelEntry::Choice* PickVariant(const std::vector<ModelEntry::Choice>& group,
                                                         JavaRandom& rng) {
                if (group.empty())
                    return nullptr;
                if (group.size() == 1)
                    return &group[0];
                int total = 0;
                for (const auto& choice : group)
                    total += choice.weight;
                int roll = rng.NextInt(total > 0 ? total : 1);
                for (const auto& choice : group) {
                    roll -= choice.weight;
                    if (roll < 0)
                        return &choice;
                }
                return &group[0];
            }

            uint16_t NeighbourId(int dx, int dy, int dz) const {
                return m.cache.Get(blk.x + dx, blk.y + dy, blk.z + dz);
            }

            bool SameGlass(uint16_t neighbour, int baseTile) const {
                return neighbour != 0 && (neighbour == blk.id || m.library->Look(neighbour).side == baseTile);
            }

            bool ResolveTexture(const ModelQuad& q, QuadTexture& tex) const {
                if (!q.tile && (q.tex == 0xFFFF || q.tex >= models.tiles.size()))
                    return false;
                tex.tile = q.tile ? q.tile : models.tiles[q.tex];
                if (!tex.tile)
                    return false;
                tex.baseTile = tex.tile;
                tex.alpha = m.library->AlphaOf(tex.tile);
                tex.glass = m.library->IsGlassBase(tex.tile);
                for (int k = 0; k < 4; k++) {
                    tex.uv[k][0] = q.uv[k][0];
                    tex.uv[k][1] = q.uv[k][1];
                }
                return true;
            }

            bool GlassEdgeExposed(const ModelQuad& q, const float* from, const float* to, bool negate,
                                  int baseTile) const {
                int d[3];
                for (int a = 0; a < 3; a++) {
                    const float diff = to[a] - from[a];
                    d[a] = std::fabs(diff) < 1e-3f ? 0 : (diff > 0 ? 1 : -1);
                    if (negate)
                        d[a] = -d[a];
                }
                if (!SameGlass(NeighbourId(d[0], d[1], d[2]), baseTile))
                    return true;
                const int* face = kFaceNormal[q.dir];
                return SameGlass(NeighbourId(d[0] + face[0], d[1] + face[1], d[2] + face[2]), baseTile);
            }

            void ConnectGlass(const ModelQuad& q, QuadTexture& tex) const {
                const int mode = m.library->GlassMode();
                if (!mode || !tex.glass)
                    return;
                float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
                for (const auto& corner : q.uv) {
                    umin = std::min(umin, corner[0]);
                    umax = std::max(umax, corner[0]);
                    vmin = std::min(vmin, corner[1]);
                    vmax = std::max(vmax, corner[1]);
                }
                int ia = -1, ib = -1, ic = -1;
                for (int k = 0; k < 4; k++) {
                    const bool atU0 = std::fabs(q.uv[k][0] - umin) < 0.01f;
                    const bool atU16 = std::fabs(q.uv[k][0] - umax) < 0.01f;
                    const bool atV0 = std::fabs(q.uv[k][1] - vmin) < 0.01f;
                    const bool atV16 = std::fabs(q.uv[k][1] - vmax) < 0.01f;
                    if (atU0 && atV0)
                        ia = k;
                    else if (atU16 && atV0)
                        ib = k;
                    else if (atU0 && atV16)
                        ic = k;
                }
                if (ia < 0 || ib < 0 || ic < 0) {
                    if (mode == 3)
                        tex.tile = m.library->GlassTile(tex.tile, mode, 0);
                    return;
                }
                const bool edgeL = umin < 0.01f, edgeR = umax > 15.99f, edgeT = vmin < 0.01f,
                           edgeB = vmax > 15.99f;
                int mask = 0;
                if (edgeL && GlassEdgeExposed(q, q.pos[ia], q.pos[ib], true, tex.baseTile))
                    mask |= 1;
                if (edgeR && GlassEdgeExposed(q, q.pos[ia], q.pos[ib], false, tex.baseTile))
                    mask |= 2;
                if (edgeT && GlassEdgeExposed(q, q.pos[ia], q.pos[ic], true, tex.baseTile))
                    mask |= 4;
                if (edgeB && GlassEdgeExposed(q, q.pos[ia], q.pos[ic], false, tex.baseTile))
                    mask |= 8;
                tex.tile = m.library->GlassTile(tex.tile, mode, mask);
                if (mode != 1 && mode != 2)
                    return;
                const float inset = BlockLibrary::GlassInset() * 16.0f;
                for (auto& corner : tex.uv) {
                    float& u = corner[0];
                    float& v = corner[1];
                    if (edgeL && !(mask & 1) && std::fabs(u - umin) < 0.01f)
                        u = inset;
                    else if (edgeR && !(mask & 2) && std::fabs(u - umax) < 0.01f)
                        u = 16.0f - inset;
                    if (edgeT && !(mask & 4) && std::fabs(v - vmin) < 0.01f)
                        v = inset;
                    else if (edgeB && !(mask & 8) && std::fabs(v - vmax) < 0.01f)
                        v = 16.0f - inset;
                }
            }

            static bool LiesOnFacePlane(const ModelQuad& q) {
                const int dir = q.dir;
                const int axis = dir < 2 ? 1 : (dir < 4 ? 2 : 0);
                const float plane = (dir & 1) ? 1.0f : 0.0f;
                for (const auto& corner : q.pos)
                    if (std::fabs(corner[axis] - plane) > 1e-4f)
                        return false;
                return true;
            }

            bool Hidden(const ModelQuad& q, const QuadTexture& tex, bool flush) const {
                const bool seeThrough = tex.alpha.translucent || tex.glass;
                const int glassMode = m.library->GlassMode();
                if (q.cull >= 0) {
                    const int* nb = kFaceNormal[q.cull];
                    const uint16_t neighbour = NeighbourId(nb[0], nb[1], nb[2]);
                    if (neighbour != 0 && m.cache.Occ(blk.x + nb[0], blk.y + nb[1], blk.z + nb[2]))
                        return true;
                    if (neighbour != 0 && gCullInnerLeaves.load(std::memory_order_relaxed) && IsLeafBlock(blk.id) &&
                        IsLeafBlock(neighbour))
                        return true;
                    const bool same =
                        neighbour == blk.id ||
                        (glassMode && neighbour != 0 && m.library->Look(neighbour).side == tex.baseTile);
                    if (same && seeThrough)
                        return true;
                    return false;
                }
                if (!flush || !seeThrough)
                    return false;
                const int* nrm = kFaceNormal[q.dir];
                const uint16_t neighbour = NeighbourId(nrm[0], nrm[1], nrm[2]);
                return neighbour != 0 &&
                       (neighbour == blk.id ||
                        (glassMode && tex.glass && m.library->Look(neighbour).side == tex.baseTile));
            }

            void BuildCorners(int dir, bool flush, Corners& out) const {
                const int* nrm = kFaceNormal[dir];
                int axis1, axis2, first[3];
                FaceAxes(nrm, axis1, axis2);
                FirstCornerOffset(nrm, first);
                const int center[3] = {blk.x + (flush ? nrm[0] : 0), blk.y + (flush ? nrm[1] : 0),
                                       blk.z + (flush ? nrm[2] : 0)};
                const float shadeCenter = AoShade(m.cache, m.library, center[0], center[1], center[2]);
                const PackedLight lightCenter = m.LightAt(center);
                CornerShade shades[2][2];
                m.ShadeFace(center, nrm, axis1, axis2, first, shadeCenter, lightCenter, shades);
                for (int s1 = 0; s1 < 2; s1++)
                    for (int s2 = 0; s2 < 2; s2++) {
                        const CornerShade& corner = shades[s1][s2];
                        out.ao[s1][s2] = corner.ao;
                        out.block[s1][s2] = corner.block;
                        out.sky[s1][s2] = corner.sky;
                    }
            }

            const Corners& CornersFor(int dir, bool flush) {
                const int key = dir * 2 + (flush ? 1 : 0);
                if (!have[key]) {
                    have[key] = true;
                    BuildCorners(dir, flush, corners[key]);
                }
                return corners[key];
            }

            std::array<float, 3> BaseColor(const ModelQuad& q, int tile) {
                std::string tilePath;
                const bool modelTexture = !q.tile && q.tex < models.Textures().size();
                if (!modelTexture)
                    tilePath = m.library->TilePathOf(tile);
                const std::string& texture = modelTexture ? models.Textures()[q.tex] : tilePath;
                std::array<float, 3> base = {1, 1, 1};
                if (texture.contains("bamboo_leaf") || texture.contains("bamboo_small_leaf"))
                    base = {0.63f, 0.63f, 0.63f};
                if (q.tint < 0 || q.tile)
                    return base;
                const auto tint = m.BiomeTintFor(blk, texture, tile, q.dir == 1);
                if (tint[3])
                    return {tint[0], tint[1], tint[2]};
                const BlockAppearance* app = blk.app;
                if (app && (app->tintTop || app->tintSide || app->tintBottom))
                    return {app->tintR, app->tintG, app->tintB};
                return base;
            }

            void EmitQuad(const ModelQuad& q) {
                QuadTexture tex;
                if (!ResolveTexture(q, tex))
                    return;
                ConnectGlass(q, tex);
                const bool flush = LiesOnFacePlane(q);
                if (Hidden(q, tex, flush))
                    return;
                const int dir = q.dir;
                int axis1, axis2;
                FaceAxes(kFaceNormal[dir], axis1, axis2);
                const Corners& light = CornersFor(dir, flush);
                const std::array<float, 3> base = BaseColor(q, tex.tile);
                float u0, v0, u1, v1;
                BlockLibrary::TileUV(tex.tile, u0, v0, u1, v1);
                const float dirShade = q.shade ? kFaceShade[q.shadeDir] : 1.0f;
                float pos[4][3], col[4][3], uv[4][2], lt[4][2], vertexAo[4];
                for (int k = 0; k < 4; k++) {
                    const int i = kVertexOrder[k];
                    pos[k][0] = blk.bx + q.pos[i][0] + offset[0];
                    pos[k][1] = blk.by + q.pos[i][1] + offset[1];
                    pos[k][2] = blk.bz + q.pos[i][2] + offset[2];
                    const float p1 = std::clamp(q.pos[i][axis1], 0.0f, 1.0f);
                    const float p2 = std::clamp(q.pos[i][axis2], 0.0f, 1.0f);
                    float ao = 0, bl = 0, sk = 0;
                    for (int s1 = 0; s1 < 2; s1++)
                        for (int s2 = 0; s2 < 2; s2++) {
                            const float w = (s1 ? p1 : 1 - p1) * (s2 ? p2 : 1 - p2);
                            ao += w * light.ao[s1][s2];
                            bl += w * light.block[s1][s2];
                            sk += w * light.sky[s1][s2];
                        }
                    if (q.light) {
                        bl = std::max(bl, static_cast<float>(q.light));
                        sk = std::max(sk, static_cast<float>(q.light));
                    }
                    vertexAo[k] = ao;
                    const float shade = ao * dirShade;
                    col[k][0] = base[0] * shade;
                    col[k][1] = base[1] * shade;
                    col[k][2] = base[2] * shade;
                    lt[k][0] = bl;
                    lt[k][1] = sk;
                    uv[k][0] = u0 + (tex.uv[i][0] / 16.0f) * (u1 - u0);
                    uv[k][1] = v0 + (tex.uv[i][1] / 16.0f) * (v1 - v0);
                }
                m.tess.SetLayer(tex.alpha.translucent ? RenderLayer::TranslucentCull
                                : tex.alpha.holes     ? RenderLayer::CutoutCull
                                                      : RenderLayer::Solid);
                m.tess.AddQuad(pos, col, uv, false, lt, vertexAo, dirShade);
            }

            BlockMesher& m;
            BlockCtx& blk;
            const ModelEntry& entry;
            const BlockModels& models;
            float offset[3] = {0, 0, 0};
            bool have[12] = {};
            Corners corners[12];
        };

        void BlockMesher::EmitModel(BlockCtx& blk) {
            ModelEmitter(*this, blk, *blk.modelEntry).EmitAll();
        }

        void BlockMesher::EmitFlat(BlockCtx& blk) {
            const int x = blk.x;
            const int y = blk.y;
            const int z = blk.z;
            const BlockAppearance* app = blk.app;
            const CellLight& cellLight = blk.cellLight;
            const float bx = blk.bx;
            const float by = blk.by;
            const float bz = blk.bz;
            const GeomInfo& gi = *blk.gi;
            const uint32_t bhash = blk.bhash;

            const std::vector<int>& fv = app->varTop.empty() ? app->varSide : app->varTop;
            int tile = PickTile(fv, app->top ? app->top : app->side, bhash);
            float baseRGB[3];
            bool maskGrey = !library || library->SaturationOf(tile) < 0.25f;
            if (app->hasCarriedRGB && maskGrey) {
                baseRGB[0] = app->carriedR;
                baseRGB[1] = app->carriedG;
                baseRGB[2] = app->carriedB;
            } else {
                auto tc = BiomeTintFor(blk, app->texTop.empty() ? app->texSide : app->texTop, tile, true);
                baseRGB[0] = tc[0];
                baseRGB[1] = tc[1];
                baseRGB[2] = tc[2];
                if (!tc[3]) {
                    if (app->tintTop) {
                        baseRGB[0] = app->tintR;
                        baseRGB[1] = app->tintG;
                        baseRGB[2] = app->tintB;
                    } else {
                        baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                    }
                }
            }
            float u0, v0uv, u1, v1uv;
            BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
            float h = 0.0625f;
            if (gi.snowH >= 0)
                h = (gi.snowH + 1) * 2.0f / 16.0f;
            else if (app->texTop.contains("snow") || app->texSide.contains("snow"))
                h = 0.125f;
            float inset = 0;
            if (gi.growth >= 0) {
                float cov = 0.5f + 0.5f * (gi.growth + 1) / 8.0f;
                inset = (1.0f - cov) * 0.5f;
            }
            float fx0 = bx + inset, fx1 = bx + 1 - inset;
            float fz0 = bz + inset, fz1 = bz + 1 - inset;
            float top[4][3] = {
                {fx0, by + h, fz0}, {fx1, by + h, fz0}, {fx1, by + h, fz1}, {fx0, by + h, fz1}};
            float uv[4][2] = {{u0, v0uv}, {u1, v0uv}, {u1, v1uv}, {u0, v1uv}};
            {
                int rot = (bhash >> 2) & 3;
                float base[4][2] = {{u0, v0uv}, {u1, v0uv}, {u1, v1uv}, {u0, v1uv}};
                for (int i = 0; i < 4; i++) {
                    uv[i][0] = base[(i + rot) & 3][0];
                    uv[i][1] = base[(i + rot) & 3][1];
                }
            }
            float col[4][3];
            for (auto& i : col) {
                i[0] = baseRGB[0] * kShadeTop;
                i[1] = baseRGB[1] * kShadeTop;
                i[2] = baseRGB[2] * kShadeTop;
            }
            tess.AddQuad(top, col, uv, false, cellLight.block, cellLight.sky);
            float sideUv[4][2] = {{u0, v1uv}, {u1, v1uv}, {u1, v1uv}, {u0, v1uv}};
            (void)sideUv;
            float px[4][3] = {{fx1, by + h, fz0}, {fx1, by, fz0}, {fx1, by, fz1}, {fx1, by + h, fz1}};
            float nx[4][3] = {{fx0, by + h, fz1}, {fx0, by, fz1}, {fx0, by, fz0}, {fx0, by + h, fz0}};
            float pz[4][3] = {{fx0, by + h, fz1}, {fx0, by, fz1}, {fx1, by, fz1}, {fx1, by + h, fz1}};
            float nz[4][3] = {{fx1, by + h, fz0}, {fx1, by, fz0}, {fx0, by, fz0}, {fx0, by + h, fz0}};
            float scol[4][3];
            for (auto& i : scol) {
                i[0] = baseRGB[0] * kShadeX;
                i[1] = baseRGB[1] * kShadeX;
                i[2] = baseRGB[2] * kShadeX;
            }
            float uvS[4][2] = {{u0, v0uv}, {u0, v0uv}, {u1, v0uv}, {u1, v0uv}};
            if (cache.Get(x + 1, y, z) == 0)
                tess.AddQuad(px, scol, uvS, false, cellLight.block, cellLight.sky);
            if (cache.Get(x - 1, y, z) == 0)
                tess.AddQuad(nx, scol, uvS, false, cellLight.block, cellLight.sky);
            if (cache.Get(x, y, z + 1) == 0)
                tess.AddQuad(pz, scol, uvS, false, cellLight.block, cellLight.sky);
            if (cache.Get(x, y, z - 1) == 0)
                tess.AddQuad(nz, scol, uvS, false, cellLight.block, cellLight.sky);
        }

        void BlockMesher::EmitWall(BlockCtx& blk) {
            const int x = blk.x;
            const int y = blk.y;
            const int z = blk.z;
            const BlockAppearance* app = blk.app;
            const CellLight& cellLight = blk.cellLight;
            const float bx = blk.bx;
            const float by = blk.by;
            const float bz = blk.bz;

            int tile = app->side ? app->side : app->top;
            std::string tex = app->texSide.empty() ? app->texTop : app->texSide;
            auto tc = BiomeTintFor(blk, tex, tile, false);
            float baseRGB[3] = {tc[0], tc[1], tc[2]};
            if (!tc[3]) {
                if (app->tintSide) {
                    baseRGB[0] = app->tintR;
                    baseRGB[1] = app->tintG;
                    baseRGB[2] = app->tintB;
                } else {
                    baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                }
            }
            float u0, v0uv, u1, v1uv;
            BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
            float uvw[4][2] = {{u0, v1uv}, {u0, v0uv}, {u1, v0uv}, {u1, v1uv}};
            float colW[4][3];
            for (auto& i : colW) {
                i[0] = baseRGB[0];
                i[1] = baseRGB[1];
                i[2] = baseRGB[2];
            }
            constexpr float kInset = 1.0f / 16.0f;
            int quads = 0;
            if (cache.Occ(x + 1, y, z)) {
                float q[4][3] = {{bx + 1 - kInset, by, bz},
                                 {bx + 1 - kInset, by + 1, bz},
                                 {bx + 1 - kInset, by + 1, bz + 1},
                                 {bx + 1 - kInset, by, bz + 1}};
                float c[4][3];
                for (int i = 0; i < 4; i++) {
                    c[i][0] = colW[i][0] * kShadeX;
                    c[i][1] = colW[i][1] * kShadeX;
                    c[i][2] = colW[i][2] * kShadeX;
                }
                tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                quads++;
            }
            if (cache.Occ(x - 1, y, z)) {
                float q[4][3] = {{bx + kInset, by, bz + 1},
                                 {bx + kInset, by + 1, bz + 1},
                                 {bx + kInset, by + 1, bz},
                                 {bx + kInset, by, bz}};
                float c[4][3];
                for (int i = 0; i < 4; i++) {
                    c[i][0] = colW[i][0] * kShadeX;
                    c[i][1] = colW[i][1] * kShadeX;
                    c[i][2] = colW[i][2] * kShadeX;
                }
                tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                quads++;
            }
            if (cache.Occ(x, y, z + 1)) {
                float q[4][3] = {{bx + 1, by, bz + 1 - kInset},
                                 {bx + 1, by + 1, bz + 1 - kInset},
                                 {bx, by + 1, bz + 1 - kInset},
                                 {bx, by, bz + 1 - kInset}};
                float c[4][3];
                for (int i = 0; i < 4; i++) {
                    c[i][0] = colW[i][0] * kShadeZ;
                    c[i][1] = colW[i][1] * kShadeZ;
                    c[i][2] = colW[i][2] * kShadeZ;
                }
                tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                quads++;
            }
            if (cache.Occ(x, y, z - 1)) {
                float q[4][3] = {{bx, by, bz + kInset},
                                 {bx, by + 1, bz + kInset},
                                 {bx + 1, by + 1, bz + kInset},
                                 {bx + 1, by, bz + kInset}};
                float c[4][3];
                for (int i = 0; i < 4; i++) {
                    c[i][0] = colW[i][0] * kShadeZ;
                    c[i][1] = colW[i][1] * kShadeZ;
                    c[i][2] = colW[i][2] * kShadeZ;
                }
                tess.AddQuad(q, c, uvw, false, cellLight.block, cellLight.sky);
                quads++;
            }
            if (quads == 0) {
                float va[4][3] = {
                    {bx, by, bz + 1}, {bx, by + 1, bz + 1}, {bx + 1, by + 1, bz}, {bx + 1, by, bz}};
                float vb[4][3] = {
                    {bx, by, bz}, {bx, by + 1, bz}, {bx + 1, by + 1, bz + 1}, {bx + 1, by, bz + 1}};
                tess.AddQuad(va, colW, uvw, false, cellLight.block, cellLight.sky);
                tess.AddQuad(vb, colW, uvw, false, cellLight.block, cellLight.sky);
            }
        }

        void BlockMesher::EmitFire(BlockCtx& blk) {
            const int x = blk.x;
            const int y = blk.y;
            const int z = blk.z;
            const BlockAppearance* app = blk.app;
            const CellLight& cellLight = blk.cellLight;
            const float bx = blk.bx;
            const float by = blk.by;
            const float bz = blk.bz;
            const uint32_t bhash = blk.bhash;

            const uint16_t nIds[5] = {cache.Get(x, y, z - 1), cache.Get(x + 1, y, z), cache.Get(x, y, z + 1),
                                      cache.Get(x - 1, y, z), cache.Get(x, y + 1, z)};
            bool flag[5];
            bool any = false;
            for (int i = 0; i < 5; i++) {
                flag[i] = nIds[i] != 0 && GeomFor(nIds[i]).flammable;
                any = any || flag[i];
            }
            const float white[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
            const float fireBlock = 15.0f;
            auto tileFor = [&](int variant) {
                return variant ? (app->bottom ? app->bottom : app->top) : app->top;
            };
            auto rotate = [](float p[3], int axis, float deg, const float o[3], bool rescale) {
                const float r = deg * 3.14159265f / 180.0f, c = std::cos(r), sn = std::sin(r);
                const int a = (axis + 1) % 3, b = (axis + 2) % 3;
                const float da = p[a] - o[a], db = p[b] - o[b];
                float na = da * c - db * sn, nb = da * sn + db * c;
                if (rescale) {
                    na /= std::fabs(c);
                    nb /= std::fabs(c);
                }
                p[a] = o[a] + na;
                p[b] = o[b] + nb;
            };
            auto plane = [&](const float A[3], const float R[3], const float Uv[3], int axis, float deg,
                             const float origin[3], int variant, bool flipU) {
                const int tile = tileFor(variant);
                float u0, v0, u1, v1;
                BlockLibrary::TileUV(tile, u0, v0, u1, v1);
                if (flipU)
                    std::swap(u0, u1);
                float pts[4][3];
                for (int k = 0; k < 3; k++) {
                    pts[0][k] = A[k];
                    pts[1][k] = A[k] + R[k];
                    pts[2][k] = A[k] + R[k] + Uv[k];
                    pts[3][k] = A[k] + Uv[k];
                }
                float out[4][3];
                for (int i = 0; i < 4; i++) {
                    if (axis >= 0)
                        rotate(pts[i], axis, deg, origin, true);
                    out[i][0] = bx + pts[i][0] / 16.0f;
                    out[i][1] = by + pts[i][1] / 16.0f;
                    out[i][2] = bz + pts[i][2] / 16.0f;
                }
                float flipped[4][3];
                for (int i = 0; i < 4; i++)
                    for (int k = 0; k < 3; k++)
                        flipped[i][k] = out[(4 - i) % 4][k];
                float uv[4][2] = {{u0, v1}, {u0, v0}, {u1, v0}, {u1, v1}};
                tess.AddQuad(flipped, white, uv, false, fireBlock, cellLight.sky);
            };
            tess.SetLayer(RenderLayer::CutoutCull);
            const int pick0 = static_cast<int>(bhash & 1), pick1 = static_cast<int>((bhash >> 1) & 1);
            const float none[3] = {0, 0, 0}, o8[3] = {8, 8, 8};
            const float up224[3] = {0, 22.4f, 0};
            if (!any) {
                const float sA[3] = {0, 0, 8.8f}, sR[3] = {16, 0, 0};
                const float nA[3] = {16, 0, 7.2f}, nR[3] = {-16, 0, 0};
                const float wA[3] = {8.8f, 0, 0}, wR[3] = {0, 0, 16};
                const float eA[3] = {7.2f, 0, 16}, eR[3] = {0, 0, -16};
                plane(sA, sR, up224, 0, -22.5f, o8, pick0, false);
                plane(nA, nR, up224, 0, 22.5f, o8, pick1, false);
                plane(wA, wR, up224, 2, -22.5f, o8, pick1, false);
                plane(eA, eR, up224, 2, 22.5f, o8, pick0, false);
            }
            const float eps = 0.01f, far = 15.99f;
            for (int d = 0; d < 4; d++) {
                if (!flag[d])
                    continue;
                float A1[3], R1[3], A2[3], R2[3];
                if (d == 0) {
                    A1[0] = 0;
                    A1[1] = 0;
                    A1[2] = eps;
                    R1[0] = 16;
                    R1[1] = 0;
                    R1[2] = 0;
                    A2[0] = 16;
                    A2[1] = 0;
                    A2[2] = eps;
                    R2[0] = -16;
                    R2[1] = 0;
                    R2[2] = 0;
                } else if (d == 2) {
                    A1[0] = 0;
                    A1[1] = 0;
                    A1[2] = far;
                    R1[0] = 16;
                    R1[1] = 0;
                    R1[2] = 0;
                    A2[0] = 16;
                    A2[1] = 0;
                    A2[2] = far;
                    R2[0] = -16;
                    R2[1] = 0;
                    R2[2] = 0;
                } else if (d == 1) {
                    A1[0] = far;
                    A1[1] = 0;
                    A1[2] = 0;
                    R1[0] = 0;
                    R1[1] = 0;
                    R1[2] = 16;
                    A2[0] = far;
                    A2[1] = 0;
                    A2[2] = 16;
                    R2[0] = 0;
                    R2[1] = 0;
                    R2[2] = -16;
                } else {
                    A1[0] = eps;
                    A1[1] = 0;
                    A1[2] = 0;
                    R1[0] = 0;
                    R1[1] = 0;
                    R1[2] = 16;
                    A2[0] = eps;
                    A2[1] = 0;
                    A2[2] = 16;
                    R2[0] = 0;
                    R2[1] = 0;
                    R2[2] = -16;
                }
                const bool alt = ((bhash >> (2 + d)) & 1) != 0;
                plane(A1, R1, up224, -1, 0, none, pick0, alt);
                plane(A2, R2, up224, -1, 0, none, pick1, alt);
            }
            if (flag[4]) {
                const float uvec[3] = {0, 0, 16}, rvec[3] = {16, 0, 0};
                const float a[3] = {0, 16, 0};
                const float oA[3] = {16, 16, 8}, oB[3] = {0, 16, 8};
                plane(a, rvec, uvec, 2, 22.5f, oA, pick0, false);
                plane(a, rvec, uvec, 2, -22.5f, oB, pick1, false);
            }
            tess.SetLayer(RenderLayer::Cutout);
        }

        void BlockMesher::EmitCross(BlockCtx& blk) {
            const int x = blk.x;
            const int y = blk.y;
            const int z = blk.z;
            const BlockAppearance* app = blk.app;
            const CellLight& cellLight = blk.cellLight;
            const float bx = blk.bx;
            const float by = blk.by;
            const float bz = blk.bz;
            const GeomInfo& gi = *blk.gi;
            const uint32_t bhash = blk.bhash;

            const std::vector<int>& cvv = app->varSide.empty() ? app->varTop : app->varSide;
            int tile = PickTile(cvv, app->side ? app->side : app->top, bhash);
            std::string tex = app->texSide.empty() ? app->texTop : app->texSide;
            if (gi.kelp && GeomFor(cache.Get(x, y + 1, z)).kelp) {
                tile = PickTile(app->varTop, app->top, static_cast<uint32_t>(y) & 3u);
                tex = app->texTop;
            }
            if (gi.dblPlant) {
                if (gi.upper && app->top) {
                    tile = PickTile(app->varTop, app->top, bhash);
                    tex = app->texTop;
                }
            }
            auto tc = BiomeTintFor(blk, tex, tile, true);
            float baseRGB[3] = {tc[0], tc[1], tc[2]};
            if (!tc[3]) {
                if (app->tintTop) {
                    baseRGB[0] = app->tintR;
                    baseRGB[1] = app->tintG;
                    baseRGB[2] = app->tintB;
                } else {
                    baseRGB[0] = baseRGB[1] = baseRGB[2] = 1.0f;
                }
            }
            float u0, v0uv, u1, v1uv;
            BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
            if (!gi.kelp && ((bhash >> 3) & 1)) {
                float t = u0;
                u0 = u1;
                u1 = t;
            }
            float uvc[4][2] = {{u0, v1uv}, {u0, v0uv}, {u1, v0uv}, {u1, v1uv}};
            float colC[4][3];
            for (auto& i : colC) {
                i[0] = baseRGB[0];
                i[1] = baseRGB[1];
                i[2] = baseRGB[2];
            }
            float ox, oy, oz;
            PlantOffset(baseX + x, baseZ + z, tex, gi.dblPlant, ox, oy, oz);
            const float lo = 0.05f, hi = 0.95f;
            const float px = bx + ox, py = by + oy, pz = bz + oz;
            float va[4][3] = {{px + lo, py, pz + hi},
                              {px + lo, py + 1, pz + hi},
                              {px + hi, py + 1, pz + lo},
                              {px + hi, py, pz + lo}};
            float vb[4][3] = {{px + lo, py, pz + lo},
                              {px + lo, py + 1, pz + lo},
                              {px + hi, py + 1, pz + hi},
                              {px + hi, py, pz + hi}};
            tess.AddQuad(va, colC, uvc, false, cellLight.block, cellLight.sky);
            tess.AddQuad(vb, colC, uvc, false, cellLight.block, cellLight.sky);
        }

        class BlockMesher::CubeEmitter {
        public:
            CubeEmitter(BlockMesher& mesher, BlockCtx& blk)
                : m(mesher), blk(blk), x(blk.x), y(blk.y), z(blk.z),
                  liquidKind(blk.app ? LiquidKind(mesher.library, blk.id) : 0), water(liquidKind != 0),
                  translucent(liquidKind == 1 || (liquidKind == 0 && mesher.textured &&
                                                  IsTranslucentId(mesher.library, blk.id))) {}

            void Emit() {
                BuildShape();
                ChooseSurfaces();
                tess().SetLayer(translucent ? RenderLayer::Translucent : RenderLayer::Solid);
                if (water)
                    EmitWaterFaces();
                else
                    EmitSolidFaces();
            }

        private:
            struct Face {
                int nx, ny, nz;
                int tile;
                float r, g, b;
                bool tinted;
                float shade;
                const int (*cells)[3];
                const float (*verts)[3];
            };

            struct Style {
                bool tinted = false;
                float r = 1, g = 1, b = 1;
            };

            struct Overlay {
                int tile = 0;
                float r = 1, g = 1, b = 1;
            };

            Tessellator& tess() const { return m.tess; }
            const BlockCache& cache() const { return m.cache; }
            BlockLibrary* library() const { return m.library; }

            bool IsGlassTile(int tile) const { return library() && library()->IsGlassBase(tile); }

            void BuildShape() {
                const float bx = blk.bx, by = blk.by, bz = blk.bz;
                float yBot = by, yTop = by + 1.0f;
                float h[4] = {-1, -1, -1, -1};
                if (water) {
                    constexpr float kLift = 0.001f;
                    h[0] = by + WaterCornerHeight(cache(), library(), liquidKind, y, x, z) - kLift;
                    h[1] = by + WaterCornerHeight(cache(), library(), liquidKind, y, x + 1, z) - kLift;
                    h[2] = by + WaterCornerHeight(cache(), library(), liquidKind, y, x, z + 1) - kLift;
                    h[3] = by + WaterCornerHeight(cache(), library(), liquidKind, y, x + 1, z + 1) - kLift;
                    yTop = std::max(std::max(h[0], h[1]), std::max(h[2], h[3]));
                } else if (blk.gi->slab) {
                    if (blk.gi->slabTop)
                        yBot = by + 0.5f;
                    else
                        yTop = by + 0.5f;
                }
                const float c00 = water ? h[0] : yTop;
                const float c10 = water ? h[1] : yTop;
                const float c01 = water ? h[2] : yTop;
                const float c11 = water ? h[3] : yTop;
                const float topV[4][3] = {
                    {bx, c00, bz}, {bx + 1, c10, bz}, {bx + 1, c11, bz + 1}, {bx, c01, bz + 1}};
                const float bottomV[4][3] = {
                    {bx, yBot, bz + 1}, {bx + 1, yBot, bz + 1}, {bx + 1, yBot, bz}, {bx, yBot, bz}};
                const float posX[4][3] = {
                    {bx + 1, c10, bz}, {bx + 1, yBot, bz}, {bx + 1, yBot, bz + 1}, {bx + 1, c11, bz + 1}};
                const float negX[4][3] = {
                    {bx, c01, bz + 1}, {bx, yBot, bz + 1}, {bx, yBot, bz}, {bx, c00, bz}};
                const float posZ[4][3] = {
                    {bx, c01, bz + 1}, {bx + 1, c11, bz + 1}, {bx + 1, yBot, bz + 1}, {bx, yBot, bz + 1}};
                const float negZ[4][3] = {
                    {bx + 1, c10, bz}, {bx, c00, bz}, {bx, yBot, bz}, {bx + 1, yBot, bz}};
                std::memcpy(top, topV, sizeof top);
                std::memcpy(bottom, bottomV, sizeof bottom);
                std::memcpy(px, posX, sizeof px);
                std::memcpy(nx, negX, sizeof nx);
                std::memcpy(pz, posZ, sizeof pz);
                std::memcpy(nz, negZ, sizeof nz);
                const int cellTop[4][3] = {
                    {x, y + 1, z}, {x + 1, y + 1, z}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}};
                const int cellBottom[4][3] = {{x, y, z + 1}, {x + 1, y, z + 1}, {x + 1, y, z}, {x, y, z}};
                const int cellPx[4][3] = {
                    {x + 1, y + 1, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}};
                const int cellNx[4][3] = {{x, y + 1, z + 1}, {x, y, z + 1}, {x, y, z}, {x, y + 1, z}};
                const int cellPz[4][3] = {
                    {x, y + 1, z + 1}, {x + 1, y + 1, z + 1}, {x + 1, y, z + 1}, {x, y, z + 1}};
                const int cellNz[4][3] = {{x + 1, y + 1, z}, {x, y + 1, z}, {x, y, z}, {x + 1, y, z}};
                std::memcpy(cTop, cellTop, sizeof cTop);
                std::memcpy(cBottom, cellBottom, sizeof cBottom);
                std::memcpy(cPx, cellPx, sizeof cPx);
                std::memcpy(cNx, cellNx, sizeof cNx);
                std::memcpy(cPz, cellPz, sizeof cPz);
                std::memcpy(cNz, cellNz, sizeof cNz);
            }

            bool Shut(int dx, int dy, int dz) const {
                return cache().Get(x + dx, y + dy, z + dz) != 0 && cache().Occ(x + dx, y + dy, z + dz);
            }

            void ChooseSurfaces() {
                const BlockAppearance* app = blk.app;
                Style base;
                static const std::string kNoTexture;
                const std::string* topTexture = &kNoTexture;
                const std::string* sideTexture = &kNoTexture;
                const std::string* bottomTexture = &kNoTexture;
                if (app) {
                    tileTop = PickTile(app->varTop, app->top, blk.bhash);
                    tileSide = PickTile(app->varSide, app->side, blk.bhash);
                    tileBottom = PickTile(app->varBottom, app->bottom, blk.bhash);
                    base = {app->tintTop, app->tintR, app->tintG, app->tintB};
                    known = (tileTop | tileSide | tileBottom) != 0;
                    topTexture = &app->texTop;
                    sideTexture = &app->texSide;
                    bottomTexture = &app->texBottom;
                }
                styleTop = styleSide = styleBottom = base;
                if (app) {
                    styleSide.tinted = app->tintSide;
                    styleBottom.tinted = app->tintBottom;
                }
                const bool sideOpen = !Shut(1, 0, 0) || !Shut(-1, 0, 0) || !Shut(0, 0, 1) || !Shut(0, 0, -1);
                const bool needTop = !Shut(0, 1, 0) || sideOpen;
                const bool needBottom = !Shut(0, -1, 0);
                if (known) {
                    static constexpr std::array<float, 4> kSkip{0, 0, 0, 0};
                    ApplyBiomeTint(styleTop,
                                   needTop ? m.BiomeTintFor(blk, *topTexture, tileTop, true) : kSkip);
                    ApplyBiomeTint(styleSide,
                                   sideOpen ? m.BiomeTintFor(blk, *sideTexture, tileSide, false) : kSkip);
                    ApplyBiomeTint(styleBottom, needBottom
                                                    ? m.BiomeTintFor(blk, *bottomTexture, tileBottom, false)
                                                    : kSkip);
                }
                LogGrassOnce();
            }

            static void ApplyBiomeTint(Style& style, const std::array<float, 4>& tint) {
                if (!tint[3])
                    return;
                style.r = tint[0];
                style.g = tint[1];
                style.b = tint[2];
                style.tinted = true;
            }

            void LogGrassOnce() const {
                const BlockAppearance* app = blk.app;
                if (!app || app->overlayFrac <= 0)
                    return;
                static bool logged = false;
                if (logged)
                    return;
                logged = true;
                std::printf("[blocks] grass top: haveTints=%d known=%d fT=%d tile=%d sat=%.2f biome=%s "
                            "tint=(%.2f,%.2f,%.2f)\n",
                            static_cast<int>(m.haveTints), static_cast<int>(known),
                            static_cast<int>(styleTop.tinted), tileTop,
                            library() ? library()->SaturationOf(tileTop) : -1.0f, blk.biomeName->c_str(),
                            styleTop.r, styleTop.g, styleTop.b);
            }

            Face MakeFace(int fx, int fy, int fz, int tile, const Style& style, float shade,
                          const int (*cells)[3], const float (*verts)[3]) const {
                return Face{fx, fy, fz, tile, style.r, style.g, style.b, style.tinted, shade, cells, verts};
            }

            void EmitWaterFaces() {
                const auto water = [](Style s) {
                    s.tinted = true;
                    return s;
                };
                EmitFace(MakeFace(0, 1, 0, tileTop, water(styleTop), kShadeTop, cTop, top));
                EmitFace(MakeFace(0, -1, 0, tileBottom ? tileBottom : tileTop, water(styleBottom),
                                  kShadeBottom, cBottom, bottom));
                EmitFace(MakeFace(1, 0, 0, tileSide, water(styleSide), kShadeX, cPx, px));
                EmitFace(MakeFace(-1, 0, 0, tileSide, water(styleSide), kShadeX, cNx, nx));
                EmitFace(MakeFace(0, 0, 1, tileSide, water(styleSide), kShadeZ, cPz, pz));
                EmitFace(MakeFace(0, 0, -1, tileSide, water(styleSide), kShadeZ, cNz, nz));
            }

            void EmitSolidFaces() {
                const BlockAppearance* app = blk.app;
                EmitFace(MakeFace(0, 1, 0, tileTop, styleTop, kShadeTop, cTop, top));
                EmitFace(MakeFace(0, -1, 0, tileBottom, styleBottom, kShadeBottom, cBottom, bottom));
                if (app && app->overlayFrac > 0 && app->overlayTile && !blk.gi->slab && styleTop.tinted &&
                    tileSide)
                    overlay = {app->overlayTile, styleTop.r, styleTop.g, styleTop.b};
                if (overlay.tile && library() && library()->BetterGrass() && tileTop) {
                    overlay.tile = 0;
                    EmitFace(MakeFace(1, 0, 0, tileTop, styleTop, kShadeX, cPx, px));
                    EmitFace(MakeFace(-1, 0, 0, tileTop, styleTop, kShadeX, cNx, nx));
                    EmitFace(MakeFace(0, 0, 1, tileTop, styleTop, kShadeZ, cPz, pz));
                    EmitFace(MakeFace(0, 0, -1, tileTop, styleTop, kShadeZ, cNz, nz));
                    return;
                }
                const auto sideTile = [&](int i) {
                    return app && app->sideTile[i] ? app->sideTile[i] : tileSide;
                };
                EmitFace(MakeFace(1, 0, 0, sideTile(0), styleSide, kShadeX, cPx, px));
                EmitFace(MakeFace(-1, 0, 0, sideTile(1), styleSide, kShadeX, cNx, nx));
                EmitFace(MakeFace(0, 0, 1, sideTile(2), styleSide, kShadeZ, cPz, pz));
                EmitFace(MakeFace(0, 0, -1, sideTile(3), styleSide, kShadeZ, cNz, nz));
            }

            bool FaceHidden(const Face& f) const {
                const uint16_t neighbour = cache().Get(x + f.nx, y + f.ny, z + f.nz);
                if (neighbour != 0 && cache().Occ(x + f.nx, y + f.ny, z + f.nz))
                    return true;
                if (neighbour != 0 && gCullInnerLeaves.load(std::memory_order_relaxed) && IsLeafBlock(blk.id) &&
                    IsLeafBlock(neighbour))
                    return true;
                if (neighbour != 0 && (translucent || IsGlassTile(f.tile)) &&
                    (neighbour == blk.id || (library() && library()->GlassMode() && IsGlassTile(f.tile) &&
                                             library()->Look(neighbour).side == f.tile)))
                    return true;
                if (!water)
                    return false;
                const uint16_t other = EffLiquid(cache(), library(), x + f.nx, y + f.ny, z + f.nz);
                return other != 0 && LiquidKind(library(), other) == liquidKind;
            }

            bool SameGlass(uint16_t neighbour, int baseTile) const {
                return neighbour != 0 && (neighbour == blk.id || library()->Look(neighbour).side == baseTile);
            }

            bool GlassNeighbourConnected(const Face& f, int sx, int sy, int sz, int baseTile) const {
                return SameGlass(cache().Get(x + sx, y + sy, z + sz), baseTile) &&
                       !SameGlass(cache().Get(x + sx + f.nx, y + sy + f.ny, z + sz + f.nz), baseTile);
            }

            int GlassMask(const Face& f, int baseTile) const {
                int du[3] = {0, 0, 0}, dv[3] = {0, 0, 0};
                if (f.nx != 0) {
                    du[2] = f.nx > 0 ? -1 : 1;
                    dv[1] = -1;
                } else if (f.nz != 0) {
                    du[0] = f.nz < 0 ? -1 : 1;
                    dv[1] = -1;
                } else {
                    du[0] = 1;
                    dv[2] = f.ny > 0 ? 1 : -1;
                }
                int mask = 0;
                if (!GlassNeighbourConnected(f, -du[0], -du[1], -du[2], baseTile))
                    mask |= 1;
                if (!GlassNeighbourConnected(f, du[0], du[1], du[2], baseTile))
                    mask |= 2;
                if (!GlassNeighbourConnected(f, -dv[0], -dv[1], -dv[2], baseTile))
                    mask |= 4;
                if (!GlassNeighbourConnected(f, dv[0], dv[1], dv[2], baseTile))
                    mask |= 8;
                return mask;
            }

            void FaceCoordinates(const Face& f, float (&fu)[4], float (&fv)[4]) const {
                for (int i = 0; i < 4; i++) {
                    const auto& c = f.cells[i];
                    if (f.nx != 0) {
                        fu[i] = static_cast<float>(c[2] - z);
                        if (f.nx > 0)
                            fu[i] = 1.0f - fu[i];
                        fv[i] = 1.0f - static_cast<float>(c[1] - y);
                    } else if (f.nz != 0) {
                        fu[i] = static_cast<float>(c[0] - x);
                        if (f.nz < 0)
                            fu[i] = 1.0f - fu[i];
                        fv[i] = 1.0f - static_cast<float>(c[1] - y);
                    } else {
                        fu[i] = static_cast<float>(c[0] - x);
                        fv[i] = f.ny > 0 ? static_cast<float>(c[2] - z) : 1.0f - static_cast<float>(c[2] - z);
                    }
                }
            }

            static void InsetGlassUv(float& fu, float& fv, int mask) {
                const float k = BlockLibrary::GlassInset();
                if (!(mask & 1) && fu < 0.001f)
                    fu = k;
                else if (!(mask & 2) && fu > 0.999f)
                    fu = 1.0f - k;
                if (!(mask & 4) && fv < 0.001f)
                    fv = k;
                else if (!(mask & 8) && fv > 0.999f)
                    fv = 1.0f - k;
            }

            void EmitFace(const Face& f) {
                if (FaceHidden(f))
                    return;
                int tile = f.tile;
                int glassMode = 0;
                int glassMask = 15;
                if (const int mode = library() ? library()->GlassMode() : 0; mode && IsGlassTile(tile)) {
                    glassMode = mode;
                    glassMask = GlassMask(f, tile);
                    tile = library()->GlassTile(tile, mode, glassMask);
                }
                float u0, v0, u1, v1;
                BlockLibrary::TileUV(tile, u0, v0, u1, v1);
                float fu[4], fv[4], uv[4][2];
                FaceCoordinates(f, fu, fv);
                for (int i = 0; i < 4; i++) {
                    float du = fu[i], dv = fv[i];
                    if (glassMode == 1 || glassMode == 2)
                        InsetGlassUv(du, dv, glassMask);
                    uv[i][0] = u0 + du * (u1 - u0);
                    uv[i][1] = v0 + dv * (v1 - v0);
                }
                float base[3];
                if (!m.textured || !known)
                    base[0] = base[1] = base[2] = 0.6f;
                else if (f.tinted) {
                    base[0] = f.r;
                    base[1] = f.g;
                    base[2] = f.b;
                } else
                    base[0] = base[1] = base[2] = 1.0f;

                float col[4][3], lt[4][2], kv[4], ao[4];
                const int center[3] = {x + f.nx, y + f.ny, z + f.nz};
                const int normal[3] = {f.nx, f.ny, f.nz};
                int axis1, axis2, first[3];
                FaceAxes(normal, axis1, axis2);
                FirstCornerOffset(normal, first);
                const float shadeCenter = AoShade(cache(), library(), center[0], center[1], center[2]);
                const PackedLight lightCenter = m.LightAt(center);
                const int origin[3] = {x, y, z};
                CornerShade shades[2][2];
                if (!water)
                    m.ShadeFace(center, normal, axis1, axis2, first, shadeCenter, lightCenter, shades);
                for (int i = 0; i < 4; i++) {
                    if (water) {
                        ao[i] = 1.0f;
                        lt[i][0] = blk.cellLight.block;
                        lt[i][1] = blk.cellLight.sky;
                    } else {
                        const int a = f.cells[i][axis1] - origin[axis1] > 0 ? 1 : 0;
                        const int b = f.cells[i][axis2] - origin[axis2] > 0 ? 1 : 0;
                        const CornerShade& corner = shades[a][b];
                        ao[i] = corner.ao;
                        lt[i][0] = corner.block;
                        lt[i][1] = corner.sky;
                    }
                    kv[i] = ao[i] * f.shade;
                    col[i][0] = base[0] * kv[i];
                    col[i][1] = base[1] * kv[i];
                    col[i][2] = base[2] * kv[i];
                }
                const bool flip = f.nx != 0;
                const bool holes = library() && library()->AlphaOf(tile).holes;
                const RenderLayer layer = translucent ? RenderLayer::Translucent
                                          : holes     ? RenderLayer::CutoutCull
                                                      : RenderLayer::Solid;
                tess().SetLayer(layer);
                tess().AddQuad(f.verts, col, uv, flip, lt, ao, f.shade);
                if (overlay.tile && f.ny == 0) {
                    tess().PinLastQuad();
                    EmitOverlay(f, fu, fv, kv, lt, ao, flip, layer);
                }
            }

            void EmitOverlay(const Face& f, const float (&fu)[4], const float (&fv)[4], const float (&kv)[4],
                             const float (&lt)[4][2], const float (&ao)[4], bool flip, RenderLayer restore) {
                float u0, v0, u1, v1;
                BlockLibrary::TileUV(overlay.tile, u0, v0, u1, v1);
                const float inset = 0.02f / 16.0f;
                float uv[4][2], col[4][3];
                for (int i = 0; i < 4; i++) {
                    const float du = inset + fu[i] * (1.0f - 2 * inset);
                    const float dv = inset + fv[i] * (1.0f - 2 * inset);
                    uv[i][0] = u0 + du * (u1 - u0);
                    uv[i][1] = v0 + dv * (v1 - v0);
                    col[i][0] = overlay.r * kv[i];
                    col[i][1] = overlay.g * kv[i];
                    col[i][2] = overlay.b * kv[i];
                }
                tess().SetLayer(RenderLayer::Cutout);
                tess().AddQuad(f.verts, col, uv, flip, lt, ao, f.shade);
                tess().SetLayer(restore);
            }

            BlockMesher& m;
            BlockCtx& blk;
            const int x, y, z;
            const int liquidKind;
            const bool water;
            const bool translucent;
            bool known = false;
            int tileTop = 0, tileSide = 0, tileBottom = 0;
            Style styleTop, styleSide, styleBottom;
            Overlay overlay;
            float top[4][3], bottom[4][3], px[4][3], nx[4][3], pz[4][3], nz[4][3];
            int cTop[4][3], cBottom[4][3], cPx[4][3], cNx[4][3], cPz[4][3], cNz[4][3];
        };

        void BlockMesher::EmitCubeOrLiquid(BlockCtx& blk) {
            CubeEmitter(*this, blk).Emit();
        }
    }

    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints, MeshReuse* reuse) {
        const uint64_t t0 = NowUs();
        bool textured = library && library->Ready();
        bool haveTints = tints && tints->Ready();

        static thread_local BlockCache cache;
        int yLo = 0, yHi = 0;
        {
            std::lock_guard<std::mutex> lk(level.mutex);
            auto it = level.chunks.find(pos);
            if (it == level.chunks.end())
                return;
            if (!it->second.YRange(yLo, yHi))
                return;
            int fLo = std::max(yLo - 1, static_cast<int>(Chunk::MIN_Y));
            int fHi = std::min(yHi + 1, static_cast<int>(Chunk::MIN_Y) + Chunk::HEIGHT - 1);
            cache.FillRaw(level, pos, fLo, fHi);
        }
        Memo().Reset(gMemoGlobal.load());
        if (library)
            cache.Classify(library);
        else
            for (size_t i = 0; i < cache.cells.size(); i++)
                cache.solid[i] = cache.cells[i] != 0;
        int lowAir = cache.yBase + cache.yRows;
        {
            const size_t layer = static_cast<size_t>(18) * 18;
            for (int row = 0; row < cache.yRows; row++) {
                const uint8_t* s = &cache.solid[static_cast<size_t>(row) * layer];
                bool open = false;
                for (size_t i = 0; i < layer; i++)
                    if (!s[i]) {
                        open = true;
                        break;
                    }
                if (open) {
                    lowAir = cache.yBase + row;
                    break;
                }
            }
        }
        const int faceLo = std::max(yLo, lowAir - 1);
        const uint64_t t1 = NowUs();
        for (int sy = yLo >> 4; sy <= (yHi >> 4); sy++)
            tess.AddVisibility(sy, SectionConnectivity(cache, sy));
        const uint64_t t2 = NowUs();
        static thread_local ChunkLighting::Result lighting;
        ChunkLighting::Compute(level, pos, std::max(cache.yBase, lowAir - 3), cache.yBase + cache.yRows - 1,
                               lighting);
        const uint64_t t3 = NowUs();
        tess.Reserve(8192);

        static thread_local std::vector<uint8_t> keep;
        keep.assign(Chunk::SECTION_COUNT + 2, 0);
        const int sectionBase = Chunk::MIN_Y >> 4;
        uint64_t reuseKey = 0;
        if (reuse) {
            reuseKey = reuse->generation * 0x9E3779B97F4A7C15ull;
            for (const uint64_t v :
                 {static_cast<uint64_t>(reinterpret_cast<uintptr_t>(library)),
                  static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tints)),
                  static_cast<uint64_t>(gMemoGlobal.load()),
                  static_cast<uint64_t>(textured) | (static_cast<uint64_t>(haveTints) << 1)})
                reuseKey = (reuseKey ^ v) * 1099511628211ull;
        }
        const bool partial = reuse && reuse->mesh && reuse->key == reuseKey &&
                             reuse->cellBase == cache.yBase && reuse->cellRows == cache.yRows &&
                             reuse->lightBase == lighting.yBase && reuse->lightRows == lighting.rows &&
                             reuse->faceLo == faceLo && reuse->cells.size() == cache.cells.size() &&
                             reuse->light.size() == lighting.packed.size();
        if (partial) {
            constexpr size_t kRow = 18 * 18;
            const auto rowDiffers = [](const auto& a, const auto& b, int row) {
                return std::memcmp(&a[static_cast<size_t>(row) * kRow], &b[static_cast<size_t>(row) * kRow],
                                   kRow * sizeof(a[0])) != 0;
            };
            std::vector<int> changed;
            for (int row = 0; row < cache.yRows; row++)
                if (rowDiffers(cache.cells, reuse->cells, row) ||
                    rowDiffers(cache.liquid, reuse->liquid, row) ||
                    rowDiffers(cache.biome, reuse->biome, row)) {
                    const int y = cache.yBase + row;
                    for (int sy = (y - 3) >> 4; sy <= (y + 3) >> 4; sy++)
                        if (sy - sectionBase >= 0 && sy - sectionBase < static_cast<int>(keep.size()))
                            keep[sy - sectionBase] |= 2;
                }
            for (int row = 0; row < lighting.rows; row++)
                if (rowDiffers(lighting.packed, reuse->light, row)) {
                    const int y = lighting.yBase + row;
                    for (int sy = (y - 2) >> 4; sy <= (y + 2) >> 4; sy++)
                        if (sy - sectionBase >= 0 && sy - sectionBase < static_cast<int>(keep.size()))
                            keep[sy - sectionBase] |= 2;
                }
            for (auto& k : keep)
                k = k & 2 ? 0 : 1;
        }
        BlockMesher mesher(tess, cache, library, tints, lighting, pos);

        int rebuiltSections = 0, copiedSections = 0;
        for (int y = faceLo; y <= yHi; y++) {
            if ((y == faceLo || (y & 15) == 0)) {
                const int sy = y >> 4;
                if (partial && keep[sy - sectionBase]) {
                    tess.AppendSection(*reuse->mesh, sy);
                    copiedSections++;
                    y = std::min(yHi, y | 15);
                    continue;
                }
                rebuiltSections++;
            }
            tess.EnterSection(y >> 4);
            uint16_t lastId = 0;
            int lastKind = 0;
            for (int z = 0; z < 16; z++) {
                const size_t rowIndex = cache.Idx(0, y, z);
                for (int x = 0; x < 16; x++) {
                    const uint16_t here = cache.cells[rowIndex + static_cast<size_t>(x)];
                    bool liquidPass = true;
                    if (cache.liquid[rowIndex + static_cast<size_t>(x)] == 0) {
                        if (here == 0) {
                            liquidPass = false;
                        } else {
                            if (here != lastId) {
                                lastId = here;
                                lastKind = SpecialKind(here);
                            }
                            liquidPass = lastKind == 4;
                        }
                    }
                    if (liquidPass)
                        mesher.MeshCell(x, y, z, 0);
                    if (here != 0)
                        mesher.MeshCell(x, y, z, 1);
                }
            }
        }
        if (reuse) {
            reuse->mesh.reset();
            reuse->key = reuseKey;
            reuse->cellBase = cache.yBase;
            reuse->cellRows = cache.yRows;
            reuse->lightBase = lighting.yBase;
            reuse->lightRows = lighting.rows;
            reuse->faceLo = faceLo;
            reuse->cells = cache.cells;
            reuse->liquid = cache.liquid;
            reuse->biome = cache.biome;
            reuse->light = lighting.packed;
            reuse->rebuilt = rebuiltSections;
            reuse->copied = copiedSections;
        }
        const uint64_t t4 = NowUs();
        gProf[0] += t1 - t0;
        gProf[1] += t2 - t1;
        gProf[2] += t3 - t2;
        gProf[3] += t4 - t3;
        gProf[4] += t4 - t0;
        gProfChunks++;
    }
}
