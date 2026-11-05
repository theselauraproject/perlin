#include "core/Log.hpp"
#include "core/Server.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/Mobs.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace perlin::server {
    namespace {
        constexpr int kFaceOffset[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        constexpr double kEye = 1.62;

        std::string NameOf(uint16_t block) {
            const std::string n = BlockRegistry::Instance().Name(block);
            return n.empty() ? "minecraft:air" : n;
        }

        bool Hollow(const std::string& n) {
            return n == "minecraft:air" || n == "minecraft:fire" || n == "minecraft:soul_fire" || n == "minecraft:portal";
        }
    }

    bool Server::FindPortalFrame(int dim, int x, int y, int z, int axis, PortalFrame& out) {
        const int dx = axis == 0 ? 1 : 0, dz = axis == 0 ? 0 : 1;
        const auto at = [&](int bx, int by, int bz) { return NameOf(BlockAt(dim, bx, by, bz)); };
        if (!Hollow(at(x, y, z)))
            return false;
        const DimensionRange range = RangeOf(dim);
        int by = y;
        for (int i = 0; i < 21 && by > range.minY && Hollow(at(x, by - 1, z)); i++)
            by--;
        if (at(x, by - 1, z) != "minecraft:obsidian")
            return false;
        int left = 0;
        for (; left <= 21; left++) {
            const int px = x - dx * (left + 1), pz = z - dz * (left + 1);
            const std::string n = at(px, by, pz);
            if (n == "minecraft:obsidian")
                break;
            if (!Hollow(n) || at(px, by - 1, pz) != "minecraft:obsidian")
                return false;
        }
        if (left > 21)
            return false;
        const int sx = x - dx * left, sz = z - dz * left;
        int width = 0;
        for (; width <= 21; width++) {
            const int px = sx + dx * width, pz = sz + dz * width;
            const std::string n = at(px, by, pz);
            if (n == "minecraft:obsidian")
                break;
            if (!Hollow(n) || at(px, by - 1, pz) != "minecraft:obsidian")
                return false;
        }
        if (width < 2 || width > 21)
            return false;
        int height = 0;
        for (; height <= 21; height++) {
            const int ry = by + height;
            bool top = true;
            for (int w = 0; w < width; w++)
                if (at(sx + dx * w, ry, sz + dz * w) != "minecraft:obsidian")
                    top = false;
            if (top)
                break;
            if (at(sx - dx, ry, sz - dz) != "minecraft:obsidian" || at(sx + dx * width, ry, sz + dz * width) != "minecraft:obsidian")
                return false;
            for (int w = 0; w < width; w++)
                if (!Hollow(at(sx + dx * w, ry, sz + dz * w)))
                    return false;
        }
        if (height < 3 || height > 21)
            return false;
        out = {dim, sx, by, sz, axis, width, height};
        return true;
    }

    bool Server::TryLightPortal(int dimension, int x, int y, int z) {
        if (dimension == kEnd)
            return false;
        for (int axis = 0; axis < 2; axis++) {
            PortalFrame f;
            if (!FindPortalFrame(dimension, x, y, z, axis, f))
                continue;
            FillPortal(f);
            return true;
        }
        return false;
    }

    void Server::FillPortal(const PortalFrame& f) {
        const uint16_t portal = BlockCatalog::Instance().Resolve(std::string("minecraft:portal[portal_axis=") + (f.axis == 0 ? "x" : "z") + "]");
        const int dx = f.axis == 0 ? 1 : 0, dz = f.axis == 0 ? 0 : 1;
        for (int h = 0; h < f.height; h++)
            for (int w = 0; w < f.width; w++)
                ApplyBlock(f.dimension, f.x + dx * w, f.y + h, f.z + dz * w, portal);
        std::erase_if(portals, [&](const PortalFrame& p) { return p.dimension == f.dimension && p.x == f.x && p.y == f.y && p.z == f.z; });
        portals.push_back(f);
    }

    bool Server::UseItemOn(Session& by, int dimension, int x, int y, int z, int face, const CatalogItem& item) {
        if (face < 0 || face > 5)
            return false;
        if ((item.name == "minecraft:flint_and_steel" || item.name == "minecraft:fire_charge") && dimension == kOverworld &&
            NameOf(BlockAt(dimension, x, y, z)) == "minecraft:tnt") {
            ApplyBlock(dimension, x, y, z, BlockRegistry::kAir);
            Mobs().SpawnPrimedTnt(x + 0.5, y, z + 0.5, 80);
            if (!by.Creative() && item.name == "minecraft:fire_charge") {
                by.Inventory().TakeHeld(1);
                by.SendSlot(by.Inventory().selected);
            }
            return true;
        }
        if (item.name == "minecraft:flint_and_steel" || item.name == "minecraft:fire_charge") {
            const int tx = x + kFaceOffset[face][0], ty = y + kFaceOffset[face][1], tz = z + kFaceOffset[face][2];
            if (!TryLightPortal(dimension, tx, ty, tz))
                return false;
            if (!by.Creative() && item.name == "minecraft:fire_charge") {
                by.Inventory().TakeHeld(1);
                by.SendSlot(by.Inventory().selected);
            }
            return true;
        }
        if (item.name == "minecraft:ender_eye") {
            const uint16_t current = BlockAt(dimension, x, y, z);
            if (NameOf(current) != "minecraft:end_portal_frame")
                return false;
            std::string state = BlockCatalog::Instance().StateText(current);
            if (state.contains("end_portal_eye_bit=true") || state.contains("end_portal_eye_bit=1"))
                return false;
            const size_t bit = state.find("end_portal_eye_bit=");
            if (bit != std::string::npos) {
                const size_t end = state.find_first_of(",]", bit);
                state = state.substr(0, bit) + "end_portal_eye_bit=true" + state.substr(end);
            } else {
                state = "minecraft:end_portal_frame[end_portal_eye_bit=true]";
            }
            ApplyBlock(dimension, x, y, z, BlockCatalog::Instance().Resolve(state));
            if (!by.Creative()) {
                by.Inventory().TakeHeld(1);
                by.SendSlot(by.Inventory().selected);
            }
            const auto filled = [&](int bx, int bz) {
                const uint16_t b = BlockAt(dimension, bx, y, bz);
                if (NameOf(b) != "minecraft:end_portal_frame")
                    return false;
                const std::string s = BlockCatalog::Instance().StateText(b);
                return s.contains("end_portal_eye_bit=true") || s.contains("end_portal_eye_bit=1");
            };
            for (int cx = x - 2; cx <= x + 2; cx++)
                for (int cz = z - 2; cz <= z + 2; cz++) {
                    bool ring = true;
                    for (int k = -1; k <= 1 && ring; k++)
                        ring = filled(cx + k, cz - 2) && filled(cx + k, cz + 2) && filled(cx - 2, cz + k) && filled(cx + 2, cz + k);
                    if (!ring)
                        continue;
                    const uint16_t portal = BlockCatalog::Instance().DefaultBlock("end_portal");
                    for (int px = -1; px <= 1; px++)
                        for (int pz = -1; pz <= 1; pz++)
                            ApplyBlock(dimension, cx + px, y, cz + pz, portal);
                    for (Session* s : PlayersIn(dimension))
                        s->SendText(0, "", "§5The End portal opens");
                    return true;
                }
            return true;
        }
        return false;
    }

    void Server::OnBlockRemoved(Session& by, int dimension, int x, int y, int z, const std::string& name) {
        (void)by;
        if (name != "minecraft:obsidian" && name != "minecraft:portal")
            return;
        std::vector<std::array<int, 3>> stack;
        for (const auto& o : kFaceOffset)
            stack.push_back({x + o[0], y + o[1], z + o[2]});
        int removed = 0;
        while (!stack.empty() && removed < 21 * 21 * 2) {
            const auto p = stack.back();
            stack.pop_back();
            if (NameOf(BlockAt(dimension, p[0], p[1], p[2])) != "minecraft:portal")
                continue;
            ApplyBlock(dimension, p[0], p[1], p[2], BlockRegistry::kAir);
            removed++;
            for (const auto& o : kFaceOffset)
                stack.push_back({p[0] + o[0], p[1] + o[1], p[2] + o[2]});
        }
        if (removed > 0)
            std::erase_if(portals, [&](const PortalFrame& f) {
                return f.dimension == dimension && std::abs(f.x - x) <= 22 && std::abs(f.z - z) <= 22 &&
                       NameOf(BlockAt(dimension, f.x, f.y, f.z)) != "minecraft:portal";
            });
    }

    void Server::ChangeDimension(Session& who, int dimension, double x, double y, double z) {
        if (dimension < 0 || dimension >= kDimensionCount)
            return;
        if (dimension == who.DimensionId()) {
            who.Teleport(x, y, z);
            return;
        }
        const int from = who.DimensionId();
        CloseContainer(who);
        for (Session* other : PlayersIn(from))
            if (other != &who) {
                other->HidePlayer(who);
                who.HidePlayer(*other);
            }
        for (const ItemEntity& item : itemEntities)
            if (item.dimension == from)
                who.HideItem(item.id);
        if (from == kOverworld)
            mobWorld->Hide(who);
        World(dimension);
        who.SwitchDimension(dimension, x, y, z);
        for (Session* other : PlayersIn(dimension))
            if (other != &who) {
                other->ShowPlayer(who);
                who.ShowPlayer(*other);
                other->ShowArmor(who);
                who.ShowArmor(*other);
            }
        for (const ItemEntity& item : itemEntities)
            if (item.dimension == dimension)
                who.ShowItem(item);
        if (dimension == kOverworld)
            mobWorld->Show(who);
        SendWorldState(who);
        static const char* kNames[] = {"overworld", "the_nether", "the_end"};
        LogInfo(who.Name() + " went to the " + kNames[dimension]);
    }

    void Server::BuildEndPlatform() {
        ChunkManager& end = World(kEnd);
        for (int cx = 5; cx <= 7; cx++)
            for (int cz = -1; cz <= 0; cz++)
                end.Load({cx, cz});
        const uint16_t obsidian = BlockCatalog::Instance().DefaultBlock("obsidian");
        for (int x = 98; x <= 102; x++)
            for (int z = -2; z <= 2; z++) {
                ApplyBlock(kEnd, x, 48, z, obsidian);
                for (int y = 49; y <= 51; y++)
                    ApplyBlock(kEnd, x, y, z, BlockRegistry::kAir);
            }
    }

    bool Server::PlacePortalNear(int dimension, int x, int y, int z, PortalFrame& out) {
        const DimensionRange range = RangeOf(dimension);
        const int top = dimension == kNether ? 122 : range.MaxY() - 6;
        const int bottom = dimension == kNether ? 1 : range.minY + 1;
        ChunkManager& world = World(dimension);
        for (int cx = (x - 16) >> 4; cx <= (x + 16) >> 4; cx++)
            for (int cz = (z - 16) >> 4; cz <= (z + 16) >> 4; cz++)
                world.Load({cx, cz});
        const auto at = [&](int bx, int by, int bz) { return NameOf(BlockAt(dimension, bx, by, bz)); };
        const auto solid = [&](const std::string& n) {
            return n != "minecraft:air" && n != "minecraft:lava" && n != "minecraft:water" && n != "minecraft:flowing_lava" &&
                   n != "minecraft:flowing_water" && n != "minecraft:fire";
        };
        double best = 1e300;
        int bx = 0, by = 0, bz = 0;
        bool found = false;
        for (int r = 0; r <= 16 && !found; r++)
            for (int px = x - r; px <= x + r; px++)
                for (int pz = z - r; pz <= z + r; pz++) {
                    if (std::max(std::abs(px - x), std::abs(pz - z)) != r)
                        continue;
                    for (int py = top; py >= bottom; py--) {
                        if (!(solid(at(px, py - 1, pz)) && solid(at(px + 1, py - 1, pz)) && solid(at(px + 2, py - 1, pz)) &&
                              solid(at(px - 1, py - 1, pz))))
                            continue;
                        bool clear = true;
                        for (int w = -1; w <= 2 && clear; w++)
                            for (int h = 0; h <= 4 && clear; h++)
                                clear = at(px + w, py + h, pz) == "minecraft:air";
                        if (!clear)
                            continue;
                        const double d = std::hypot(px - x, pz - z) + std::abs(py - y) * 0.25;
                        if (d < best) {
                            best = d;
                            bx = px;
                            by = py;
                            bz = pz;
                            found = true;
                        }
                        break;
                    }
                }
        const uint16_t obsidian = BlockCatalog::Instance().DefaultBlock("obsidian");
        if (!found) {
            bx = x;
            bz = z;
            by = std::clamp(y, std::max(bottom + 1, dimension == kNether ? 32 : 70), top - 2);
            for (int w = -1; w <= 2; w++)
                for (int d = -1; d <= 1; d++) {
                    ApplyBlock(dimension, bx + w, by - 1, bz + d, obsidian);
                    for (int h = 0; h <= 3; h++)
                        ApplyBlock(dimension, bx + w, by + h, bz + d, BlockRegistry::kAir);
                }
        }
        for (int w = -1; w <= 2; w++) {
            ApplyBlock(dimension, bx + w, by - 1, bz, obsidian);
            ApplyBlock(dimension, bx + w, by + 3, bz, obsidian);
        }
        for (int h = 0; h <= 2; h++) {
            ApplyBlock(dimension, bx - 1, by + h, bz, obsidian);
            ApplyBlock(dimension, bx + 2, by + h, bz, obsidian);
        }
        out = {dimension, bx, by, bz, 0, 2, 3};
        FillPortal(out);
        return true;
    }

    void Server::TravelThroughPortal(Session& who, bool endPortal) {
        const int from = who.DimensionId();
        if (endPortal) {
            if (from == kEnd) {
                ChangeDimension(who, kOverworld, SpawnX() + 0.5, SpawnY() + kEye, SpawnZ() + 0.5);
                return;
            }
            BuildEndPlatform();
            ChangeDimension(who, kEnd, 100.5, 49.0 + kEye, 0.5);
            return;
        }
        const int to = from == kNether ? kOverworld : kNether;
        const double scale = to == kNether ? 1.0 / 8.0 : 8.0;
        const int tx = static_cast<int>(std::floor(who.X() * scale)), tz = static_cast<int>(std::floor(who.Z() * scale));
        const int ty = static_cast<int>(std::floor(who.Y() - kEye));
        const int reach = to == kNether ? 16 : 128;
        const PortalFrame* best = nullptr;
        double bestDistance = 1e300;
        for (const PortalFrame& p : portals) {
            if (p.dimension != to || std::abs(p.x - tx) > reach || std::abs(p.z - tz) > reach)
                continue;
            const double d = std::hypot(p.x - tx, p.z - tz) + std::abs(p.y - ty) * 0.01;
            if (d < bestDistance) {
                bestDistance = d;
                best = &p;
            }
        }
        PortalFrame target;
        if (best) {
            target = *best;
            World(to).Load({target.x >> 4, target.z >> 4});
            if (NameOf(BlockAt(to, target.x, target.y, target.z)) != "minecraft:portal")
                best = nullptr;
        }
        if (!best)
            PlacePortalNear(to, tx, std::clamp(ty, RangeOf(to).minY + 2, RangeOf(to).MaxY() - 8), tz, target);
        const double ox = target.axis == 0 ? target.x + target.width / 2.0 : target.x + 0.5;
        const double oz = target.axis == 0 ? target.z + 0.5 : target.z + target.width / 2.0;
        who.portalCooldown = 1;
        ChangeDimension(who, to, ox, target.y + kEye, oz);
    }

    void Server::TickPortals() {
        for (Session* s : InGamePlayers()) {
            if (s->Dead() || s->Spectator())
                continue;
            const int dim = s->DimensionId();
            const int bx = static_cast<int>(std::floor(s->X())), bz = static_cast<int>(std::floor(s->Z()));
            const int feet = static_cast<int>(std::floor(s->Y() - kEye));
            ChunkManager* world = LoadedWorld(dim);
            if (!world || !world->Peek({bx >> 4, bz >> 4}))
                continue;
            const std::string lower = NameOf(world->GetBlock(bx, feet, bz));
            const std::string upper = NameOf(world->GetBlock(bx, feet + 1, bz));
            const bool nether = lower == "minecraft:portal" || upper == "minecraft:portal";
            const bool end = lower == "minecraft:end_portal" || upper == "minecraft:end_portal";
            if (!nether && !end) {
                s->portalTicks = 0;
                s->portalCooldown = 0;
                continue;
            }
            if (s->portalCooldown > 0)
                continue;
            if (end) {
                s->portalCooldown = 1;
                TravelThroughPortal(*s, true);
                continue;
            }
            if (dim == kEnd)
                continue;
            const int needed = s->Creative() ? 1 : RuleValue("playersnetherportaldefaultdelay") > 0
                                                       ? RuleValue("playersnetherportaldefaultdelay")
                                                       : 80;
            if (++s->portalTicks >= needed) {
                s->portalTicks = 0;
                TravelThroughPortal(*s, false);
            }
        }
    }
}
