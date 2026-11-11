#include "client/entity/BlockEntityView.hpp"

#include "world/level/block/BlockPalette.hpp"

#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <tuple>

namespace perlin::entity {
    namespace {
        float BedYaw(int direction) {
            switch (direction & 3) {
            case 0:
                return 0.0f;
            case 1:
                return 90.0f;
            case 2:
                return 180.0f;
            default:
                return 270.0f;
            }
        }

        void BedStep(int direction, int& dx, int& dz) {
            switch (direction & 3) {
            case 0:
                dx = 0;
                dz = 1;
                break;
            case 1:
                dx = -1;
                dz = 0;
                break;
            case 2:
                dx = 0;
                dz = -1;
                break;
            default:
                dx = 1;
                dz = 0;
                break;
            }
        }

        float FacingYaw(int facing) {
            switch (facing) {
            case 2:
                return 180.0f;
            case 4:
                return 90.0f;
            case 5:
                return 270.0f;
            default:
                return 0.0f;
            }
        }

        const char* BannerPatternName(const std::string& code) {
            static const std::unordered_map<std::string, const char*> kNames = {
                {"bl", "square_bottom_left"},
                {"br", "square_bottom_right"},
                {"tl", "square_top_left"},
                {"tr", "square_top_right"},
                {"bs", "stripe_bottom"},
                {"ts", "stripe_top"},
                {"ls", "stripe_left"},
                {"rs", "stripe_right"},
                {"cs", "stripe_center"},
                {"ms", "stripe_middle"},
                {"drs", "stripe_downright"},
                {"dls", "stripe_downleft"},
                {"ss", "small_stripes"},
                {"cr", "cross"},
                {"sc", "straight_cross"},
                {"bt", "triangle_bottom"},
                {"tt", "triangle_top"},
                {"bts", "triangles_bottom"},
                {"tts", "triangles_top"},
                {"ld", "diagonal_left"},
                {"rd", "diagonal_up_right"},
                {"lud", "diagonal_up_left"},
                {"rud", "diagonal_right"},
                {"mc", "circle"},
                {"mr", "rhombus"},
                {"vh", "half_vertical"},
                {"hh", "half_horizontal"},
                {"vhr", "half_vertical_right"},
                {"hhb", "half_horizontal_bottom"},
                {"bo", "border"},
                {"cbo", "curly_border"},
                {"gra", "gradient"},
                {"gru", "gradient_up"},
                {"bri", "bricks"},
                {"glb", "globe"},
                {"cre", "creeper"},
                {"sku", "skull"},
                {"flo", "flower"},
                {"moj", "mojang"},
                {"pig", "piglin"},
                {"flw", "flow"},
                {"gus", "guster"}};
            const auto it = kNames.find(code);
            return it == kNames.end() ? nullptr : it->second;
        }

        void BannerLayers(const BlockEntity* be, BlockEntityDraw& d) {
            int base = 15;
            std::vector<std::pair<std::string, int>> layers;
            if (be) {
                if (be->base >= 0)
                    base = be->base;
                layers = be->patterns;
                if (be->bannerType == 1 && layers.empty()) {
                    base = 15;
                    layers = {{"mr", 6}, {"bs", 7}, {"cs", 8}, {"bo", 7},
                              {"ms", 0}, {"hh", 7}, {"mc", 7}, {"bo", 0}};
                }
            } else if (const char* forced = std::getenv("PERLIN_PREVIEW_BANNER")) {
                std::string spec = forced;
                size_t at = 0;
                bool first = true;
                while (at <= spec.size()) {
                    const size_t end = std::min(spec.find(';', at), spec.size());
                    const std::string part = spec.substr(at, end - at);
                    if (first)
                        base = std::atoi(part.c_str());
                    else if (const size_t colon = part.find(':'); colon != std::string::npos)
                        layers.emplace_back(part.substr(0, colon), std::atoi(part.c_str() + colon + 1));
                    first = false;
                    at = end + 1;
                }
            }
            d.color = 15 - std::clamp(base, 0, 15);
            for (const auto& [code, colour] : layers)
                if (const char* name = BannerPatternName(code))
                    d.patterns.emplace_back(std::string("textures/entity/banner/banner_") + name,
                                            15 - std::clamp(colour, 0, 15));
        }

