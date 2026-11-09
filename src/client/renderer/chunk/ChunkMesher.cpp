#include <unordered_set>
#include <mutex>
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "world/level/Level.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "client/renderer/block/BlockModels.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/renderer/Tessellator.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "client/player/BlockCollision.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

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
            if (pal.HasState(id, "minecraft:vertical_half") &&
                pal.Name(id).find("double") == std::string::npos) {
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
        uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + z * 1440662683u);
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
            std::sort(states.begin(), states.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            result = pal.Intern(BlockPalette::HashState(name, states));
        }
        memo[key] = result;
        return result;
    }

    static int SpecialKind(uint16_t id) {
        thread_local std::vector<int8_t> memo(65536, -1);
        int8_t& m = memo[id];
        if (m < 0) {
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
        return m;
    }

    static bool IsDoorId(uint16_t id) {
        thread_local std::vector<int8_t> memo(65536, -1);
        int8_t& m = memo[id];
        if (m < 0) {
            BlockPalette& pal = BlockPalette::Instance();
            const std::string name = pal.Name(id);
            m = name.size() > 5 && name.compare(name.size() - 5, 5, "_door") == 0 &&
                        pal.BlockHasProperty(name, "door_hinge_bit")
                    ? 1
                    : 0;
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
            for (int b = 0; b < s.boxes.size(); b++) {
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
            for (int b = 0; b < shape.boxes.size() && !full; b++) {
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
            h = h * 1000003u ^ (size_t)k.tile;
            h = h * 1000003u ^ k.texHash;
            h = h * 1000003u ^ (size_t)k.biome;
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

        size_t Idx(int x, int y, int z) const {
            return ((size_t)(y - yBase) * 18 + (size_t)(z + 1)) * 18 + (size_t)(x + 1);
        }
        bool InRange(int x, int y, int z) const {
            return x >= -1 && x <= 16 && z >= -1 && z <= 16 && y >= yBase && y < yBase + yRows;
        }

        void FillRaw(Level& level, const ChunkPos& pos, int yLo, int yHi) {
            yBase = yLo;
            yRows = yHi - yLo + 1;
            const size_t n = (size_t)18 * (size_t)yRows * 18;
            cells.assign(n, 0);
            liquid.assign(n, 0);
            anyLiquid = false;
            solid.assign(n, 0);
            biome.assign(n, 1);
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
                solid[ci] = (uint8_t)sv;
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

        uint16_t Get(int x, int y, int z) const {
            return InRange(x, y, z) ? cells[Idx(x, y, z)] : (uint16_t)0;
        }
        uint16_t Liquid(int x, int y, int z) const {
            return InRange(x, y, z) ? liquid[Idx(x, y, z)] : (uint16_t)0;
        }
        uint8_t Occ(int x, int y, int z) const { return InRange(x, y, z) ? solid[Idx(x, y, z)] : (uint8_t)0; }
        uint8_t Biome(int x, int y, int z) const {
            return InRange(x, y, z) ? biome[Idx(x, y, z)] : (uint8_t)1;
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

    static float AoShade(const BlockCache& cache, BlockLibrary* library, int x, int y, int z) {
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
            stack.push_back((uint16_t)start);
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
                        stack.push_back((uint16_t)ni);
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
                            conn |= (uint16_t)(1u << (a * (11 - a) / 2 + (b - a - 1)));
            if (conn == 0x7FFF)
                break;
        }
        return conn;
    }

    namespace {
        std::atomic<uint64_t> gProf[5];
        std::atomic<uint64_t> gProfChunks{0};
        uint64_t NowUs() {
            return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
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

    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints) {
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
            int fLo = std::max(yLo - 1, (int)Chunk::MIN_Y);
            int fHi = std::min(yHi + 1, (int)Chunk::MIN_Y + Chunk::HEIGHT - 1);
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
            const size_t layer = (size_t)18 * 18;
            for (int row = 0; row < cache.yRows; row++) {
                const uint8_t* s = &cache.solid[(size_t)row * layer];
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
        const LightRegistry& registry = LightRegistry::Instance();
        auto sample = [&](int lx, int ly, int lz) {
            const uint8_t packed = lighting.At(lx, ly, lz);
            return std::array<int, 2>{ChunkLighting::Result::Block(packed),
                                      ChunkLighting::Result::Sky(packed)};
        };
        auto ownLight = [&](int lx, int ly, int lz, uint16_t cellId) {
            std::array<int, 2> l = sample(lx, ly, lz);
            if (!cache.Occ(lx, ly, lz) && registry.Resolve(cellId).opacity >= kMaxLight) {
                static constexpr int kOffsets[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                                       {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
                for (const auto& o : kOffsets) {
                    const std::array<int, 2> n = sample(lx + o[0], ly + o[1], lz + o[2]);
                    l[0] = std::max(l[0], n[0]);
                    l[1] = std::max(l[1], n[1]);
                }
            }
            return CellLight{static_cast<float>(l[0]), static_cast<float>(l[1])};
        };
        tess.Reserve(8192);

        std::vector<std::string> biomeNames(256);
        std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> tintMemo;
        std::unordered_map<TintKey, std::array<float, 4>, TintKeyHash> blendMemo;
        static const std::string kPlainsName = "minecraft:plains";

        int baseX = pos.x * 16, baseZ = pos.z * 16;
        std::unordered_set<uint16_t> askedFor;
        for (int y = faceLo; y <= yHi; y++) {
            tess.EnterSection(y >> 4);
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    for (int pass = 0; pass < 2; pass++) {
                        uint16_t id = cache.Get(x, y, z);
                        if (pass == 1 && y > cache.yBase && y < cache.yBase + cache.yRows - 1) {
                            const size_t c = cache.Idx(x, y, z);
                            if (cache.solid[c] && cache.solid[c - 1] && cache.solid[c + 1] &&
                                cache.solid[c - 18] && cache.solid[c + 18] && cache.solid[c - 324] &&
                                cache.solid[c + 324])
                                continue;
                        }
                        if (pass == 0) {
                            const uint16_t here = cache.Get(x, y, z);
                            const bool column = here != 0 && SpecialKind(here) == 4;
                            id = cache.Liquid(x, y, z);
                            if (column && library && (id == 0 || !IsLiquidId(library, id)))
                                id = ColumnWater(cache, library, x, y, z);
                            if (id == 0 || !library || !IsLiquidId(library, id) ||
                                (!column && IsLiquidId(library, here)))
                                continue;
                        }
                        if (id == 0)
                            continue;
                        if (pass == 1 && textured) {
                            if (SpecialKind(id) == 4)
                                continue;
                            if (const int special = SpecialKind(id); special && special != 4) {
                                tess.AddSpecial(baseX + x, y, baseZ + z, id);
                                if (special == 1 || special == 3 || special == 5)
                                    continue;
                            }
                        }
                        if (pass == 1 && textured && IsDoorId(id)) {
                            const bool upperHalf =
                                BlockPalette::Instance().StateInt(id, "upper_block_bit", 0) != 0;
                            const int oy = upperHalf ? y - 1 : y + 1;
                            if (oy >= cache.yBase && oy < cache.yBase + cache.yRows)
                                id = ResolveDoor(id, cache.Get(x, oy, z), upperHalf);
                        }
                        tess.SetLayer(RenderLayer::Cutout);
                        const CellLight cellLight = ownLight(x, y, z, id);
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
                                       (unsigned)id, BlockPalette::Instance().Name(id).c_str(), baseX + x, y,
                                       baseZ + z);
                            continue;
                        }
                        float bx = (float)(baseX + x), by = (float)y, bz = (float)(baseZ + z);
                        tess.SetBlock(id, bx, by, bz);

                        const int biomeId = cache.Biome(x, y, z);
                        const std::string* biomeNamePtr = &kPlainsName;
                        if (haveTints) {
                            std::string& bn = biomeNames[(size_t)biomeId & 255];
                            if (bn.empty())
                                bn = tints->NameForId(biomeId);
                            biomeNamePtr = &bn;
                        }
                        const std::string& biomeName = *biomeNamePtr;
                        const GeomInfo& gi = GeomFor(id);
                        uint32_t bhash = PosHash(baseX + x, y, baseZ + z);

                        if (app && !modelEntry && (gi.stair || gi.fence || gi.wall || gi.cakeBites >= 0)) {
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
                            auto emitRot = [&](float lx0, float ly0, float lz0, float lx1, float ly1,
                                               float lz1, int dir, bool flipY) {
                                float ax0 = lx0, az0 = lz0, ax1 = lx1, az1 = lz1;
                                float ox0, oz0, ox1, oz1;
                                rotXZ(ax0, az0, dir, ox0, oz0);
                                rotXZ(ax1, az1, dir, ox1, oz1);
                                float rx0 = ox0 < ox1 ? ox0 : ox1, rx1 = ox0 < ox1 ? ox1 : ox0;
                                float rz0 = oz0 < oz1 ? oz0 : oz1, rz1 = oz0 < oz1 ? oz1 : oz0;
                                float ry0 = flipY ? 1 - ly1 : ly0, ry1 = flipY ? 1 - ly0 : ly1;
                                EmitBox(tess, cellLight, bx, by, bz, rx0, ry0, rz0, rx1, ry1, rz1, ct, cs,
                                        cb);
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
                                EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, 0, 6 / 16.0f, 10 / 16.0f, 1,
                                        10 / 16.0f, ct, cs, cb);
                                const float armY0 = 11 / 16.0f, armY1 = 15 / 16.0f;
                                if (gi.fenceArm[0])
                                    EmitBox(tess, cellLight, bx, by, bz, 10 / 16.0f, armY0, 6 / 16.0f, 1,
                                            armY1, 10 / 16.0f, ct, cs, cb);
                                if (gi.fenceArm[1])
                                    EmitBox(tess, cellLight, bx, by, bz, 0, armY0, 6 / 16.0f, 6 / 16.0f,
                                            armY1, 10 / 16.0f, ct, cs, cb);
                                if (gi.fenceArm[2])
                                    EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 10 / 16.0f, 1,
                                            armY1, 10 / 16.0f, ct, cs, cb);
                                if (gi.fenceArm[3])
                                    EmitBox(tess, cellLight, bx, by, bz, 6 / 16.0f, armY0, 0, 10 / 16.0f,
                                            armY1, 6 / 16.0f, ct, cs, cb);
                            } else if (gi.wall) {
                                EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 5 / 16.0f, 11 / 16.0f, 1,
                                        11 / 16.0f, ct, cs, cb);
                                const int h[4] = {gi.wallArm[0] >= 2 ? 16 : 14, gi.wallArm[1] >= 2 ? 16 : 14,
                                                  gi.wallArm[2] >= 2 ? 16 : 14, gi.wallArm[3] >= 2 ? 16 : 14};
                                if (gi.wallArm[0])
                                    EmitBox(tess, cellLight, bx, by, bz, 11 / 16.0f, 0, 5 / 16.0f, 1,
                                            h[0] / 16.0f, 11 / 16.0f, ct, cs, cb);
                                if (gi.wallArm[1])
                                    EmitBox(tess, cellLight, bx, by, bz, 0, 0, 5 / 16.0f, 5 / 16.0f,
                                            h[1] / 16.0f, 11 / 16.0f, ct, cs, cb);
                                if (gi.wallArm[2])
                                    EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 11 / 16.0f, 1,
                                            h[2] / 16.0f, 11 / 16.0f, ct, cs, cb);
                                if (gi.wallArm[3])
                                    EmitBox(tess, cellLight, bx, by, bz, 5 / 16.0f, 0, 0, 11 / 16.0f,
                                            h[3] / 16.0f, 11 / 16.0f, ct, cs, cb);
                            } else {
                                float x0 = (1 + 2 * gi.cakeBites) / 16.0f;
                                if (x0 < 0)
                                    x0 = 0;
                                if (x0 > 15 / 16.0f)
                                    x0 = 15 / 16.0f;
                                EmitBox(tess, cellLight, bx, by, bz, x0, 0, 1 / 16.0f, 15 / 16.0f, 8 / 16.0f,
                                        15 / 16.0f, ct, cs, cb);
                            }
                            continue;
                        }

                        auto biomeTintForRaw = [&](const std::string& biomeName, const std::string& tex,
                                                   int tile, bool isTop) -> std::array<float, 4> {
                            (void)isTop;
                            if (app) {
                                static const char* kGrassKeys[] = {
                                    "grass_top",   "grass_carried",      "tallgrass", "tall_grass",
                                    "short_grass", "double_plant_grass", "fern",      "large_fern",
                                    "reeds",       "sugar_cane"};
                                std::string tpath = library ? library->TilePathOf(tile) : std::string();
                                bool grassLike = false;
                                for (const char* k : kGrassKeys)
                                    if (TexHas(tex, k) || TexHas(tpath, k)) {
                                        grassLike = true;
                                        break;
                                    }
                                if (grassLike) {
                                    std::array<float, 3> c =
                                        haveTints ? tints->Grass(biomeName)
                                                  : std::array<float, 3>{0.4745f, 0.7529f, 0.3529f};
                                    static int logged = 0;
                                    if (logged < 6) {
                                        logged++;
                                        printf("[tint] grass-like tex='%s' path='%s' tile=%d haveTints=%d "
                                               "biome=%s -> (%.2f,%.2f,%.2f)\n",
                                               tex.c_str(), tpath.c_str(), tile, (int)haveTints,
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
                                    return {tints->MapAverage("evergreen")[0],
                                            tints->MapAverage("evergreen")[1],
                                            tints->MapAverage("evergreen")[2], 1};
                                if (TexHas(tex, "mangrove"))
                                    return {tints->MapAverage("mangrove_swamp_foliage")[0],
                                            tints->MapAverage("mangrove_swamp_foliage")[1],
                                            tints->MapAverage("mangrove_swamp_foliage")[2], 1};
                                auto c = tints->Foliage(biomeName);
                                return {c[0], c[1], c[2], 1};
                            }
                            if (TexHas(tex, "grass_carried") || tex == "grass_top" ||
                                TexHas(tex, "grass_top") || TexHas(tex, "tallgrass") ||
                                TexHas(tex, "tall_grass") || TexHas(tex, "short_grass") ||
                                TexHas(tex, "double_plant_grass") || tex == "fern" ||
                                TexHas(tex, "fern_carried")) {
                                auto c = tints->Grass(biomeName);
                                return {c[0], c[1], c[2], 1};
                            }
                            if (app->useCarriedColor) {
                                auto c = tints->Foliage(biomeName);
                                return {c[0], c[1], c[2], 1};
                            }
                            return {app->tintR, app->tintG, app->tintB, 0};
                        };
                        int blockUniform = -1, blockCentre = 0;
                        auto biomeTintFor = [&](const std::string& tex, int tile,
                                                bool isTop) -> std::array<float, 4> {
                            if (TexHas(tex, "redstone_dust")) {
                                const float f = static_cast<float>(BlockPalette::Instance().StateInt(
                                                    id, "redstone_signal", 0)) /
                                                15.0f;
                                return {f * 0.6f + (f > 0.0f ? 0.4f : 0.3f),
                                        std::clamp(f * f * 0.7f - 0.5f, 0.0f, 1.0f),
                                        std::clamp(f * f * 0.6f - 0.7f, 0.0f, 1.0f), 1.0f};
                            }
                            const size_t texHash = std::hash<std::string>()(tex);
                            std::array<float, 4> sum{0, 0, 0, 0};
                            std::array<float, 4> own{0, 0, 0, 0};
                            int count = 0;
                            if (blockUniform < 0) {
                                blockCentre = cache.Biome(std::clamp(x, -1, 16), y, std::clamp(z, -1, 16));
                                blockUniform = 1;
                                for (int dz = -2; dz <= 2 && blockUniform; dz++)
                                    for (int dx = -2; dx <= 2; dx++)
                                        if (cache.Biome(std::clamp(x + dx, -1, 16), y,
                                                        std::clamp(z + dz, -1, 16)) != blockCentre) {
                                            blockUniform = 0;
                                            break;
                                        }
                            }
                            const int centre = blockCentre;
                            const bool uniform = blockUniform == 1;
                            if (uniform) {
                                TintKey k{app, tile, texHash, centre};
                                auto mit = tintMemo.find(k);
                                if (mit == tintMemo.end()) {
                                    std::string& bn = biomeNames[static_cast<size_t>(centre) & 255];
                                    if (bn.empty() && haveTints)
                                        bn = tints->NameForId(centre);
                                    mit = tintMemo
                                              .emplace(k, biomeTintForRaw(haveTints ? bn : kPlainsName, tex,
                                                                          tile, isTop))
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
                                    const int sx = std::clamp(x + dx, -1, 16),
                                              sz = std::clamp(z + dz, -1, 16);
                                    const int sample = cache.Biome(sx, y, sz);
                                    TintKey k{app, tile, texHash, sample};
                                    auto mit = tintMemo.find(k);
                                    if (mit == tintMemo.end()) {
                                        std::string& bn = biomeNames[(size_t)sample & 255];
                                        if (bn.empty() && haveTints)
                                            bn = tints->NameForId(sample);
                                        mit = tintMemo
                                                  .emplace(k, biomeTintForRaw(haveTints ? bn : kPlainsName,
                                                                              tex, tile, isTop))
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
                        };

                        if (modelEntry) {
                            const BlockModels& bm = library->Models();
                            float mox = 0, moy = 0, moz = 0;
                            if (modelEntry->offset)
                                ModelOffset(baseX + x, baseZ + z, modelEntry->offset, mox, moy, moz);
                            static constexpr int kDV[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                                              {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
                            static constexpr float kDirShade[6] = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
                            struct Packed {
                                int block, sky;
                            };
                            auto coordsAt = [&](const int p[3]) {
                                const std::array<int, 2> l = sample(p[0], p[1], p[2]);
                                return Packed{l[0] * 16, l[1] * 16};
                            };
                            auto smoothBlend = [](Packed n1, Packed n2, Packed n3, Packed c) {
                                if (c.sky > 2 * 16 - 1 || c.block > 2 * 16 - 1) {
                                    for (Packed* n : {&n1, &n2, &n3}) {
                                        if (n->block == 0 && n->sky == 0)
                                            *n = c;
                                        else if (n->sky == 0)
                                            n->sky = c.sky;
                                    }
                                }
                                return Packed{(n1.block + n2.block + n3.block + c.block) >> 2,
                                              (n1.sky + n2.sky + n3.sky + c.sky) >> 2};
                            };
                            bool memoHave[12] = {};
                            float memoAo[12][2][2], memoBlock[12][2][2], memoSky[12][2][2];
                            auto emitQuad = [&](const ModelQuad& q) {
                                if (!q.tile && (q.tex == 0xFFFF || q.tex >= bm.tiles.size()))
                                    return;
                                int tile = q.tile ? q.tile : bm.tiles[q.tex];
                                if (!tile)
                                    return;
                                const int baseTile = tile;
                                const BlockLibrary::TileAlpha ta = library->AlphaOf(tile);
                                const bool glassTile = library->IsGlassBase(tile);
                                float quv[4][2];
                                for (int k = 0; k < 4; k++) {
                                    quv[k][0] = q.uv[k][0];
                                    quv[k][1] = q.uv[k][1];
                                }
                                if (const int glassMode = library->GlassMode();
                                    glassMode && library->IsGlassBase(tile)) {
                                    int ia = -1, ib = -1, ic = -1;
                                    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
                                    for (auto k : q.uv) {
                                        umin = std::min(umin, k[0]);
                                        umax = std::max(umax, k[0]);
                                        vmin = std::min(vmin, k[1]);
                                        vmax = std::max(vmax, k[1]);
                                    }
                                    for (int k = 0; k < 4; k++) {
                                        const bool u0z = std::fabs(q.uv[k][0] - umin) < 0.01f,
                                                   u16 = std::fabs(q.uv[k][0] - umax) < 0.01f;
                                        const bool v0z = std::fabs(q.uv[k][1] - vmin) < 0.01f,
                                                   v16 = std::fabs(q.uv[k][1] - vmax) < 0.01f;
                                        if (u0z && v0z)
                                            ia = k;
                                        else if (u16 && v0z)
                                            ib = k;
                                        else if (u0z && v16)
                                            ic = k;
                                    }
                                    const bool edgeL = umin < 0.01f, edgeR = umax > 15.99f,
                                               edgeT = vmin < 0.01f, edgeB = vmax > 15.99f;
                                    if (ia >= 0 && ib >= 0 && ic >= 0) {
                                        int mask = 0;
                                        const auto exposed = [&](const float* from, const float* to,
                                                                 bool negate) {
                                            int d[3];
                                            for (int a = 0; a < 3; a++) {
                                                const float diff = to[a] - from[a];
                                                d[a] = std::fabs(diff) < 1e-3f ? 0 : (diff > 0 ? 1 : -1);
                                                if (negate)
                                                    d[a] = -d[a];
                                            }
                                            // connected only when the neighbour is the same glass (id or look,
                                            // the rule the face cull uses) and its own face here is visible:
                                            // covered by more of the same glass, OptiFine draws the border
                                            const int* fn = kDV[q.dir];
                                            const auto same = [&](uint16_t nn) {
                                                return nn != 0 && (nn == id || library->Look(nn).side == baseTile);
                                            };
                                            if (!same(cache.Get(x + d[0], y + d[1], z + d[2])))
                                                return true;
                                            return same(cache.Get(x + d[0] + fn[0], y + d[1] + fn[1], z + d[2] + fn[2]));
                                        };
                                        if (edgeL && exposed(q.pos[ia], q.pos[ib], true))
                                            mask |= 1;
                                        if (edgeR && exposed(q.pos[ia], q.pos[ib], false))
                                            mask |= 2;
                                        if (edgeT && exposed(q.pos[ia], q.pos[ic], true))
                                            mask |= 4;
                                        if (edgeB && exposed(q.pos[ia], q.pos[ic], false))
                                            mask |= 8;
                                        tile = library->GlassTile(tile, glassMode, mask);
                                        if (glassMode == 1 || glassMode == 2) {
                                            const float k = BlockLibrary::GlassInset() * 16.0f;
                                            for (auto& c : quv) {
                                                float& u = c[0];
                                                float& v = c[1];
                                                if (edgeL && !(mask & 1) && std::fabs(u - umin) < 0.01f)
                                                    u = k;
                                                else if (edgeR && !(mask & 2) && std::fabs(u - umax) < 0.01f)
                                                    u = 16.0f - k;
                                                if (edgeT && !(mask & 4) && std::fabs(v - vmin) < 0.01f)
                                                    v = k;
                                                else if (edgeB && !(mask & 8) && std::fabs(v - vmax) < 0.01f)
                                                    v = 16.0f - k;
                                            }
                                        }
                                    } else if (glassMode == 3) {
                                        tile = library->GlassTile(tile, glassMode, 0);
                                    }
                                }
                                if (q.cull >= 0) {
                                    const int* nb = kDV[q.cull];
                                    const uint16_t nid = cache.Get(x + nb[0], y + nb[1], z + nb[2]);
                                    if (nid != 0 && cache.Occ(x + nb[0], y + nb[1], z + nb[2]))
                                        return;
                                    if ((nid == id || (library->GlassMode() && nid != 0 &&
                                                       library->Look(nid).side == baseTile)) &&
                                        (ta.translucent || glassTile))
                                        return;
                                }
                                const int dir = q.dir;
                                const int* nrm = kDV[dir];
                                const int nAxis = dir < 2 ? 1 : (dir < 4 ? 2 : 0);
                                const float planeAt = (dir & 1) ? 1.0f : 0.0f;
                                bool flush = true;
                                for (auto po : q.pos)
                                    if (std::fabs(po[nAxis] - planeAt) > 1e-4f)
                                        flush = false;
                                if (flush && q.cull < 0 && (glassTile || ta.translucent)) {
                                    const uint16_t nid = cache.Get(x + nrm[0], y + nrm[1], z + nrm[2]);
                                    if (nid != 0 && (nid == id || (library->GlassMode() && glassTile &&
                                                                   library->Look(nid).side == baseTile)))
                                        return;
                                }
                                const int axis1 = nrm[0] != 0 ? 1 : 0;
                                const int axis2 = nrm[2] != 0 ? 1 : 2;
                                int first[3] = {0, 0, 0};
                                if (nrm[1] < 0)
                                    first[0] = -1;
                                else if (nrm[1] > 0)
                                    first[0] = 1;
                                else if (nrm[2] < 0)
                                    first[1] = 1;
                                else if (nrm[2] > 0)
                                    first[0] = -1;
                                else if (nrm[0] < 0)
                                    first[1] = 1;
                                else
                                    first[1] = -1;
                                const int memoKey = dir * 2 + (flush ? 1 : 0);
                                float(&cAo)[2][2] = memoAo[memoKey];
                                float(&cBlock)[2][2] = memoBlock[memoKey];
                                float(&cSky)[2][2] = memoSky[memoKey];
                                if (!memoHave[memoKey]) {
                                    memoHave[memoKey] = true;
                                    const int center[3] = {x + (flush ? nrm[0] : 0), y + (flush ? nrm[1] : 0),
                                                           z + (flush ? nrm[2] : 0)};
                                    const float shadeCenter =
                                        AoShade(cache, library, center[0], center[1], center[2]);
                                    const Packed lightCenter = coordsAt(center);
                                    for (int s1 = 0; s1 < 2; s1++)
                                        for (int s2 = 0; s2 < 2; s2++) {
                                            const int d1 = s1 ? 1 : -1, d2 = s2 ? 1 : -1;
                                            int e1[3] = {center[0], center[1], center[2]};
                                            int e2[3] = {center[0], center[1], center[2]};
                                            int corner[3] = {center[0], center[1], center[2]};
                                            e1[axis1] += d1;
                                            e2[axis2] += d2;
                                            corner[axis1] += d1;
                                            corner[axis2] += d2;
                                            const bool perm1 =
                                                !cache.Occ(e1[0] + nrm[0], e1[1] + nrm[1], e1[2] + nrm[2]);
                                            const bool perm2 =
                                                !cache.Occ(e2[0] + nrm[0], e2[1] + nrm[1], e2[2] + nrm[2]);
                                            const int fb[3] = {center[0] + first[0], center[1] + first[1],
                                                               center[2] + first[2]};
                                            float sc;
                                            Packed lc;
                                            if (!perm1 && !perm2) {
                                                sc = AoShade(cache, library, fb[0], fb[1], fb[2]);
                                                lc = coordsAt(fb);
                                            } else {
                                                sc = AoShade(cache, library, corner[0], corner[1], corner[2]);
                                                lc = coordsAt(corner);
                                            }
                                            const float a1 = AoShade(cache, library, e1[0], e1[1], e1[2]);
                                            const float a2 = AoShade(cache, library, e2[0], e2[1], e2[2]);
                                            cAo[s1][s2] = (a1 + a2 + sc + shadeCenter) * 0.25f;
                                            const Packed b =
                                                smoothBlend(coordsAt(e1), coordsAt(e2), lc, lightCenter);
                                            cBlock[s1][s2] = static_cast<float>(b.block) / 16.0f;
                                            cSky[s1][s2] = static_cast<float>(b.sky) / 16.0f;
                                        }
                                }

                                float base[3] = {1, 1, 1};
                                const std::string tintTexture = q.tile || q.tex >= bm.Textures().size()
                                                                    ? library->TilePathOf(tile)
                                                                    : bm.Textures()[q.tex];
                                {
                                    const std::string& tn0 = tintTexture;
                                    if (tn0.contains("bamboo_leaf") || tn0.contains("bamboo_small_leaf"))
                                        base[0] = base[1] = base[2] = 0.63f;
                                }
                                if (q.tint >= 0 && !q.tile) {
                                    const std::string& tn = tintTexture;
                                    const auto tc = biomeTintFor(tn, tile, dir == 1);
                                    if (tc[3]) {
                                        base[0] = tc[0];
                                        base[1] = tc[1];
                                        base[2] = tc[2];
                                    } else if (app && (app->tintTop || app->tintSide || app->tintBottom)) {
                                        base[0] = app->tintR;
                                        base[1] = app->tintG;
                                        base[2] = app->tintB;
                                    }
                                }
                                float u0, v0uv, u1, v1uv;
                                BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                                const float dirShade = q.shade ? kDirShade[q.shadeDir] : 1.0f;
                                static constexpr int kOrder[4] = {0, 3, 2, 1};
                                float v[4][3], col[4][3], uv[4][2], lt[4][2], vertexAo[4];
                                for (int k = 0; k < 4; k++) {
                                    const int i = kOrder[k];
                                    v[k][0] = bx + q.pos[i][0] + mox;
                                    v[k][1] = by + q.pos[i][1] + moy;
                                    v[k][2] = bz + q.pos[i][2] + moz;
                                    const float p1 = std::clamp(q.pos[i][axis1], 0.0f, 1.0f);
                                    const float p2 = std::clamp(q.pos[i][axis2], 0.0f, 1.0f);
                                    float ao = 0, bl = 0, sk = 0;
                                    for (int s1 = 0; s1 < 2; s1++)
                                        for (int s2 = 0; s2 < 2; s2++) {
                                            const float w = (s1 ? p1 : 1 - p1) * (s2 ? p2 : 1 - p2);
                                            ao += w * cAo[s1][s2];
                                            bl += w * cBlock[s1][s2];
                                            sk += w * cSky[s1][s2];
                                        }
                                    if (q.light) {
                                        bl = std::max(bl, static_cast<float>(q.light));
                                        sk = std::max(sk, static_cast<float>(q.light));
                                    }
                                    vertexAo[k] = ao;
                                    const float kv = ao * dirShade;
                                    col[k][0] = base[0] * kv;
                                    col[k][1] = base[1] * kv;
                                    col[k][2] = base[2] * kv;
                                    lt[k][0] = bl;
                                    lt[k][1] = sk;
                                    uv[k][0] = u0 + (quv[i][0] / 16.0f) * (u1 - u0);
                                    uv[k][1] = v0uv + (quv[i][1] / 16.0f) * (v1uv - v0uv);
                                }
                                tess.SetLayer(ta.translucent ? RenderLayer::TranslucentCull
                                              : ta.holes     ? RenderLayer::CutoutCull
                                                             : RenderLayer::Solid);
                                tess.AddQuad(v, col, uv, false, lt, vertexAo, dirShade);
                            };
                            if (modelEntry->direct) {
                                for (const ModelQuad& q : modelEntry->quads)
                                    emitQuad(q);
                                continue;
                            }
                            JavaRandom rng(JavaPosSeed(baseX + x, y, baseZ + z));
                            for (const auto& group : modelEntry->groups) {
                                const ModelEntry::Choice* pick = nullptr;
                                if (group.size() == 1) {
                                    pick = &group[0];
                                } else if (!group.empty()) {
                                    int total = 0;
                                    for (const auto& ch : group)
                                        total += ch.weight;
                                    int r = rng.NextInt(total > 0 ? total : 1);
                                    for (const auto& ch : group) {
                                        r -= ch.weight;
                                        if (r < 0) {
                                            pick = &ch;
                                            break;
                                        }
                                    }
                                    if (!pick)
                                        pick = &group[0];
                                }
                                if (pick)
                                    for (const ModelQuad& q : bm.Variant(pick->variant))
                                        emitQuad(q);
                            }
                            continue;
                        }

                        if (app && app->shape == BlockAppearance::Shape::Flat) {
                            const std::vector<int>& fv = app->varTop.empty() ? app->varSide : app->varTop;
                            int tile = PickTile(fv, app->top ? app->top : app->side, bhash);
                            float baseRGB[3];
                            bool maskGrey = !library || library->SaturationOf(tile) < 0.25f;
                            if (app->hasCarriedRGB && maskGrey) {
                                baseRGB[0] = app->carriedR;
                                baseRGB[1] = app->carriedG;
                                baseRGB[2] = app->carriedB;
                            } else {
                                auto tc = biomeTintFor(app->texTop.empty() ? app->texSide : app->texTop, tile,
                                                       true);
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
                            float top[4][3] = {{fx0, by + h, fz0},
                                               {fx1, by + h, fz0},
                                               {fx1, by + h, fz1},
                                               {fx0, by + h, fz1}};
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
                            float px[4][3] = {
                                {fx1, by + h, fz0}, {fx1, by, fz0}, {fx1, by, fz1}, {fx1, by + h, fz1}};
                            float nx[4][3] = {
                                {fx0, by + h, fz1}, {fx0, by, fz1}, {fx0, by, fz0}, {fx0, by + h, fz0}};
                            float pz[4][3] = {
                                {fx0, by + h, fz1}, {fx0, by, fz1}, {fx1, by, fz1}, {fx1, by + h, fz1}};
                            float nz[4][3] = {
                                {fx1, by + h, fz0}, {fx1, by, fz0}, {fx0, by, fz0}, {fx0, by + h, fz0}};
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
                            continue;
                        }

                        if (app && app->shape == BlockAppearance::Shape::Wall) {
                            int tile = app->side ? app->side : app->top;
                            std::string tex = app->texSide.empty() ? app->texTop : app->texSide;
                            auto tc = biomeTintFor(tex, tile, false);
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
                                float va[4][3] = {{bx, by, bz + 1},
                                                  {bx, by + 1, bz + 1},
                                                  {bx + 1, by + 1, bz},
                                                  {bx + 1, by, bz}};
                                float vb[4][3] = {{bx, by, bz},
                                                  {bx, by + 1, bz},
                                                  {bx + 1, by + 1, bz + 1},
                                                  {bx + 1, by, bz + 1}};
                                tess.AddQuad(va, colW, uvw, false, cellLight.block, cellLight.sky);
                                tess.AddQuad(vb, colW, uvw, false, cellLight.block, cellLight.sky);
                            }
                            continue;
                        }

                        if (gi.fire && app) {
                            const uint16_t nIds[5] = {cache.Get(x, y, z - 1), cache.Get(x + 1, y, z),
                                                      cache.Get(x, y, z + 1), cache.Get(x - 1, y, z),
                                                      cache.Get(x, y + 1, z)};
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
                            auto rotate = [](float p[3], int axis, float deg, const float o[3],
                                             bool rescale) {
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
                            auto plane = [&](const float A[3], const float R[3], const float Uv[3], int axis,
                                             float deg, const float origin[3], int variant, bool flipU) {
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
                            const int pick0 = static_cast<int>(bhash & 1),
                                      pick1 = static_cast<int>((bhash >> 1) & 1);
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
                            continue;
                        }

                        if (app && app->shape == BlockAppearance::Shape::Cross) {
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
                            auto tc = biomeTintFor(tex, tile, true);
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
                            continue;
                        }

                        const int liquidKind = app ? LiquidKind(library, id) : 0;
                        const bool isWater = liquidKind != 0;
                        const bool translucent =
                            liquidKind == 1 || (liquidKind == 0 && textured && IsTranslucentId(library, id));
                        int ovTile = 0;
                        float ovR = 1, ovG = 1, ovB = 1;
                        auto face = [&](int nx, int ny, int nz, int tile, float tintR, float tintG,
                                        float tintB, bool tinted, bool known, float shade, const int c[4][3],
                                        const float v[4][3]) {
                            const uint16_t neighborId = cache.Get(x + nx, y + ny, z + nz);
                            if (neighborId != 0 && cache.Occ(x + nx, y + ny, z + nz))
                                return;
                            if (neighborId != 0 && (translucent || (library && library->IsGlassBase(tile))) &&
                                (neighborId == id || (library && library->GlassMode() && library->IsGlassBase(tile) &&
                                                      library->Look(neighborId).side == tile)))
                                return;
                            if (isWater) {
                                const uint16_t other = EffLiquid(cache, library, x + nx, y + ny, z + nz);
                                if (other != 0 && LiquidKind(library, other) == liquidKind)
                                    return;
                            }
                            int glassMask = 15;
                            int glassModeNow = 0;
                            if (const int glassMode = library ? library->GlassMode() : 0;
                                glassMode && library->IsGlassBase(tile)) {
                                glassModeNow = glassMode;
                                int du[3] = {0, 0, 0}, dv[3] = {0, 0, 0};
                                if (nx != 0) {
                                    du[2] = nx > 0 ? -1 : 1;
                                    dv[1] = -1;
                                } else if (nz != 0) {
                                    du[0] = nz < 0 ? -1 : 1;
                                    dv[1] = -1;
                                } else {
                                    du[0] = 1;
                                    dv[2] = ny > 0 ? 1 : -1;
                                }
                                // same rule as the cull above and the model path: same id or same glass look,
                                // and the neighbour's face on this side not covered by more of that glass
                                const int baseTile = tile;
                                const auto same = [&](uint16_t nn) {
                                    return nn != 0 && (nn == id || library->Look(nn).side == baseTile);
                                };
                                const auto sameGlass = [&](int sx, int sy, int sz) {
                                    return same(cache.Get(x + sx, y + sy, z + sz)) &&
                                           !same(cache.Get(x + sx + nx, y + sy + ny, z + sz + nz));
                                };
                                int mask = 0;
                                if (!sameGlass(-du[0], -du[1], -du[2]))
                                    mask |= 1;
                                if (!sameGlass(du[0], du[1], du[2]))
                                    mask |= 2;
                                if (!sameGlass(-dv[0], -dv[1], -dv[2]))
                                    mask |= 4;
                                if (!sameGlass(dv[0], dv[1], dv[2]))
                                    mask |= 8;
                                glassMask = mask;
                                tile = library->GlassTile(tile, glassMode, mask);
                            }
                            float u0, v0uv, u1, v1uv;
                            BlockLibrary::TileUV(tile, u0, v0uv, u1, v1uv);
                            float uv[4][2];
                            float fuA[4], fvA[4];
                            for (int i = 0; i < 4; i++) {
                                float fu, fv;
                                if (nx != 0) {
                                    fu = (float)(c[i][2] - z);
                                    if (nx > 0)
                                        fu = 1.0f - fu;
                                    fv = 1.0f - (float)(c[i][1] - y);
                                } else if (nz != 0) {
                                    fu = (float)(c[i][0] - x);
                                    if (nz < 0)
                                        fu = 1.0f - fu;
                                    fv = 1.0f - (float)(c[i][1] - y);
                                } else {
                                    fu = (float)(c[i][0] - x);
                                    fv = (ny > 0) ? (float)(c[i][2] - z) : 1.0f - (float)(c[i][2] - z);
                                }
                                fuA[i] = fu;
                                fvA[i] = fv;
                                if (glassModeNow == 1 || glassModeNow == 2) {
                                    const float k = BlockLibrary::GlassInset();
                                    if (!(glassMask & 1) && fu < 0.001f)
                                        fu = k;
                                    else if (!(glassMask & 2) && fu > 0.999f)
                                        fu = 1.0f - k;
                                    if (!(glassMask & 4) && fv < 0.001f)
                                        fv = k;
                                    else if (!(glassMask & 8) && fv > 0.999f)
                                        fv = 1.0f - k;
                                }
                                uv[i][0] = u0 + fu * (u1 - u0);
                                uv[i][1] = v0uv + fv * (v1uv - v0uv);
                            }
                            float base[3];
                            if (!textured) {
                                base[0] = 0.6f;
                                base[1] = 0.6f;
                                base[2] = 0.6f;
                            } else if (!known) {
                                base[0] = base[1] = base[2] = 0.6f;
                            } else if (tinted) {
                                base[0] = tintR;
                                base[1] = tintG;
                                base[2] = tintB;
                            } else {
                                base[0] = base[1] = base[2] = 1.0f;
                            }
                            float col[4][3];
                            float lt[4][2];
                            float kv[4];
                            float aoV[4];
                            const int origin[3] = {x, y, z};
                            const int center[3] = {x + nx, y + ny, z + nz};
                            const int axis1 = nx != 0 ? 1 : 0;
                            const int axis2 = nz != 0 ? 1 : 2;
                            const int normal[3] = {nx, ny, nz};
                            int first[3] = {0, 0, 0};
                            if (ny < 0)
                                first[0] = -1;
                            else if (ny > 0)
                                first[0] = 1;
                            else if (nz < 0)
                                first[1] = 1;
                            else if (nz > 0)
                                first[0] = -1;
                            else if (nx < 0)
                                first[1] = 1;
                            else
                                first[1] = -1;
                            struct Packed {
                                int block, sky;
                            };
                            auto coordsAt = [&](const int p[3]) {
                                const std::array<int, 2> l = sample(p[0], p[1], p[2]);
                                return Packed{l[0] * 16, l[1] * 16};
                            };
                            auto smoothBlend = [](Packed n1, Packed n2, Packed n3, Packed c) {
                                if (c.sky > 2 * 16 - 1 || c.block > 2 * 16 - 1) {
                                    for (Packed* n : {&n1, &n2, &n3}) {
                                        if (n->block == 0 && n->sky == 0)
                                            *n = c;
                                        else if (n->sky == 0)
                                            n->sky = c.sky;
                                    }
                                }
                                return Packed{(n1.block + n2.block + n3.block + c.block) >> 2,
                                              (n1.sky + n2.sky + n3.sky + c.sky) >> 2};
                            };
                            const float shadeCenter =
                                AoShade(cache, library, center[0], center[1], center[2]);
                            const Packed lightCenter = coordsAt(center);
                            for (int i = 0; i < 4; i++) {
                                if (isWater) {
                                    aoV[i] = 1.0f;
                                    lt[i][0] = cellLight.block;
                                    lt[i][1] = cellLight.sky;
                                } else {
                                    const int d1 = c[i][axis1] - origin[axis1] > 0 ? 1 : -1;
                                    const int d2 = c[i][axis2] - origin[axis2] > 0 ? 1 : -1;
                                    int e1[3] = {center[0], center[1], center[2]};
                                    int e2[3] = {center[0], center[1], center[2]};
                                    int corner[3] = {center[0], center[1], center[2]};
                                    e1[axis1] += d1;
                                    e2[axis2] += d2;
                                    corner[axis1] += d1;
                                    corner[axis2] += d2;
                                    const bool permeable1 =
                                        !cache.Occ(e1[0] + normal[0], e1[1] + normal[1], e1[2] + normal[2]);
                                    const bool permeable2 =
                                        !cache.Occ(e2[0] + normal[0], e2[1] + normal[1], e2[2] + normal[2]);
                                    const int fb[3] = {center[0] + first[0], center[1] + first[1],
                                                       center[2] + first[2]};
                                    float sc;
                                    Packed lc;
                                    if (!permeable1 && !permeable2) {
                                        sc = AoShade(cache, library, fb[0], fb[1], fb[2]);
                                        lc = coordsAt(fb);
                                    } else {
                                        sc = AoShade(cache, library, corner[0], corner[1], corner[2]);
                                        lc = coordsAt(corner);
                                    }
                                    const float s1 = AoShade(cache, library, e1[0], e1[1], e1[2]);
                                    const float s2 = AoShade(cache, library, e2[0], e2[1], e2[2]);
                                    aoV[i] = (s1 + s2 + sc + shadeCenter) * 0.25f;
                                    const Packed blended =
                                        smoothBlend(coordsAt(e1), coordsAt(e2), lc, lightCenter);
                                    lt[i][0] = static_cast<float>(blended.block) / 16.0f;
                                    lt[i][1] = static_cast<float>(blended.sky) / 16.0f;
                                }
                                kv[i] = aoV[i] * shade;
                                col[i][0] = base[0] * kv[i];
                                col[i][1] = base[1] * kv[i];
                                col[i][2] = base[2] * kv[i];
                            }
                            const bool flip = nx != 0;
                            const bool holes = library && library->AlphaOf(tile).holes;
                            const RenderLayer faceLayer = translucent ? RenderLayer::Translucent
                                                          : holes     ? RenderLayer::CutoutCull
                                                                      : RenderLayer::Solid;
                            tess.SetLayer(faceLayer);
                            tess.AddQuad(v, col, uv, flip, lt, aoV, shade);
                            if (ovTile && ny == 0) {
                                float ou0, ov0, ou1, ov1;
                                BlockLibrary::TileUV(ovTile, ou0, ov0, ou1, ov1);
                                const float in = 0.02f / 16.0f;
                                float uv2[4][2], col2[4][3];
                                for (int i = 0; i < 4; i++) {
                                    float fu = in + fuA[i] * (1.0f - 2 * in);
                                    float fv = in + fvA[i] * (1.0f - 2 * in);
                                    uv2[i][0] = ou0 + fu * (ou1 - ou0);
                                    uv2[i][1] = ov0 + fv * (ov1 - ov0);
                                    col2[i][0] = ovR * kv[i];
                                    col2[i][1] = ovG * kv[i];
                                    col2[i][2] = ovB * kv[i];
                                }
                                tess.SetLayer(RenderLayer::Cutout);
                                tess.AddQuad(v, col2, uv2, flip, lt, aoV, shade);
                                tess.SetLayer(faceLayer);
                            }
                        };

                        float yBot = by, yTop = by + 1.0f;
                        float h00 = -1, h10 = -1, h01 = -1, h11 = -1;
                        if (isWater) {
                            constexpr float kLift = 0.001f;
                            h00 = by + WaterCornerHeight(cache, library, liquidKind, y, x, z) - kLift;
                            h10 = by + WaterCornerHeight(cache, library, liquidKind, y, x + 1, z) - kLift;
                            h01 = by + WaterCornerHeight(cache, library, liquidKind, y, x, z + 1) - kLift;
                            h11 = by + WaterCornerHeight(cache, library, liquidKind, y, x + 1, z + 1) - kLift;
                            yTop = std::max(std::max(h00, h10), std::max(h01, h11));
                        } else if (gi.slab) {
                            if (gi.slabTop)
                                yBot = by + 0.5f;
                            else
                                yTop = by + 0.5f;
                        }
                        float c00 = isWater ? h00 : yTop;
                        float c10 = isWater ? h10 : yTop;
                        float c11 = isWater ? h11 : yTop;
                        float c01 = isWater ? h01 : yTop;
                        float top[4][3] = {
                            {bx, c00, bz}, {bx + 1, c10, bz}, {bx + 1, c11, bz + 1}, {bx, c01, bz + 1}};
                        float bottom[4][3] = {
                            {bx, yBot, bz + 1}, {bx + 1, yBot, bz + 1}, {bx + 1, yBot, bz}, {bx, yBot, bz}};
                        float px[4][3] = {{bx + 1, c10, bz},
                                          {bx + 1, yBot, bz},
                                          {bx + 1, yBot, bz + 1},
                                          {bx + 1, c11, bz + 1}};
                        float nx[4][3] = {
                            {bx, c01, bz + 1}, {bx, yBot, bz + 1}, {bx, yBot, bz}, {bx, c00, bz}};
                        float pz[4][3] = {{bx, c01, bz + 1},
                                          {bx + 1, c11, bz + 1},
                                          {bx + 1, yBot, bz + 1},
                                          {bx, yBot, bz + 1}};
                        float nz[4][3] = {
                            {bx + 1, c10, bz}, {bx, c00, bz}, {bx, yBot, bz}, {bx + 1, yBot, bz}};

                        int tTop = 0, tSide = 0, tBottom = 0;
                        bool tintT = false, tintS = false, tintB = false, known = false;
                        float tr = 1, tg = 1, tb = 1;
                        static const std::string kNoTex;
                        const std::string* useTopTexP = &kNoTex;
                        const std::string* useSideTexP = &kNoTex;
                        const std::string* useBotTexP = &kNoTex;
                        if (app) {
                            tTop = PickTile(app->varTop, app->top, bhash);
                            tSide = PickTile(app->varSide, app->side, bhash);
                            tBottom = PickTile(app->varBottom, app->bottom, bhash);
                            tintT = app->tintTop;
                            tintS = app->tintSide;
                            tintB = app->tintBottom;
                            tr = app->tintR;
                            tg = app->tintG;
                            tb = app->tintB;
                            known = (tTop | tSide | tBottom) != 0;
                            useTopTexP = &app->texTop;
                            useSideTexP = &app->texSide;
                            useBotTexP = &app->texBottom;
                        }
                        const std::string& useTopTex = *useTopTexP;
                        const std::string& useSideTex = *useSideTexP;
                        const std::string& useBotTex = *useBotTexP;
                        const auto shut = [&](int dx, int dy, int dz) {
                            return cache.Get(x + dx, y + dy, z + dz) != 0 && cache.Occ(x + dx, y + dy, z + dz);
                        };
                        const bool sideOpen = !shut(1, 0, 0) || !shut(-1, 0, 0) || !shut(0, 0, 1) || !shut(0, 0, -1);
                        const bool needTop = !shut(0, 1, 0) || sideOpen;
                        const bool needBottom = !shut(0, -1, 0);
                        float trT = tr, tgT = tg, tbT = tb;
                        float trS = tr, tgS = tg, tbS = tb;
                        float trB = tr, tgB = tg, tbB = tb;
                        bool fT = tintT, fS = tintS, fB = tintB;
                        if (known) {
                            static constexpr std::array<float, 4> kSkip{0, 0, 0, 0};
                            const auto ttT = needTop ? biomeTintFor(useTopTex, tTop, true) : kSkip;
                            const auto ttS = sideOpen ? biomeTintFor(useSideTex, tSide, false) : kSkip;
                            const auto ttB = needBottom ? biomeTintFor(useBotTex, tBottom, false) : kSkip;
                            if (ttT[3]) {
                                trT = ttT[0];
                                tgT = ttT[1];
                                tbT = ttT[2];
                                fT = true;
                            }
                            if (ttS[3]) {
                                trS = ttS[0];
                                tgS = ttS[1];
                                tbS = ttS[2];
                                fS = true;
                            }
                            if (ttB[3]) {
                                trB = ttB[0];
                                tgB = ttB[1];
                                tbB = ttB[2];
                                fB = true;
                            }
                        }
                        const int cTop[4][3] = {
                            {x, y + 1, z}, {x + 1, y + 1, z}, {x + 1, y + 1, z + 1}, {x, y + 1, z + 1}};
                        const int cBottom[4][3] = {
                            {x, y, z + 1}, {x + 1, y, z + 1}, {x + 1, y, z}, {x, y, z}};
                        const int cPx[4][3] = {
                            {x + 1, y + 1, z}, {x + 1, y, z}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}};
                        const int cNx[4][3] = {{x, y + 1, z + 1}, {x, y, z + 1}, {x, y, z}, {x, y + 1, z}};
                        const int cPz[4][3] = {
                            {x, y + 1, z + 1}, {x + 1, y + 1, z + 1}, {x + 1, y, z + 1}, {x, y, z + 1}};
                        const int cNz[4][3] = {{x + 1, y + 1, z}, {x, y + 1, z}, {x, y, z}, {x + 1, y, z}};
                        if (app && app->overlayFrac > 0) {
                            static bool once = false;
                            if (!once) {
                                once = true;
                                printf("[blocks] grass top: haveTints=%d known=%d fT=%d "
                                       "tile=%d sat=%.2f biome=%s tint=(%.2f,%.2f,%.2f)\n",
                                       (int)haveTints, (int)known, (int)fT, tTop,
                                       library ? library->SaturationOf(tTop) : -1.0f, biomeName.c_str(), trT,
                                       tgT, tbT);
                            }
                        }
                        tess.SetLayer(translucent ? RenderLayer::Translucent : RenderLayer::Solid);
                        if (isWater) {
                            face(0, 1, 0, tTop, trT, tgT, tbT, true, known, kShadeTop, cTop, top);
                            face(0, -1, 0, tBottom ? tBottom : tTop, trB, tgB, tbB, true, known, kShadeBottom,
                                 cBottom, bottom);
                            face(1, 0, 0, tSide, trS, tgS, tbS, true, known, kShadeX, cPx, px);
                            face(-1, 0, 0, tSide, trS, tgS, tbS, true, known, kShadeX, cNx, nx);
                            face(0, 0, 1, tSide, trS, tgS, tbS, true, known, kShadeZ, cPz, pz);
                            face(0, 0, -1, tSide, trS, tgS, tbS, true, known, kShadeZ, cNz, nz);
                        } else {
                            face(0, 1, 0, tTop, trT, tgT, tbT, fT, known, kShadeTop, cTop, top);
                            face(0, -1, 0, tBottom, trB, tgB, tbB, fB, known, kShadeBottom, cBottom, bottom);
                            if (app && app->overlayFrac > 0 && app->overlayTile && !gi.slab && fT && tSide) {
                                ovTile = app->overlayTile;
                                ovR = trT;
                                ovG = tgT;
                                ovB = tbT;
                            }
                            if (ovTile && library && library->BetterGrass() && tTop) {
                                ovTile = 0;
                                face(1, 0, 0, tTop, trT, tgT, tbT, fT, known, kShadeX, cPx, px);
                                face(-1, 0, 0, tTop, trT, tgT, tbT, fT, known, kShadeX, cNx, nx);
                                face(0, 0, 1, tTop, trT, tgT, tbT, fT, known, kShadeZ, cPz, pz);
                                face(0, 0, -1, tTop, trT, tgT, tbT, fT, known, kShadeZ, cNz, nz);
                            } else {
                                const auto sideOf = [&](int i) {
                                    return app && app->sideTile[i] ? app->sideTile[i] : tSide;
                                };
                                face(1, 0, 0, sideOf(0), trS, tgS, tbS, fS, known, kShadeX, cPx, px);
                                face(-1, 0, 0, sideOf(1), trS, tgS, tbS, fS, known, kShadeX, cNx, nx);
                                face(0, 0, 1, sideOf(2), trS, tgS, tbS, fS, known, kShadeZ, cPz, pz);
                                face(0, 0, -1, sideOf(3), trS, tgS, tbS, fS, known, kShadeZ, cNz, nz);
                            }
                            ovTile = 0;
                        }
                    }
                }
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
