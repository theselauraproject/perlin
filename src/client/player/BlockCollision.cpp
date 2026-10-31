#include "client/player/BlockCollision.hpp"
#include "client/player/VoxelShapes.hpp"
#include "client/renderer/block/BlockGeometry.hpp"
#include "client/resources/ServerBlocks.hpp"

#include "client/renderer/block/BlockLibrary.hpp"
#include "util/Paths.hpp"
#include "util/Resources.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>

namespace perlin {
    namespace {
        using physics::BlockShape;
        using physics::Box;

        bool Ends(const std::string& s, const char* suffix) {
            const size_t n = std::strlen(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        bool Has(const std::string& s, const char* part) {
            return s.contains(part);
        }

        void Add(BlockShape& s, double x0, double y0, double z0, double x1, double y1, double z1) {
            s.boxes.push_back({x0, y0, z0, x1, y1, z1});
        }

        int State(uint16_t id, const char* key, int fallback) {
            const BlockPalette& palette = BlockPalette::Instance();
            const std::string prefixed = std::string("minecraft:") + key;
            if (palette.HasState(id, key))
                return palette.StateInt(id, key, fallback);
            return palette.StateInt(id, prefixed, fallback);
        }

        std::string StateText(uint16_t id, const char* key, const char* fallback) {
            const BlockPalette& palette = BlockPalette::Instance();
            if (palette.HasState(id, key))
                return palette.StateString(id, key, fallback);
            return palette.StateString(id, std::string("minecraft:") + key, fallback);
        }

        bool HasAnyState(uint16_t id, const char* key) {
            const BlockPalette& palette = BlockPalette::Instance();
            return palette.HasState(id, key) || palette.HasState(id, std::string("minecraft:") + key);
        }

        // cardinal_direction value index: south 0, west 1, north 2, east 3
        int DoorDirection(uint16_t id) {
            const std::string card = StateText(id, "cardinal_direction", "");
            if (card.empty())
                return State(id, "direction", 0);
            return card == "south" ? 0 : card == "west" ? 1 : card == "north" ? 2 : 3;
        }

        // DoorBlock 0x149c5b200: 0.1825 thick (0x3E3AE148), not 3/16
        void AddDoor(physics::BlockShape& s, int dir, bool open, bool hinge) {
            // 0 north side, 1 east side, 2 south side, 3 west side
            int side = 3;
            switch (dir) {
            case 0: side = open ? (hinge ? 2 : 0) : 3; break;
            case 1: side = open ? (hinge ? 3 : 1) : 0; break;
            case 2: side = open ? (hinge ? 0 : 2) : 1; break;
            case 3: side = open ? (hinge ? 1 : 3) : 2; break;
            default: break;
            }
            switch (side) {
            case 0: Add(s, 0, 0, 0, 1, 1, 0.1825f); break;
            case 1: Add(s, 0.8175f, 0, 0, 1, 1, 1); break;
            case 2: Add(s, 0, 0, 0.8175f, 1, 1, 1); break;
            default: Add(s, 0, 0, 0, 0.1825f, 1, 1); break;
            }
        }

        // PointedDripstoneBlock 0x148f97240 boxes by dripstone_thickness; tip follows "hanging"
        void AddDripstone(physics::BlockShape& s, uint16_t id) {
            const std::string thickness = StateText(id, "dripstone_thickness", "tip");
            if (thickness == "frustum") Add(s, 0.25, 0, 0.25, 0.75, 1, 0.75);
            else if (thickness == "middle") Add(s, 0.1875, 0, 0.1875, 0.8125, 1, 0.8125);
            else if (thickness == "base") Add(s, 0.125, 0, 0.125, 0.875, 1, 0.875);
            else if (thickness == "merge") Add(s, 0.3125, 0, 0.3125, 0.6875, 1, 0.6875);
            else if (State(id, "hanging", 0)) Add(s, 0.3125, 0.3125, 0.3125, 0.6875, 1, 0.6875);
            else Add(s, 0.3125, 0, 0.3125, 0.6875, 0.6875, 0.6875);
        }

        // block_physics.bin is wrong for these: 6-decimal rounding (chests, trapdoors, lectern, turtle egg),
        // offsets baked for the origin (bamboo, dripstone), or states newer than the dump
        bool AddComputedShape(const std::string& n, uint16_t id, physics::BlockShape& s) {
            // TrapDoorBlock 0x1449bbef0: 0.1825 thick, not 3/16
            if (Ends(n, "_trapdoor") || n == "trapdoor") {
                if (State(id, "open_bit", 0)) {
                    switch (State(id, "direction", 0) & 3) {
                    case 0: Add(s, 0, 0, 0, 0.1825f, 1, 1); break;
                    case 1: Add(s, 0.8175f, 0, 0, 1, 1, 1); break;
                    case 2: Add(s, 0, 0, 0, 1, 1, 0.1825f); break;
                    default: Add(s, 0, 0, 0.8175f, 1, 1, 1); break;
                    }
                } else if (State(id, "upside_down_bit", 0)) {
                    Add(s, 0, 0.8175f, 0, 1, 1, 1);
                } else {
                    Add(s, 0, 0, 0, 1, 0.1825f, 1);
                }
                return true;
            }
            // ChestBlock 0x14d190ef0: top is 0x3F733334, one ulp above 0.95f
            if (Ends(n, "chest")) {
                Add(s, 0.025f, 0, 0.025f, 0.975f, 0.950000048f, 0.975f);
                return true;
            }
            // LecternBlock 0x143c88b95
            if (n == "lectern") {
                Add(s, 0, 0, 0, 1, 0.9f, 1);
                return true;
            }
            // TurtleEggBlock 0x1449c3860: 0x3E4CCCCC and 0x3EE66667, not 0.2f / 0.45f
            if (n == "turtle_egg") {
                Add(s, 0.199999988f, 0, 0.199999988f, 0.8f, 0.450000018f, 0.8f);
                return true;
            }
            // BambooBlock 0x143148970; random offset 0x1529d1160: x, z in [-0.25, 0.25], 16 steps
            if (n == "bamboo") {
                const double side = StateText(id, "bamboo_stalk_thickness", "thin") == "thick" ? 0.6875 : 0.625;
                Add(s, 0.5, 0, 0.5, side, 1, side);
                s.randomOffset = physics::bds::BlockRandomOffset{{{{-0.25f, 0.25f, 16}, {0, 0, 0}, {-0.25f, 0.25f, 16}}}};
                return true;
            }
            // dripstone registration 0x14cf3264f: x, z in [-0.125, 0.125], 16 steps; icicle has no offset
            if (n == "pointed_dripstone" || n == "sulfur_spike" || n == "icicle") {
                AddDripstone(s, id);
                if (n != "icicle")
                    s.randomOffset = physics::bds::BlockRandomOffset{{{{-0.125f, 0.125f, 16}, {0, 0, 0}, {-0.125f, 0.125f, 16}}}};
                return true;
            }
            // ChorusPlantBlock 0x14a302920: the 0.125 inset core reaches each face with a connection state
            if (n == "chorus_plant") {
                const auto side = [&](const char* key) { return State(id, key, 0) != 0; };
                Add(s, side("connection_west") ? 0 : 0.125, side("connection_down") ? 0 : 0.125,
                    side("connection_north") ? 0 : 0.125, side("connection_east") ? 1 : 0.875,
                    side("connection_up") ? 1 : 0.875, side("connection_south") ? 1 : 0.875);
                return true;
            }
            // AmethystClusterBlock 0x149c4eed0 with the cluster size (height 7, inset 3 pixels, ctor 0x14d029886)
            if (n == "ice_crystal") {
                constexpr double h = 7 / 16.0, i = 3 / 16.0;
                const std::string face = StateText(id, "block_face", "up");
                if (face == "down") Add(s, i, 1 - h, i, 1 - i, 1, 1 - i);
                else if (face == "north") Add(s, i, i, 1 - h, 1 - i, 1 - i, 1);
                else if (face == "south") Add(s, i, i, 0, 1 - i, 1 - i, h);
                else if (face == "west") Add(s, 1 - h, i, i, 1, 1 - i, 1 - i);
                else if (face == "east") Add(s, 0, i, i, h, 1 - i, 1 - i);
                else Add(s, i, 0, i, 1 - i, h, 1 - i);
                return true;
            }
            return false;
        }

        struct PhysRecord {
            uint8_t flags = 0, surface = 0;
            float friction = 0.6f;
            std::vector<Box> boxes;
        };
        const std::unordered_map<int32_t, PhysRecord>& Registry() {
            static const std::unordered_map<int32_t, PhysRecord> table = [] {
                std::unordered_map<int32_t, PhysRecord> t;
                for (int once = 0; once < 1; once++) {
                    std::string raw;
                    if (!resources::Read("block_physics.bin", raw))
                        continue;
                    std::vector<uint8_t> d(raw.begin(), raw.end());
                    if (d.size() < 8 || std::memcmp(d.data(), "PHY1", 4) != 0)
                        continue;
                    uint32_t count;
                    std::memcpy(&count, d.data() + 4, 4);
                    size_t p = 8;
                    auto have = [&](size_t k) { return p + k <= d.size(); };
                    for (uint32_t i = 0; i < count && have(22); i++) {
                        int32_t hash;
                        PhysRecord r;
                        float hs, vs, fh;
                        std::memcpy(&hash, &d[p], 4);
                        r.flags = d[p + 4];
                        r.surface = d[p + 5];
                        std::memcpy(&r.friction, &d[p + 6], 4);
                        std::memcpy(&hs, &d[p + 10], 4);
                        std::memcpy(&vs, &d[p + 14], 4);
                        std::memcpy(&fh, &d[p + 18], 4);
                        p += 22;
                        if (!have(1))
                            break;
                        const int nb = d[p++];
                        if (!have(static_cast<size_t>(nb) * 24))
                            break;
                        for (int b = 0; b < nb; b++, p += 24) {
                            float v[6];
                            std::memcpy(v, &d[p], 24);
                            r.boxes.push_back({v[0], v[1], v[2], v[3], v[4], v[5]});
                        }
                        t.emplace(hash, std::move(r));
                    }
                    std::printf("[physics] block registry: %zu records from %s\n", t.size(), "embedded block_physics.bin");
                    break;
                }
                if (t.empty())
                    std::printf("[physics] block_physics.bin not found; using heuristic collision shapes\n");
                return t;
            }();
            return table;
        }

        bool Passable(const std::string& n) {
            static const char* const exact[] = {
                "short_grass", "tallgrass", "tall_grass", "fern", "large_fern", "double_plant", "yellow_flower",
                "red_flower", "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip",
                "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley",
                "wither_rose", "torchflower", "pitcher_plant", "pitcher_crop", "sunflower", "lilac", "rose_bush",
                "peony", "brown_mushroom", "red_mushroom", "crimson_fungus", "warped_fungus", "crimson_roots",
                "warped_roots", "nether_sprouts", "hanging_roots", "glow_lichen", "vine", "weeping_vines",
                "twisting_vines", "cave_vines", "cave_vines_body_with_berries", "cave_vines_head_with_berries",
                "reeds", "sugar_cane", "bamboo_sapling", "wheat", "carrots", "potatoes", "beetroot", "melon_stem",
                "pumpkin_stem", "sweet_berry_bush", "nether_wart", "lever", "tripwire", "tripwire_hook", "string",
                "redstone_wire", "fire", "soul_fire", "dead_bush", "deadbush", "light_block", "structure_void",
                "portal", "end_portal", "end_gateway", "item_frame", "glow_frame", "sculk_vein", "powder_snow",
                "sea_pickle", "pink_petals", "wildflowers", "leaf_litter", "short_dry_grass", "tall_dry_grass",
                "firefly_bush", "bush", "cactus_flower", "end_rod", "chain", "lantern", "soul_lantern", "bell",
                "candle", "turtle_egg", "pointed_dripstone", "flower_pot", "mangrove_propagule", "spore_blossom",
                "big_dripleaf_stem", "small_dripleaf_block", "sniffer_egg", "frog_spawn", "cocoa", "hay_block_none",
                "rail", "golden_rail", "detector_rail", "activator_rail", "torch", "redstone_torch", "soul_torch",
                "copper_torch", "unlit_redstone_torch", "colored_torch_rg", "colored_torch_bp", "underwater_torch",
                "coral", "brewing_stand", "snow_none", "heavy_core_none", "air", "cave_air", "void_air"};
            for (const char* e : exact)
                if (n == e)
                    return true;
            if (n.starts_with("light_block") || n == "piston_arm_collision" || n == "sticky_piston_arm_collision")
                return true;
            return Ends(n, "_sapling") || Ends(n, "sapling") || Ends(n, "_pressure_plate") || Ends(n, "_button") ||
                   n == "stone_button" || Ends(n, "_sign") || Ends(n, "_banner") || n == "standing_banner" ||
                   n == "wall_banner" || Ends(n, "_coral") || Ends(n, "_coral_fan") || Ends(n, "_coral_wall_fan") ||
                   Ends(n, "_skull") || Ends(n, "_head") || n == "skull" || Ends(n, "_torch") || Ends(n, "_candle") ||
                   Ends(n, "_roots") || Ends(n, "_wall_hanging_sign") || Ends(n, "hanging_sign") ||
                   Ends(n, "_fungus") || Ends(n, "_vines") || Ends(n, "_stem_none") || Ends(n, "_rail");
        }
    }

    namespace {
        struct SelectionTable {
            struct Shape {
                uint8_t pick = 1;
                std::vector<physics::Box> boxes;
            };
            std::vector<Shape> shapes;
            std::unordered_map<int32_t, uint16_t> byHash;
        };

        const SelectionTable& Selections() {
            static const SelectionTable table = [] {
                SelectionTable t;
                std::string raw;
                if (!resources::Read("block_selection.bin", raw) || raw.size() < 12 || std::memcmp(raw.data(), "SEL2", 4) != 0)
                    return t;
                const uint8_t* d = reinterpret_cast<const uint8_t*>(raw.data());
                uint32_t shapeCount, recordCount;
                std::memcpy(&shapeCount, d + 4, 4);
                std::memcpy(&recordCount, d + 8, 4);
                size_t p = 12;
                const auto have = [&](size_t n) { return p + n <= raw.size(); };
                t.shapes.reserve(shapeCount);
                for (uint32_t i = 0; i < shapeCount && have(2); i++) {
                    SelectionTable::Shape s;
                    s.pick = d[p++];
                    const int n = d[p++];
                    if (!have(static_cast<size_t>(n) * 24 + 1))
                        return t;
                    for (int b = 0; b < n; b++, p += 24) {
                        float v[6];
                        std::memcpy(v, d + p, 24);
                        s.boxes.push_back({v[0], v[1], v[2], v[3], v[4], v[5]});
                    }
                    if (d[p++])
                        p += 24;
                    t.shapes.push_back(std::move(s));
                }
                t.byHash.reserve(recordCount);
                for (uint32_t i = 0; i < recordCount && have(6); i++, p += 6) {
                    uint32_t hash;
                    uint16_t idx;
                    std::memcpy(&hash, d + p, 4);
                    std::memcpy(&idx, d + p + 4, 2);
                    t.byHash.emplace(static_cast<int32_t>(hash), idx);
                }
                return t;
            }();
            return table;
        }
    }

    physics::bds::ScaffoldingCell LevelCollision::ClimberCell(int x, int y, int z) const {
        using Cell = physics::bds::ScaffoldingCell;
        bool loaded = false;
        const auto id = level.GetBlock(x, y, z, loaded);
        if (!loaded || id == 0) return Cell::Air;
        const auto name = BlockPalette::Instance().Name(id);
        if (name == "minecraft:scaffolding" || name == "scaffolding") return Cell::Scaffold;
        if (name == "minecraft:powder_snow" || name == "powder_snow") return Cell::PowderSnow;
        if (name == "minecraft:air" || name == "air") return Cell::Air;
        if (name == "minecraft:water" || name == "water" ||
            name == "minecraft:flowing_water" || name == "flowing_water") return Cell::Water;
        return Cell::Other;
    }

    bool LevelCollision::Selection(int x, int y, int z, std::vector<physics::Box>& out) const {
        out.clear();
        bool loaded = false;
        const uint16_t id = level.GetBlock(x, y, z, loaded);
        if (!loaded || id == 0)
            return false;
        const Info& info = Lookup(id, blocks);
        if (info.selectionDefined) {
            out = info.selection;
            return !out.empty();
        }
        if (info.kind == Kind::Plain || info.kind == Kind::Door) {
            const SelectionTable& t = Selections();
            const auto it = t.byHash.find(BlockPalette::Instance().NetworkId(id));
            if (it != t.byHash.end()) {
                const SelectionTable::Shape& s = t.shapes[it->second];
                if (s.pick != 1 || s.boxes.empty())
                    return false;
                out = s.boxes;
                return true;
            }
        }
        const physics::BlockShape shape = QueryMain(x, y, z);
        if (shape.fluid != 0)
            return false;
        if (shape.Empty()) {
            out.push_back({0, 0, 0, 1, 1, 1});
            return true;
        }
        for (physics::Box box : shape.boxes) {
            if (info.kind == Kind::Fence || info.kind == Kind::Wall)
                box.y1 = std::min(box.y1, 1.0);
            out.push_back(box);
        }
        return true;
    }

    const physics::BlockShape& LevelCollision::ShapeOf(uint16_t id) {
        return Lookup(id, nullptr).shape;
    }

    const LevelCollision::Info& LevelCollision::Lookup(uint16_t id, const BlockLibrary* blocks) {
        static std::array<std::atomic<Info*>, 65536> table{};
        static std::mutex publishMutex;
        static std::vector<std::unique_ptr<Info>> snapshots;
        for (;;) {
            const auto definitions = ServerBlocks::Instance().CollisionRevision();
            const auto voxels = VoxelShapeRegistry::Instance().Revision();
            Info* slot = table[id].load(std::memory_order_acquire);
            if (slot && slot->definitionRevision == definitions && slot->voxelRevision == voxels)
                return *slot;
            Info info = Classify(id, blocks);
            if (id != 0 && BlockPalette::Instance().Name(id).empty()) {
                static thread_local Info transient;
                transient = std::move(info);
                return transient;
            }
            info.ready = true;
            info.definitionRevision = definitions;
            info.voxelRevision = voxels;
            std::lock_guard lock(publishMutex);
            if (ServerBlocks::Instance().CollisionRevision() != definitions ||
                VoxelShapeRegistry::Instance().Revision() != voxels)
                continue;
            slot = table[id].load(std::memory_order_acquire);
            if (slot && slot->definitionRevision == definitions && slot->voxelRevision == voxels)
                return *slot;
            snapshots.push_back(std::make_unique<Info>(std::move(info)));
            Info* fresh = snapshots.back().get();
            table[id].store(fresh, std::memory_order_release);
            return *fresh;
        }
    }

    LevelCollision::Info LevelCollision::Classify(uint16_t id, const BlockLibrary* blocks) {
        CustomBlockCollision custom;
        const bool hasCustom = id != 0 &&
            ServerBlocks::Instance().Collision(BlockPalette::Instance().Name(id), custom);
        const CollisionSet* chosen = hasCustom && custom.base.defined ? &custom.base : nullptr;
        if (hasCustom)
            for (const auto& [condition, set] : custom.permutations)
                if (EvaluateBlockCondition(condition, id))
                    chosen = &set;
        Info info = chosen ? Info{} : ClassifyVanilla(id, blocks);
        const CollisionSet* selection = hasCustom && custom.selection.defined ? &custom.selection : nullptr;
        if (hasCustom)
            for (const auto& [condition, set] : custom.selectionPermutations)
                if (EvaluateBlockCondition(condition, id)) selection = &set;
        if (selection) {
            info.selectionDefined = true;
            for (const auto& b : selection->boxes)
                info.selection.push_back({b[0], b[1], b[2], b[3], b[4], b[5]});
        }
        const std::string blockName = BlockPalette::Instance().Name(id);
        if (blockName == "minecraft:honey_block" || blockName == "honey_block") {
            info.shape.jumpProperties |= physics::bds::ReducedJumpSurfaceProperty;
            info.shape.honey = true;
        }
        bool replacedGeometry = chosen != nullptr;
        if (chosen)
            for (const CollisionBox& box : chosen->boxes)
                Add(info.shape, box[0], box[1], box[2], box[3], box[4], box[5]);
        if (hasCustom) {
            if (custom.friction >= 0.0f)
                info.shape.friction = custom.friction;
            info.shape.randomOffset = custom.randomOffset;
            for (const auto& [condition, offset] : custom.randomOffsetPermutations)
                if (EvaluateBlockCondition(condition, id))
                    info.shape.randomOffset = offset;
        }
        if (!chosen && id != 0 && info.shape.Empty() && info.shape.fluid == 0 &&
            !info.shape.climbable && !info.shape.web && !info.shape.powderSnow && !info.shape.berryBush && info.shape.scaffoldingStability < 0) {
            std::vector<Box> boxes;
            if (VoxelShapeRegistry::Instance().Find(BlockPalette::Instance().Name(id), boxes) && !boxes.empty()) {
                info.shape.boxes = std::move(boxes);
                replacedGeometry = true;
            }
        }
        if (replacedGeometry)
            info.full = info.shape.boxes.size() == 1 && info.shape.boxes[0].x0 <= 0 &&
                        info.shape.boxes[0].y0 <= 0 && info.shape.boxes[0].z0 <= 0 &&
                        info.shape.boxes[0].x1 >= 1 && info.shape.boxes[0].y1 >= 1 &&
                        info.shape.boxes[0].z1 >= 1;
        return info;
    }

    LevelCollision::Info LevelCollision::ClassifyVanilla(uint16_t id, const BlockLibrary* blocks) {
        Info info;
        BlockShape& s = info.shape;
        if (id == 0)
            return info;
        s.isAir = false;

        std::string n = BlockPalette::Instance().Name(id);
        if (n.empty()) {
            Add(s, 0, 0, 0, 1, 1, 1);
            info.full = true;
            return info;
        }
        if (n.starts_with("minecraft:"))
            n.erase(0, 10);

        if (n == "portal" || n == "end_portal" || n == "end_gateway") {
            s.portal = true;
            return info;
        }
        if (n == "water" || n == "flowing_water" || n == "lava" || n == "flowing_lava") {
            const int depth = State(id, "liquid_depth", 0);
            s.fluid = Has(n, "lava") ? 2 : 1;
            s.fluidLevel = depth >= 8 ? 1.0f : static_cast<float>(9 - depth) / 9.0f;
            s.fluidDepth = static_cast<uint8_t>(depth);
            return info;
        }
        if (n == "kelp" || n == "seagrass" || n == "sea_grass" || n == "bubble_column" || n == "tall_seagrass" ||
            n == "kelp_plant") {
            s.fluid = 1;
            s.fluidLevel = 8.0f / 9.0f;
            if (n == "bubble_column")
                s.bubbleColumn = State(id, "drag_down", 0) ? -1 : 1;
            return info;
        }
        if (n == "web" || n == "cobweb") {
            s.web = true;
            return info;
        }
        if (n == "sweet_berry_bush") {
            s.berryBush = true;
            return info;
        }
        if (n == "powder_snow") {
            s.powderSnow = true;
            return info;
        }
        if (n == "scaffolding") {
            s.scaffoldingStability = static_cast<int8_t>(State(id, "stability", 0));
            return info;
        }
        // the vanilla registry (0x14ce9ad30) sets the climbable and sneak-hold properties (+0x134 |= 6) on these only
        if (n == "vine" || n == "twisting_vines" || n == "weeping_vines" ||
            n == "cave_vines" || n == "cave_vines_body_with_berries" || n == "cave_vines_head_with_berries") {
            s.climbable = true;
            return info;
        }
        // LadderBlock 0x143c836d0: a 3/16 slab against the wall it faces from, none for facing 0/1
        if (n == "ladder") {
            s.climbable = true;
            switch (State(id, "facing_direction", 0)) {
            case 2: Add(s, 0, 0, 0.8125, 1, 1, 1); break;
            case 3: Add(s, 0, 0, 0, 1, 1, 0.1875); break;
            case 4: Add(s, 0.8125, 0, 0, 1, 1, 1); break;
            case 5: Add(s, 0, 0, 0, 0.1875, 1, 1); break;
            default: break;
            }
            return info;
        }
        // collision needs the other half's states (QueryMain); this own-state box only serves ShapeOf
        if (Ends(n, "_door") && !Ends(n, "_trapdoor")) {
            info.kind = Kind::Door;
            AddDoor(s, DoorDirection(id), State(id, "open_bit", 0) != 0, State(id, "door_hinge_bit", 0) != 0);
            return info;
        }
        if (AddComputedShape(n, id, s))
            return info;
        {
            const auto& reg = Registry();
            const auto it = reg.find(BlockPalette::Instance().NetworkId(id));
            if (it != reg.end()) {
                const PhysRecord& r = it->second;
                // EndPortalFrameBlock 0x14a42b620 adds the eye box without the block position: no eye in the world
                const size_t boxCount = n == "end_portal_frame" ? std::min<size_t>(r.boxes.size(), 1) : r.boxes.size();
                for (size_t b = 0; b < boxCount; b++)
                    Add(s, r.boxes[b].x0, r.boxes[b].y0, r.boxes[b].z0, r.boxes[b].x1, r.boxes[b].y1, r.boxes[b].z1);
                s.friction = r.friction;
                s.surface = r.surface;
                s.climbable = (r.flags & 1) != 0;
                // registry misses the vanilla ladder flag on some packs, restore it by name
                if (n == "ladder")
                    s.climbable = true;
                // keep vanilla slip even when the registry ships its own values
                if (n == "ice" || n == "packed_ice" || n == "frosted_ice")
                    s.friction = 0.98f;
                else if (n == "blue_ice")
                    s.friction = 0.989f;
                else if (n == "slime" || n == "slime_block" || n == "honey_block")
                    s.friction = 0.8f;
                s.web = (r.flags & 8) != 0;
                info.full = r.boxes.size() == 1 && r.boxes[0].x0 <= 0 && r.boxes[0].y0 <= 0 && r.boxes[0].z0 <= 0 &&
                            r.boxes[0].x1 >= 1 && r.boxes[0].y1 >= 1 && r.boxes[0].z1 >= 1;
                return info;
            }
        }
        if (Passable(n))
            return info;

        if (Ends(n, "_slab") && !Has(n, "double")) {
            const bool top = StateText(id, "vertical_half", "bottom") == "top" || State(id, "top_slot_bit", 0) != 0;
            top ? Add(s, 0, 0.5, 0, 1, 1, 1) : Add(s, 0, 0, 0, 1, 0.5, 1);
            return info;
        }
        // StairBlock 0x148f9e8c0: half slab 0x148f9dbb0, step 0x148f9dd80, inner-corner extra 0x148f9e370
        if (Ends(n, "_stairs")) {
            const int dir = State(id, "weirdo_direction", 0) & 3;
            const bool flip = State(id, "upside_down_bit", 0) != 0;
            const std::string corner = StateText(id, "corner", "none");
            const double base0 = flip ? 0.5 : 0.0, base1 = flip ? 1.0 : 0.5;
            const double step0 = flip ? 0.0 : 0.5, step1 = flip ? 0.5 : 1.0;
            Add(s, 0, base0, 0, 1, base1, 1);
            // step x0, z0, x1, z1 per direction: straight, outer_left, outer_right
            static constexpr double step[4][3][4] = {
                {{0.5, 0, 1, 1}, {0.5, 0, 1, 0.5}, {0.5, 0.5, 1, 1}},
                {{0, 0, 0.5, 1}, {0, 0.5, 0.5, 1}, {0, 0, 0.5, 0.5}},
                {{0, 0.5, 1, 1}, {0.5, 0.5, 1, 1}, {0, 0.5, 0.5, 1}},
                {{0, 0, 1, 0.5}, {0, 0, 0.5, 0.5}, {0.5, 0, 1, 0.5}}};
            const int shape = corner == "outer_left" ? 1 : corner == "outer_right" ? 2 : 0;
            const double* st = step[dir][shape];
            Add(s, st[0], step0, st[1], st[2], step1, st[3]);
            // inner corners fill one more quarter: x0, z0, x1, z1 for inner_left, inner_right
            static constexpr double inner[4][2][4] = {
                {{0, 0, 0.5, 0.5}, {0, 0.5, 0.5, 1}},
                {{0.5, 0.5, 1, 1}, {0.5, 0, 1, 0.5}},
                {{0.5, 0, 1, 0.5}, {0, 0, 0.5, 0.5}},
                {{0, 0.5, 0.5, 1}, {0.5, 0.5, 1, 1}}};
            if (corner == "inner_left" || corner == "inner_right") {
                const double* in = inner[dir][corner == "inner_right" ? 1 : 0];
                Add(s, in[0], step0, in[1], in[2], step1, in[3]);
            }
            return info;
        }
        if (Ends(n, "_fence_gate") || n == "fence_gate") {
            if (State(id, "open_bit", 0))
                return info;
            // FenceGateBlock 0x14a4371b0: cardinal_direction south/north (0, 2) spans x
            const std::string card = StateText(id, "cardinal_direction", "");
            const bool alongX = card.empty() ? (State(id, "direction", 0) & 1) == 0 : card == "south" || card == "north";
            alongX ? Add(s, 0, 0, 0.375, 1, 1.5, 0.625) : Add(s, 0.375, 0, 0, 0.625, 1.5, 1);
            return info;
        }
        if (Ends(n, "_fence") || n == "fence" || n == "nether_brick_fence") {
            info.kind = Kind::Fence;
            return info;
        }
        if (Ends(n, "_wall") && !Has(n, "_wall_")) {
            info.kind = Kind::Wall;
            return info;
        }
        if (Ends(n, "glass_pane") || n == "iron_bars" || n == "glass_pane" || n == "stained_glass_pane" ||
            n == "copper_bars" || Ends(n, "_bars")) {
            info.kind = Kind::Pane;
            return info;
        }
        if (Ends(n, "_carpet") || n == "carpet" || n == "moss_carpet" || n == "pale_moss_carpet") {
            Add(s, 0, 0, 0, 1, 0.0625, 1);
            return info;
        }
        // TopSnowBlock 0x14b27c5d0 (current worlds): height * 0.125, a single layer has none, a full stack 0.875
        if (n == "snow_layer") {
            const int h = State(id, "height", 0);
            if (h > 0)
                Add(s, 0, 0, 0, 1, h * 0.125, 1);
            return info;
        }
        if (n == "farmland" || n == "dirt_path" || n == "grass_path") {
            Add(s, 0, 0, 0, 1, 0.9375, 1);
            return info;
        }
        // no speed factor: the game slows soul sand through ground acceleration (friction x1.225) only
        if (n == "soul_sand") {
            Add(s, 0, 0, 0, 1, 0.875, 1);
            s.surface = 4;
            return info;
        }
        if (n == "mud" || n == "packed_mud_none") {
            Add(s, 0, 0, 0, 1, 0.875, 1);
            return info;
        }
        if (n == "honey_block") {
            Add(s, 0.0625, 0, 0.0625, 0.9375, 1, 0.9375);
            s.friction = 0.8f;
            s.surface = 3;
            return info;
        }
        if (n == "enchanting_table") { Add(s, 0, 0, 0, 1, 0.75, 1); return info; }
        if (n == "daylight_detector" || n == "daylight_detector_inverted") { Add(s, 0, 0, 0, 1, 0.375, 1); return info; }
        if (Ends(n, "_bed") || n == "bed") { Add(s, 0, 0, 0, 1, 0.5625, 1); s.surface = 2; return info; }
        // CakeBlock 0x1435bde20: each bite moves the west side in by 1/8
        if (n == "cake") {
            const double bitten = (2 * State(id, "bite_counter", 0) + 1) * 0.0625;
            Add(s, std::min(0.9375, bitten), 0, 0.0625, std::max(0.9375, bitten), 0.5, 0.9375);
            return info;
        }
        if (n == "lily_pad" || n == "waterlily") { Add(s, 0.0625, 0, 0.0625, 0.9375, 0.09375, 0.9375); return info; }
        if (n == "end_portal_frame") { Add(s, 0, 0, 0, 1, 0.8125, 1); return info; }
        if (n == "cactus") { Add(s, 0.0625, 0, 0.0625, 0.9375, 1, 0.9375); return info; }
        if (n == "campfire" || n == "soul_campfire") { Add(s, 0, 0, 0, 1, 0.4375, 1); return info; }
        // the legacy "stonecutter" is a full cube
        if (n == "stonecutter_block") { Add(s, 0, 0, 0, 1, 0.5625, 1); return info; }
        if (n == "repeater" || n == "unpowered_repeater" || n == "powered_repeater" || n == "comparator" ||
            n == "unpowered_comparator" || n == "powered_comparator") {
            Add(s, 0, 0, 0, 1, 0.125, 1);
            return info;
        }

        if (blocks) {
            const BlockAppearance& look = blocks->Look(static_cast<int32_t>(id));
            if (look.shape == BlockAppearance::Shape::Cross || look.shape == BlockAppearance::Shape::Wall)
                return info;
        }

        Add(s, 0, 0, 0, 1, 1, 1);
        info.full = true;
        if (n == "ice" || n == "packed_ice" || n == "frosted_ice")
            s.friction = 0.98f;
        else if (n == "blue_ice")
            s.friction = 0.989f;
        else if (n == "slime" || n == "slime_block") {
            s.friction = 0.8f;
            s.surface = 1;
        }
        return info;
    }

    namespace {
        std::mutex gEntityBoxMutex;
        std::shared_ptr<const std::vector<physics::Box>> gEntityBoxes = std::make_shared<const std::vector<physics::Box>>();
    }

    void SetEntityCollisionBoxes(std::vector<physics::Box> boxes) {
        auto fresh = std::make_shared<const std::vector<physics::Box>>(std::move(boxes));
        std::lock_guard lock(gEntityBoxMutex);
        gEntityBoxes = std::move(fresh);
    }

    LevelCollision::LevelCollision(const Level& l, const BlockLibrary* b) : level(l), blocks(b) {
        std::lock_guard lock(gEntityBoxMutex);
        entityBoxes = gEntityBoxes;
    }

    bool LevelCollision::CollisionChunkReady(int x, int z) const {
        return level.HasChunk(x, z);
    }

    physics::bds::TerrainHeightLimits LevelCollision::CollisionHeightLimits() const {
        return {Chunk::MIN_Y, Chunk::MIN_Y + Chunk::HEIGHT};
    }

    physics::BlockShape LevelCollision::Query(int x, int y, int z) const {
        BlockShape s = QueryMain(x, y, z);
        if (entityBoxes && !entityBoxes->empty()) {
            for (const physics::Box& b : *entityBoxes) {
                if (b.x1 <= x || b.x0 >= x + 1 || b.y1 <= y || b.y0 >= y + 1 || b.z1 <= z || b.z0 >= z + 1)
                    continue;
                s.boxes.push_back({std::max<double>(b.x0, x) - x, std::max<double>(b.y0, y) - y, std::max<double>(b.z0, z) - z,
                                   std::min<double>(b.x1, x + 1) - x, std::min<double>(b.y1, y + 1) - y, std::min<double>(b.z1, z + 1) - z});
            }
        }
        if (s.fluid == 0 && y >= Chunk::MIN_Y && y < Chunk::MIN_Y + Chunk::HEIGHT) {
            const uint16_t liquid = level.GetLiquid(x, y, z);
            if (liquid != 0) {
                const Info& li = Lookup(liquid, blocks);
                if (li.shape.fluid != 0) {
                    s.fluid = li.shape.fluid;
                    s.fluidLevel = li.shape.fluidLevel;
                    s.fluidDepth = li.shape.fluidDepth;
                }
            }
        }
        return s;
    }

    physics::BlockShape LevelCollision::QueryMain(int x, int y, int z) const {
        if (y < Chunk::MIN_Y)
            return {};
        if (y >= Chunk::MIN_Y + Chunk::HEIGHT)
            return {};
        bool loaded = false;
        const uint16_t id = level.GetBlock(x, y, z, loaded);
        if (!loaded) {
            BlockShape solid;
            Add(solid, 0, 0, 0, 1, 1, 1);
            return solid;
        }
        const Info& info = Lookup(id, blocks);
        if (info.kind == Kind::Plain)
            return info.shape;

        BlockShape s;
        s.friction = info.shape.friction;
        s.randomOffset = info.shape.randomOffset;

        if (info.kind == Kind::Door) {
            // DoorBlock 0x149c5b200 / 0x149c5af70: direction and open from the lower half, hinge from the upper;
            // both halves must be this door, else direction 0, closed, hinge 0
            const BlockPalette& palette = BlockPalette::Instance();
            const bool upperHalf = State(id, "upper_block_bit", 0) != 0;
            bool otherLoaded = false;
            const uint16_t other = level.GetBlock(x, upperHalf ? y - 1 : y + 1, z, otherLoaded);
            if (otherLoaded && other != 0 && palette.Name(other) == palette.Name(id)) {
                const uint16_t lower = upperHalf ? other : id, upper = upperHalf ? id : other;
                AddDoor(s, DoorDirection(lower), State(lower, "open_bit", 0) != 0, State(upper, "door_hinge_bit", 0) != 0);
            } else {
                AddDoor(s, 0, false, false);
            }
            return s;
        }

        auto joins = [&](int dx, int dz) {
            bool nl = false;
            const uint16_t other = level.GetBlock(x + dx, y, z + dz, nl);
            if (!nl || other == 0)
                return false;
            const Info& n = Lookup(other, blocks);
            if (n.kind == info.kind || n.full)
                return true;
            if (info.kind == Kind::Fence) {
                std::string name = BlockPalette::Instance().Name(other);
                return Ends(name, "_fence_gate") || name == "minecraft:fence_gate";
            }
            return false;
        };

        // the game builds these from the block's own connection states; neighbours only for palettes without them
        bool north, south, west, east, post = true;
        if (info.kind == Kind::Wall && HasAnyState(id, "wall_connection_type_north")) {
            north = StateText(id, "wall_connection_type_north", "none") != "none";
            east = StateText(id, "wall_connection_type_east", "none") != "none";
            south = StateText(id, "wall_connection_type_south", "none") != "none";
            west = StateText(id, "wall_connection_type_west", "none") != "none";
            post = State(id, "wall_post_bit", 0) != 0;
        } else if (info.kind != Kind::Wall && HasAnyState(id, "connection_north")) {
            north = State(id, "connection_north", 0) != 0;
            east = State(id, "connection_east", 0) != 0;
            south = State(id, "connection_south", 0) != 0;
            west = State(id, "connection_west", 0) != 0;
        } else {
            north = joins(0, -1);
            south = joins(0, 1);
            west = joins(-1, 0);
            east = joins(1, 0);
        }
        switch (info.kind) {
        case Kind::Fence:
            // FenceBlock 0x14a433650: north/south bar, then east/west bar, a lone post only without connections
            if (north || south) Add(s, 0.375, 0, north ? 0 : 0.375, 0.625, 1.5, south ? 1 : 0.625);
            if (east || west) Add(s, west ? 0 : 0.375, 0, 0.375, east ? 1 : 0.625, 1.5, 0.625);
            if (!north && !south && !east && !west) Add(s, 0.375, 0, 0.375, 0.625, 1.5, 0.625);
            break;
        case Kind::Wall: {
            // WallBlock 0x144a33db0 / 0x144a33530: one box over every connected side, 1.5 high; no post narrows a
            // straight wall to 0.3125..0.6875
            double x0 = west ? 0 : 0.25, x1 = east ? 1 : 0.75, z0 = north ? 0 : 0.25, z1 = south ? 1 : 0.75;
            if (!post && north && south && !east && !west) {
                x0 = 0.3125;
                x1 = 0.6875;
            } else if (!post && east && west && !north && !south) {
                z0 = 0.3125;
                z1 = 0.6875;
            }
            Add(s, x0, 0, z0, x1, 1.5, z1);
            break;
        }
        default:
            // ThinFenceBlock 0x14b273e00: half arms from the centre, no post once connected
            if (!north && !south && !east && !west) Add(s, 0.4375, 0, 0.4375, 0.5625, 1, 0.5625);
            if (east || west) Add(s, west ? 0 : 0.5, 0, 0.4375, east ? 1 : 0.5, 1, 0.5625);
            if (north || south) Add(s, 0.4375, 0, north ? 0 : 0.5, 0.5625, 1, south ? 1 : 0.5);
            break;
        }
        return s;
    }
}