        int FacingToGround(int facing) {
            switch (facing) {
            case 2:
                return 8;
            case 4:
                return 4;
            case 5:
                return 12;
            default:
                return 0;
            }
        }
    }

    void BuildBlockEntityDraws(const std::vector<SpecialBlock>& specials, const BlockEntityStore& store,
                               std::vector<BlockEntityDraw>& out) {
        BlockPalette& palette = BlockPalette::Instance();
        std::set<std::tuple<int, int, int>> beds;
        const char* forced = std::getenv("PERLIN_PREVIEW_SIGNTEXT");
        for (const SpecialBlock& s : specials) {
            const std::string name = palette.Name(s.id);
            const std::string shortName = name.starts_with("minecraft:") ? name.substr(10) : name;
            BlockEntity be;
            if (shortName == "bed") {
                const int direction = palette.StateInt(s.id, "direction", 0);
                const bool head = palette.StateInt(s.id, "head_piece_bit", 0) != 0;
                int dx = 0, dz = 0;
                BedStep(direction, dx, dz);
                const int fx = head ? s.x - dx : s.x, fz = head ? s.z - dz : s.z;
                const int ox = head ? fx : fx + dx, oz = head ? fz : fz + dz;
                if (!beds.emplace(fx, s.y, fz).second)
                    continue;
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Bed;
                d.origin = {static_cast<double>(fx), static_cast<double>(s.y), static_cast<double>(fz)};
                d.yaw = BedYaw(direction + 2);
                int color = -1;
                if (store.Get(s.x, s.y, s.z, be))
                    color = be.color;
                if (color < 0 && store.Get(ox, s.y, oz, be))
                    color = be.color;
                d.color = color < 0 ? 14 : color;
                out.push_back(std::move(d));
                continue;
            }
            if (shortName == "chest" || shortName == "trapped_chest" || shortName == "ender_chest" ||
                shortName.ends_with("copper_chest")) {
                std::string dir = palette.StateString(s.id, "minecraft:cardinal_direction", "");
                if (dir.empty()) {
                    static const char* const kFacing[] = {"south", "south", "north", "south", "west", "east"};
                    dir = kFacing[std::clamp(palette.StateInt(s.id, "facing_direction", 3), 0, 5)];
                }
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Skull;
                d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
                d.yaw = dir == "north" ? 180.0f : dir == "west" ? 90.0f : dir == "east" ? 270.0f : 0.0f;
                const int rx = dir == "south"   ? 1
                               : dir == "north" ? -1
                                                : 0,
                          rz = dir == "west"   ? 1
                               : dir == "east" ? -1
                                               : 0;
                const bool gallery = std::getenv("PERLIN_PREVIEW_GALLERY") != nullptr;
                const auto pairedWith = [&](int ox, int oz) {
                    BlockEntity other;
                    if (store.Get(ox, s.y, oz, other) && other.paired)
                        return other.pairX == s.x && other.pairZ == s.z;
                    if (!gallery)
                        return false;
                    for (const SpecialBlock& o : specials)
                        if (o.x == ox && o.y == s.y && o.z == oz && o.id == s.id)
                            return true;
                    return false;
                };
                int side = 0;
                if (store.Get(s.x, s.y, s.z, be) && be.paired) {
                    side = be.pairX == s.x + rx && be.pairZ == s.z + rz   ? 1
                           : be.pairX == s.x - rx && be.pairZ == s.z - rz ? -1
                                                                          : 0;
                } else if (pairedWith(s.x - rx, s.z - rz)) {
                    side = -1;
                } else if (pairedWith(s.x + rx, s.z + rz)) {
                    side = 1;
                }
                if (side < 0)
                    continue;
                bool large = side > 0;
                const int px = s.x + rx, pz = s.z + rz;
                const bool ender = shortName == "ender_chest", trapped = shortName == "trapped_chest";
                const int copper = shortName.contains("oxidized")        ? 4
                                   : shortName.contains("weathered")     ? 3
                                   : shortName.contains("exposed")       ? 2
                                   : shortName.ends_with("copper_chest") ? 1
                                                                         : 0;
                if (ender)
                    large = false;
                if (large)
                    d.skull = 21 + (copper ? 1 + copper : trapped ? 1 : 0);
                else
                    d.skull = 14 + (copper ? 2 + copper : ender ? 2 : trapped ? 1 : 0);
                d.lid = store.LidOpenness(s.x, s.y, s.z);
                if (large)
                    d.lid = std::max(d.lid, store.LidOpenness(px, s.y, pz));
                if (const char* forcedLid = std::getenv("PERLIN_PREVIEW_LID"))
                    d.lid = static_cast<float>(std::atof(forcedLid));
                out.push_back(std::move(d));
                continue;
            }
            static const char* const kSkulls[] = {"skeleton_skull", "wither_skeleton_skull", "zombie_head",
                                                  "player_head",    "creeper_head",          "dragon_head",
                                                  "piglin_head"};
            int skull = -1;
            for (int k = 0; k < 7; k++)
                if (shortName == kSkulls[k])
                    skull = k;
            if (shortName == "standing_banner" || shortName == "wall_banner") {
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Skull;
                d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
                if (shortName == "wall_banner") {
                    d.skull = 13;
                    d.yaw = FacingYaw(palette.StateInt(s.id, "facing_direction", 3));
                } else {
                    d.skull = 12;
                    d.yaw =
                        static_cast<float>(palette.StateInt(s.id, "ground_sign_direction", 0) % 16) * 22.5f;
                }
                const bool has = store.Get(s.x, s.y, s.z, be);
                BannerLayers(has ? &be : nullptr, d);
                out.push_back(std::move(d));
                continue;
            }
            if (shortName == "conduit") {
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Skull;
                d.skull = 11;
                d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
                out.push_back(std::move(d));
                continue;
            }
            if (shortName.contains("copper_golem_statue")) {
                const int level = shortName.contains("oxidized")    ? 3
                                  : shortName.contains("weathered") ? 2
                                  : shortName.contains("exposed")   ? 1
                                                                    : 0;
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Skull;
                d.skull = 7 + level;
                d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
                const std::string dir = palette.StateString(s.id, "minecraft:cardinal_direction", "south");
                d.yaw = dir == "west" ? 90.0f : dir == "north" ? 180.0f : dir == "east" ? 270.0f : 0.0f;
                out.push_back(std::move(d));
                continue;
            }
            if (skull >= 0) {
                BlockEntityDraw d;
                d.kind = BlockEntityDraw::Kind::Skull;
                d.skull = skull;
                d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
                const int facing = palette.StateInt(s.id, "facing_direction", 1);
                if (facing >= 2 && facing <= 5) {
                    d.onWall = true;
                    d.yaw = FacingYaw(facing);
                } else {
                    d.yaw = store.Get(s.x, s.y, s.z, be) ? be.rotation : 0.0f;
                }
                out.push_back(std::move(d));
                continue;
            }
            BlockEntityDraw d;
            d.kind = BlockEntityDraw::Kind::Sign;
            d.origin = {static_cast<double>(s.x), static_cast<double>(s.y), static_cast<double>(s.z)};
            const bool wall = shortName.contains("wall_sign");
            const bool hangingSign = shortName.contains("hanging_sign");
            if (!store.Get(s.x, s.y, s.z, be) && !forced)
                continue;
            if (forced && be.front.text.empty())
                be.front.text = forced;
            if (be.front.text.empty() && be.back.text.empty())
                continue;
            d.front = be.front.text;
            d.back = be.back.text;
            d.frontColor = be.front.color;
            d.backColor = be.back.color;
            d.frontGlow = be.front.glow;
            d.backGlow = be.back.glow;
            const int facing = palette.StateInt(s.id, "facing_direction", 2);
            if (wall) {
                d.wall = true;
                d.yaw = FacingYaw(facing);
            } else if (hangingSign) {
                d.hanging = true;
                const bool hangs = palette.StateInt(s.id, "hanging", 0) != 0;
                if (hangs) {
                    const bool attached = palette.StateInt(s.id, "attached_bit", 0) != 0;
                    const int k = attached ? palette.StateInt(s.id, "ground_sign_direction", 0) % 16
                                           : FacingToGround(facing);
                    d.yaw = static_cast<float>(k) * 22.5f;
                } else
                    d.yaw = FacingYaw(facing);
            } else {
                d.yaw = static_cast<float>(palette.StateInt(s.id, "ground_sign_direction", 0) % 16) * 22.5f;
            }
            out.push_back(std::move(d));
        }
    }
}
