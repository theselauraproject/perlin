#include "world/Mobs.hpp"

#include "core/Server.hpp"
#include "session/Session.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <algorithm>
#include <vector>
#include <random>
#include <unordered_set>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace perlin::server {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        struct Shape {
            bool solid = false;
            bool liquid = false;
        };

        Shape ShapeOf(uint16_t id) {
            static std::mutex mutex;
            static std::vector<int8_t> cache;
            {
                std::lock_guard lock(mutex);
                if (id < cache.size() && cache[id])
                    return {cache[id] == 2, cache[id] == 3};
            }
            const std::string n = BlockRegistry::Instance().Name(id);
            int8_t kind = 1;
            if (id == 0 || n.ends_with(":air"))
                kind = 1;
            else if (n.contains("water") || n.contains("lava"))
                kind = 3;
            else {
                static const char* const passable[] = {"grass", "fern", "flower", "sapling", "bush", "vine", "torch", "rail",
                                                       "button", "pressure_plate", "lever", "sign", "banner", "carpet",
                                                       "snow_layer", "seagrass", "kelp", "reeds", "deadbush", "mushroom",
                                                       "petals", "wildflowers", "leaf_litter", "tulip", "poppy", "dandelion",
                                                       "orchid", "allium", "bluet", "daisy", "lilac", "peony", "rose",
                                                       "sunflower", "lichen", "roots", "dripleaf", "wheat", "carrots",
                                                       "potatoes", "beetroot", "cocoa", "web", "portal", "fire", "light_block",
                                                       "redstone_wire", "tripwire", "lily", "cornflower", "eyeblossom", "pitcher",
                                                       "torchflower", "spore_blossom", "hanging", "structure_void"};
                bool pass = false;
                for (const char* p : passable)
                    if (n.contains(p) && !n.contains("block") && !n.contains("stem")) {
                        pass = true;
                        break;
                    }
                kind = pass ? 1 : 2;
            }
            std::lock_guard lock(mutex);
            if (cache.size() <= id)
                cache.resize(static_cast<size_t>(id) + 1, 0);
            cache[id] = kind;
            return {kind == 2, kind == 3};
        }

        uint8_t Angle(float degrees) {
            return static_cast<uint8_t>(static_cast<int>(std::floor(degrees / 360.0f * 256.0f)) & 255);
        }

        const std::unordered_map<std::string, std::string>& ItemAliases() {
            static const std::unordered_map<std::string, std::string> a = {
                {"muttonRaw", "mutton"}, {"dye", "ink_sac"}, {"skull", "wither_skeleton_skull"},
                {"fish", "cod"}, {"rabbit_hide", "rabbit_hide"}, {"appleEnchanted", "enchanted_golden_apple"}};
            return a;
        }
    }

    bool BlockSolid(uint16_t id) { return ShapeOf(id).solid; }
    bool BlockLiquid(uint16_t id) { return ShapeOf(id).liquid; }

    const MobDef* FindMob(const char* identifier) {
        std::string want = identifier;
        if (!want.contains(':'))
            want = "minecraft:" + want;
        if (want == "minecraft:villager")
            want = "minecraft:villager_v2";
        if (want == "minecraft:zombie_villager")
            want = "minecraft:zombie_villager_v2";
        if (want == "minecraft:zombified_piglin")
            want = "minecraft:zombie_pigman";
        if (want == "minecraft:evoker")
            want = "minecraft:evocation_illager";
        if (want == "minecraft:tropical_fish")
            want = "minecraft:tropicalfish";
        if (want == "minecraft:cod")
            want = "minecraft:cod";
        for (int i = 0; i < kMobDefCount; i++)
            if (want == kMobDefs[i].id)
                return &kMobDefs[i];
        return nullptr;
    }

    MobWorld::MobWorld(Server& s) : server(s) {}

    bool MobWorld::Solid(double x, double y, double z) const {
        const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)), bz = static_cast<int>(std::floor(z));
        if (by < kMinY)
            return true;
        if (!server.Chunks().Peek({bx >> 4, bz >> 4}))
            return true;
        return BlockSolid(server.Chunks().GetBlock(bx, by, bz));
    }

    bool MobWorld::Collides(const Mob& m, double x, double y, double z) const {
        const double hw = m.def->width * (m.baby ? 0.25 : 0.5) - 0.001;
        const double h = m.def->height * (m.baby ? 0.5 : 1.0) - 0.001;
        for (int bx = static_cast<int>(std::floor(x - hw)); bx <= static_cast<int>(std::floor(x + hw)); bx++)
            for (int bz = static_cast<int>(std::floor(z - hw)); bz <= static_cast<int>(std::floor(z + hw)); bz++)
                for (int by = static_cast<int>(std::floor(y)); by <= static_cast<int>(std::floor(y + h)); by++)
                    if (Solid(bx + 0.5, by + 0.5, bz + 0.5))
                        return true;
        return false;
    }

    int MobWorld::SurfaceY(int x, int z) const {
        if (!server.Chunks().Peek({x >> 4, z >> 4}))
            return kMinY - 1;
        for (int y = kMinY + kHeight - 1; y > kMinY; y--) {
            const uint16_t b = server.Chunks().GetBlock(x, y, z);
            if (BlockSolid(b) || BlockLiquid(b))
                return y + 1;
        }
        return kMinY - 1;
    }

    bool MobWorld::Day() const {
        const int64_t t = server.DayTime() % 24000;
        return t < 12500 || t > 23500;
    }

    bool MobWorld::Dark(int x, int y, int z) const {
        const bool sky = SurfaceY(x, z) <= y;
        if (sky)
            return !Day() || server.Thundering();
        return y < SurfaceY(x, z) - 4;
    }

    uint64_t MobWorld::Flags(const Mob& m) {
        namespace F = proto::FlagsMetadataFlags1;
        uint64_t f = F::HasCollision | F::Breathing | F::CanClimb | F::Walker;
        if (!(m.def->flags & mobflag::Fly) && !m.projectile && !((m.def->flags & mobflag::Aquatic) && m.inWater))
            f |= F::AffectedByGravity;
        if (m.burning)
            f |= F::Onfire;
        if (m.baby)
            f |= F::Baby;
        if (m.angry || m.target)
            f |= F::Angry;
        if (m.fuse >= 0)
            f |= F::Ignited;
        return f;
    }

    Mob* MobWorld::Spawn(const std::string& identifier, double x, double y, double z, bool persistent) {
        Mob m;
        if (identifier == "minecraft:arrow" || identifier == "arrow") {
            static const MobDef arrow{"minecraft:arrow", 0.25f, 0.25f, 1, 0, 2, 0, "", "", ""};
            m.def = &arrow;
            m.projectile = true;
        } else {
            m.def = FindMob(identifier.c_str());
        }
        if (!m.def)
            return nullptr;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.health = m.def->health;
        m.persistent = persistent;
        m.yaw = m.headYaw = static_cast<float>(rng() % 360);
        mobs.push_back(m);
        Mob& added = mobs.back();
        for (Session* s : server.PlayersIn(kOverworld))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    Mob* MobWorld::Find(uint64_t id) {
        for (Mob& m : mobs)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    bool MobWorld::Kill(uint64_t id) {
        Mob* m = Find(id);
        if (!m || m->deathTime > 0)
            return false;
        m->health = 0;
        Die(*m, nullptr);
        return true;
    }

    int MobWorld::KillAll(const std::string& identifier) {
        int n = 0;
        for (Mob& m : mobs)
            if (m.deathTime == 0 && !m.projectile && m.kind == 0 && (identifier.empty() || identifier == m.def->id ||
                                                      "minecraft:" + identifier == m.def->id)) {
                m.health = 0;
                Die(m, nullptr);
                n++;
            }
        return n;
    }

    void MobWorld::Show(Session& to) const {
        for (const Mob& m : mobs)
            if (m.deathTime == 0)
                to.ShowMob(m);
    }

    void MobWorld::Hide(Session& to) const {
        for (const Mob& m : mobs)
            to.HideItem(m.id);
    }

    Session* MobWorld::Nearest(const Mob& m, double range, bool survivalOnly) {
        Session* best = nullptr;
        double bestD = range * range;
        for (Session* s : server.PlayersIn(kOverworld)) {
            if (survivalOnly && (s->GameMode() == 1 || s->Spectator() || s->Dead()))
                continue;
            const double dx = s->X() - m.x, dy = (s->Y() - 1.62) - m.y, dz = s->Z() - m.z;
            const double d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best = s;
            }
        }
        return best;
    }

    void MobWorld::Hurt(Mob& m, float damage, double fromX, double fromZ, Session* by) {
        if (m.deathTime > 0 || m.hurtTime > 0)
            return;
        m.health -= damage;
        m.hurtTime = 10;
        const double dx = m.x - fromX, dz = m.z - fromZ;
        const double len = std::max(0.01, std::sqrt(dx * dx + dz * dz));
        m.vx = dx / len * 0.4;
        m.vz = dz / len * 0.4;
        m.vy = m.onGround ? 0.36 : m.vy;
        if (m.def->flags & mobflag::Panic)
            m.panicTicks = 60 + static_cast<int>(rng() % 40);
        if (by && (m.def->flags & (mobflag::Neutral | mobflag::Hostile))) {
            m.target = by->Id();
            m.angry = true;
        }
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->MobEvent(m.id, 2, 0);
            s->MobMotion(m);
        }
        if (m.health <= 0)
            Die(m, by);
    }

    bool MobWorld::Attack(Session& by, uint64_t entity, float damage) {
        Mob* m = Find(entity);
        if (!m || m->projectile)
            return false;
        const double dx = m->x - by.X(), dz = m->z - by.Z(), dy = m->y - (by.Y() - 1.62);
        if (dx * dx + dy * dy + dz * dz > 6.0 * 6.0)
            return false;
        Hurt(*m, by.GameMode() == 1 ? 1000.0f : damage, by.X(), by.Z(), &by);
        return true;
    }

    bool MobWorld::Interact(Session& by, uint64_t entity) {
        Mob* m = Find(entity);
        if (!m)
            return false;
        (void)by;
        return true;
    }

    void MobWorld::Die(Mob& m, Session* killer) {
        m.deathTime = 1;
        m.health = 0;
        m.target = 0;
        for (Session* s : server.PlayersIn(kOverworld))
            s->MobEvent(m.id, 3, 0);
        if (m.projectile || m.baby)
            return;
        std::istringstream drops(m.def->drops);
        std::string entry;
        while (std::getline(drops, entry, ';')) {
            std::istringstream parts(entry);
            std::string name, lo, hi, chance;
            std::getline(parts, name, ':');
            std::getline(parts, lo, ':');
            std::getline(parts, hi, ':');
            std::getline(parts, chance, ':');
            if (const auto it = ItemAliases().find(name); it != ItemAliases().end())
                name = it->second;
            if (m.burning && name == "beef")
                name = "cooked_beef";
            if (m.burning && name == "porkchop")
                name = "cooked_porkchop";
            if (m.burning && name == "chicken")
                name = "cooked_chicken";
            if (m.burning && name == "mutton")
                name = "cooked_mutton";
            const double c = chance.empty() ? 1.0 : std::atof(chance.c_str());
            if (std::uniform_real_distribution<double>(0, 1)(rng) >= c)
                continue;
            const int a = std::atoi(lo.c_str()), b = std::max(a, std::atoi(hi.c_str()));
            const int count = a + static_cast<int>(rng() % static_cast<unsigned>(b - a + 1));
            if (count <= 0)
                continue;
            const CatalogItem* item = BlockCatalog::Instance().ItemByName("minecraft:" + name);
            if (!item)
                continue;
            ItemStack stack;
            stack.networkId = item->networkId;
            stack.count = static_cast<uint16_t>(count);
            server.SpawnItem(stack, m.x, m.y + 0.5, m.z, (rng() % 100 - 50) / 500.0, 0.2, (rng() % 100 - 50) / 500.0, 10);
        }
        (void)killer;
    }

    bool FallsWithGravity(uint16_t id) {
        if (id == 0)
            return false;
        static std::vector<int8_t> memo;
        static std::mutex memoMutex;
        std::lock_guard lock(memoMutex);
        if (id >= memo.size())
            memo.resize(static_cast<size_t>(id) + 1, -1);
        int8_t& v = memo[id];
        if (v < 0) {
            const std::string& name = BlockRegistry::Instance().Name(id);
            v = name == "minecraft:sand" || name == "minecraft:red_sand" || name == "minecraft:gravel" ||
                        name == "minecraft:suspicious_sand" || name == "minecraft:suspicious_gravel" ||
                        name.ends_with("concrete_powder") || name.ends_with("anvil") || name == "minecraft:dragon_egg"
                    ? 1
                    : 0;
        }
        return v == 1;
    }

    bool FallingFree(uint16_t id) {
        if (id == 0 || BlockLiquid(id))
            return true;
        static std::vector<int8_t> memo;
        static std::mutex memoMutex;
        std::lock_guard lock(memoMutex);
        if (id >= memo.size())
            memo.resize(static_cast<size_t>(id) + 1, -1);
        int8_t& v = memo[id];
        if (v < 0) {
            const std::string& name = BlockRegistry::Instance().Name(id);
            v = name == "minecraft:air" || name == "minecraft:fire" || name == "minecraft:soul_fire" ||
                        name == "minecraft:short_grass" || name == "minecraft:tall_grass" || name == "minecraft:fern" ||
                        name == "minecraft:large_fern" || name == "minecraft:dead_bush" || name == "minecraft:seagrass" ||
                        name == "minecraft:tall_seagrass" || name == "minecraft:vine" || name == "minecraft:structure_void" ||
                        name == "minecraft:light_block" || name.ends_with("_air") || name == "minecraft:bush" ||
                        name == "minecraft:short_dry_grass" || name == "minecraft:tall_dry_grass"
                    ? 1
                    : 0;
        }
        return v == 1;
    }

    namespace {
        float BlastResistance(uint16_t id) {
            static std::vector<float> memo;
            static std::mutex memoMutex;
            std::lock_guard lock(memoMutex);
            if (id >= memo.size())
                memo.resize(static_cast<size_t>(id) + 1, -1.0f);
            float& v = memo[id];
            if (v >= 0.0f)
                return v;
            const std::string& n = BlockRegistry::Instance().Name(id);
            const auto has = [&](const char* t) { return n.find(t) != std::string::npos; };
            if (has("bedrock") || has("barrier") || has("command_block") || has("end_portal") || has("end_gateway") ||
                has("structure_block") || has("jigsaw") || has("reinforced_deepslate") || n == "minecraft:light_block" ||
                has("border_block") || has("allow") || has("deny"))
                v = 3600000.0f;
            else if (has("obsidian") || has("ancient_debris") || has("netherite_block") || has("respawn_anchor") ||
                     has("enchanting_table") || has("ender_chest"))
                v = 1200.0f;
            else if (has("anvil"))
                v = 1200.0f;
            else if (has("water") || has("lava"))
                v = 100.0f;
            else if (has("deepslate") || has("iron_block") || has("iron_door") || has("iron_bars") || has("copper") ||
                     has("diamond_block") || has("emerald_block") || has("gold_block") || has("bell") ||
                     has("hopper") || has("cauldron") || has("lodestone") || has("crafter"))
                v = 6.0f;
            else if (has("stone") || has("cobble") || has("brick") || has("andesite") || has("diorite") ||
                     has("granite") || has("tuff") || has("basalt") || has("blackstone") || has("prismarine") ||
                     has("purpur") || has("calcite") || has("_ore") || has("concrete") && !has("powder") ||
                     has("terracotta") || has("quartz") || has("dripstone"))
                v = 6.0f;
            else if (has("planks") || has("_log") || has("_wood") || has("stem") || has("hyphae") || has("fence") ||
                     has("door") || has("chest") || has("barrel") || has("crafting_table") || has("bookshelf") ||
                     has("slab") || has("stairs"))
                v = 3.0f;
            else if (has("glass"))
                v = 0.3f;
            else if (has("leaves") || has("wool") || has("carpet") || has("snow") || has("cactus") || has("sponge"))
                v = 0.8f;
            else if (has("dirt") || has("grass_block") || has("sand") || has("gravel") || has("clay") || has("farmland") ||
                     has("mud") || has("podzol") || has("mycelium") || has("soul_soil") || has("netherrack"))
                v = 0.5f;
            else if (has("tnt"))
                v = 0.0f;
            else
                v = 1.0f;
            return v;
        }
    }

    void MobWorld::Explosion(double x, double y, double z, float power, uint64_t source, bool breakBlocks) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const int cx = static_cast<int>(std::floor(x)), cy = static_cast<int>(std::floor(y)), cz = static_cast<int>(std::floor(z));
        const bool underwater = BlockLiquid(server.Chunks().GetBlock(cx, cy, cz));
        std::vector<std::array<int, 3>> broken;
        if (breakBlocks && !underwater) {
            std::unordered_set<int64_t> seen;
            for (int i = 0; i < 16; i++)
                for (int j = 0; j < 16; j++)
                    for (int k = 0; k < 16; k++) {
                        if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15)
                            continue;
                        double dx = i / 15.0 * 2.0 - 1.0, dy = j / 15.0 * 2.0 - 1.0, dz = k / 15.0 * 2.0 - 1.0;
                        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                        dx /= len;
                        dy /= len;
                        dz /= len;
                        float intensity = power * (0.7f + unit(rng) * 0.6f);
                        double px = x, py = y, pz = z;
                        while (intensity > 0.0f) {
                            const int bx = static_cast<int>(std::floor(px)), by = static_cast<int>(std::floor(py)),
                                      bz = static_cast<int>(std::floor(pz));
                            if (by < kMinY || by >= kMinY + kHeight || !server.Chunks().Peek({bx >> 4, bz >> 4}))
                                break;
                            const uint16_t b = server.Chunks().GetBlock(bx, by, bz);
                            if (b != 0)
                                intensity -= (BlastResistance(b) + 0.3f) * 0.3f;
                            if (intensity > 0.0f && b != 0 && !BlockLiquid(b)) {
                                const int64_t key = (static_cast<int64_t>(bx) & 0x3FFFFFF) << 38 |
                                                    (static_cast<int64_t>(bz) & 0x3FFFFFF) << 12 |
                                                    (static_cast<int64_t>(by + 2048) & 0xFFF);
                                if (seen.insert(key).second)
                                    broken.push_back({bx, by, bz});
                            }
                            px += dx * 0.3;
                            py += dy * 0.3;
                            pz += dz * 0.3;
                            intensity -= 0.22500001f;
                        }
                    }
        }
        for (const auto& b : broken) {
            const uint16_t id = server.Chunks().GetBlock(b[0], b[1], b[2]);
            if (BlockRegistry::Instance().Name(id) == "minecraft:tnt") {
                server.SetBlockIn(kOverworld, b[0], b[1], b[2], BlockRegistry::kAir);
                SpawnPrimedTnt(b[0] + 0.5, b[1], b[2] + 0.5, 10 + static_cast<int>(rng() % 20));
                continue;
            }
            server.SetBlockIn(kOverworld, b[0], b[1], b[2], BlockRegistry::kAir);
        }
        const double reach = power * 2.0;
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->SendLevelEvent(power >= 2.0f ? 16400 : 16390, x, y, z, 0);
            s->PlaySound("Explode", x, y, z);
            const double dx = s->X() - x, dy = s->Y() - 1.0 - y, dz = s->Z() - z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < reach && s->GameMode() != 1 && !s->Spectator()) {
                const double impact = 1.0 - d / reach;
                s->Hurt(static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0), x, z);
            }
        }
        for (Mob& o : mobs) {
            if (o.id == source || o.deathTime != 0)
                continue;
            const double dx = o.x - x, dy = o.y - y, dz = o.z - z;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d >= reach)
                continue;
            const double impact = 1.0 - d / reach;
            if (o.kind != 0) {
                const double len = std::max(0.01, d);
                o.vx += dx / len * impact;
                o.vy += dy / len * impact;
                o.vz += dz / len * impact;
                continue;
            }
            Hurt(o, static_cast<float>((impact * impact + impact) / 2.0 * 7.0 * reach + 1.0), x, z, nullptr);
        }
    }

    void MobWorld::Explode(Mob& m) {
        Explosion(m.x, m.y + 0.5, m.z, 3.0f, m.id, server.RuleValue("mobgriefing"));
        m.health = 0;
        m.deathTime = 19;
        for (Session* s : server.PlayersIn(kOverworld))
            s->HideItem(m.id);
    }

    Mob* MobWorld::SpawnFallingBlock(double x, double y, double z, uint16_t block) {
        static const MobDef falling{"minecraft:falling_block", 0.98f, 0.98f, 1, 0, 0, 0, "", "", ""};
        Mob m;
        m.def = &falling;
        m.kind = 1;
        m.block = block;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        m.health = 1;
        m.persistent = true;
        mobs.push_back(m);
        Mob& added = mobs.back();
        for (Session* s : server.PlayersIn(kOverworld))
            s->ShowMob(added);
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    Mob* MobWorld::SpawnPrimedTnt(double x, double y, double z, int fuse) {
        static const MobDef tnt{"minecraft:tnt", 0.98f, 0.98f, 1, 0, 0, 0, "", "", ""};
        Mob m;
        m.def = &tnt;
        m.kind = 2;
        m.fuse = fuse;
        m.id = nextId++;
        m.x = x;
        m.y = y;
        m.z = z;
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        const double angle = unit(rng) * 2.0 * kPi;
        m.vx = -std::sin(angle) * 0.02;
        m.vy = 0.2;
        m.vz = -std::cos(angle) * 0.02;
        m.health = 1;
        m.persistent = true;
        mobs.push_back(m);
        Mob& added = mobs.back();
        for (Session* s : server.PlayersIn(kOverworld)) {
            s->ShowMob(added);
            s->MobMotion(added);
            s->PlaySound("Fuse", x, y, z);
        }
        added.lastX = static_cast<float>(x);
        added.lastY = static_cast<float>(y);
        added.lastZ = static_cast<float>(z);
        added.lastFlags = Flags(added);
        return &added;
    }

    bool MobWorld::TickSpecial(Mob& m) {
        const auto blockAt = [&](int bx, int by, int bz) -> uint16_t {
            if (by < kMinY || by >= kMinY + kHeight || !server.Chunks().Peek({bx >> 4, bz >> 4}))
                return BlockRegistry::kAir;
            return server.Chunks().GetBlock(bx, by, bz);
        };
        const int bx = static_cast<int>(std::floor(m.x)), bz = static_cast<int>(std::floor(m.z));
        if (m.kind == 1) {
            if (m.age > 600 || m.y < kMinY - 64)
                return true;
            m.vy = (m.vy - 0.04) * 0.98;
            const double ny = m.y + m.vy;
            for (int c = static_cast<int>(std::floor(m.y)) - (m.y == std::floor(m.y) ? 1 : 0); c >= static_cast<int>(std::floor(ny)); c--) {
                if (c < kMinY)
                    return true;
                if (!FallingFree(blockAt(bx, c, bz))) {
                    const int place = c + 1;
                    if (FallingFree(blockAt(bx, place, bz)))
                        server.SetBlockIn(kOverworld, bx, place, bz, m.block);
                    return true;
                }
                if (c == static_cast<int>(std::floor(ny)))
                    break;
            }
            m.y = ny;
            return false;
        }
        if (m.kind == 2) {
            m.vy -= 0.04;
            const auto solid = [&](double px, double py, double pz) {
                return BlockSolid(blockAt(static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py)),
                                          static_cast<int>(std::floor(pz))));
            };
            double nx = m.x + m.vx, ny = m.y + m.vy, nz = m.z + m.vz;
            m.onGround = false;
            if (solid(m.x, ny, m.z)) {
                if (m.vy < 0) {
                    ny = std::floor(ny) + 1.0;
                    m.onGround = true;
                } else {
                    ny = m.y;
                }
                m.vy = 0;
            }
            if (solid(nx, ny + 0.1, m.z)) {
                nx = m.x;
                m.vx = 0;
            }
            if (solid(nx, ny + 0.1, nz)) {
                nz = m.z;
                m.vz = 0;
            }
            m.x = nx;
            m.y = ny;
            m.z = nz;
            m.vx *= 0.98;
            m.vy *= 0.98;
            m.vz *= 0.98;
            if (m.onGround) {
                m.vx *= 0.7;
                m.vz *= 0.7;
            }
            if (--m.fuse <= 0) {
                Explosion(m.x, m.y + 0.0625, m.z, 4.0f, m.id, server.RuleValue("tntexplodes") != 0);
                return true;
            }
            return false;
        }
        return true;
    }

    void MobWorld::Shoot(Mob& m, Session& at) {
        Mob* arrow = Spawn("minecraft:arrow", m.x, m.y + m.def->height * 0.85, m.z, false);
        if (!arrow)
            return;
        Mob* self = Find(m.id);
        const double dx = at.X() - arrow->x, dy = (at.Y() - 0.6) - arrow->y, dz = at.Z() - arrow->z;
        const double flat = std::sqrt(dx * dx + dz * dz);
        const double len = std::max(0.01, std::sqrt(dx * dx + dy * dy + dz * dz));
        const double speed = 1.6;
        arrow->vx = dx / len * speed;
        arrow->vy = (dy + flat * 0.2) / len * speed;
        arrow->vz = dz / len * speed;
        arrow->owner = self ? self->id : 0;
        arrow->yaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
        for (Session* s : server.PlayersIn(kOverworld))
            s->MobMotion(*arrow);
    }

    void MobWorld::Physics(Mob& m) {
        const uint16_t here = server.Chunks().GetBlock(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.y + 0.2)),
                                                       static_cast<int>(std::floor(m.z)));
        m.inWater = BlockLiquid(here);
        const bool flying = (m.def->flags & mobflag::Fly) != 0;
        const bool swimmer = (m.def->flags & mobflag::Aquatic) != 0;
        if (m.projectile) {
            m.vy -= 0.05;
            m.vx *= 0.99;
            m.vy *= 0.99;
            m.vz *= 0.99;
        } else if (m.inWater) {
            if (swimmer)
                m.vy *= 0.8;
            else
                m.vy = m.vy * 0.8 + 0.03;
            m.vx *= 0.8;
            m.vz *= 0.8;
        } else if (flying) {
            m.vx *= 0.91;
            m.vy *= 0.91;
            m.vz *= 0.91;
        } else {
            m.vy = (m.vy - 0.08) * 0.98;
        }
        double nx = m.x + m.vx, ny = m.y + m.vy, nz = m.z + m.vz;
        const bool wasGround = m.onGround;
        m.onGround = false;
        if (m.projectile) {
            if (Solid(nx, ny, nz)) {
                m.vx = m.vy = m.vz = 0;
                m.age = std::max(m.age, 1100);
                return;
            }
            m.x = nx;
            m.y = ny;
            m.z = nz;
            return;
        }
        if (!Collides(m, m.x, ny, m.z)) {
            m.y = ny;
        } else {
            if (m.vy < 0) {
                m.onGround = true;
                m.y = std::floor(ny) + 1.0;
                if (Collides(m, m.x, m.y, m.z))
                    m.y = std::floor(m.y + 0.5);
            }
            m.vy = 0;
        }
        bool blocked = false;
        if (!Collides(m, nx, m.y, m.z))
            m.x = nx;
        else
            blocked = true, m.vx = 0;
        if (!Collides(m, m.x, m.y, nz))
            m.z = nz;
        else
            blocked = true, m.vz = 0;
        if (blocked && (wasGround || m.onGround || m.inWater) && m.hasGoal && !flying) {
            if (!Collides(m, m.x, m.y + 1.05, m.z))
                m.vy = m.inWater ? 0.12 : 0.42;
            else
                m.hasGoal = false;
        }
        if (m.def->flags & mobflag::Spider && blocked && m.hasGoal)
            m.vy = 0.2;
        if (m.onGround) {
            m.vx *= 0.546;
            m.vz *= 0.546;
        } else if (!flying && !m.inWater) {
            m.vx *= 0.91;
            m.vz *= 0.91;
        }
        if (m.y < kMinY - 64)
            m.health = 0;
    }

    void MobWorld::Think(Mob& m) {
        if (m.projectile) {
            for (Session* s : server.PlayersIn(kOverworld)) {
                if (s->Spectator() || s->GameMode() == 1 || s->Dead())
                    continue;
                const double dx = s->X() - m.x, dy = (s->Y() - 0.9) - m.y, dz = s->Z() - m.z;
                if (std::abs(dx) < 0.5 && std::abs(dz) < 0.5 && std::abs(dy) < 1.0 && (m.vx || m.vz)) {
                    const double speed = std::sqrt(m.vx * m.vx + m.vy * m.vy + m.vz * m.vz);
                    s->Hurt(static_cast<float>(std::ceil(speed * 2.0)), m.x - m.vx, m.z - m.vz);
                    m.age = 2000;
                    break;
                }
            }
            return;
        }
        const MobDef& d = *m.def;
        const bool hostile = (d.flags & mobflag::Hostile) != 0;
        const bool flying = (d.flags & mobflag::Fly) != 0;
        const bool swimmer = (d.flags & mobflag::Aquatic) != 0;
        double speed = d.speed * (m.baby ? 1.5 : 1.0);
        Session* target = nullptr;
        if (m.target) {
            target = server.FindSession(m.target);
            if (!target || !target->InGame() || target->Dead() || target->GameMode() == 1 || target->Spectator()) {
                m.target = 0;
                m.angry = false;
                target = nullptr;
            }
        }
        if (hostile && !target && server.Config().difficulty > 0 && m.age % 10 == 0)
            if (Session* s = Nearest(m, d.flags & mobflag::Ranged ? 16.0 : 24.0, true)) {
                if (!(m.def->id == std::string("minecraft:enderman")))
                    m.target = s->Id(), target = s;
            }
        if (server.Config().difficulty == 0 && hostile && !m.persistent) {
            m.health = 0;
            return;
        }
        if (d.flags & mobflag::Burns) {
            const bool sky = SurfaceY(static_cast<int>(std::floor(m.x)), static_cast<int>(std::floor(m.z))) <= m.y + d.height;
            m.burning = Day() && sky && !m.inWater && !server.Raining() && !m.baby;
            if (m.burning && ++m.burnTicks % 20 == 0)
                Hurt(m, 1.0f, m.x, m.z, nullptr);
        }
        if (m.panicTicks > 0) {
            m.panicTicks--;
            if (!m.hasGoal || m.goalTicks <= 0) {
                const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
                m.goalX = m.x + std::cos(a) * 8;
                m.goalZ = m.z + std::sin(a) * 8;
                m.goalY = m.y;
                m.hasGoal = true;
                m.goalTicks = 40;
            }
            speed *= 1.6;
        } else if (target) {
            const double dx = target->X() - m.x, dz = target->Z() - m.z, dy = (target->Y() - 1.62) - m.y;
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (d.flags & mobflag::Explodes) {
                if (dist < 3.0 && std::abs(dy) < 3) {
                    if (m.fuse < 0)
                        m.fuse = 0;
                    m.hasGoal = false;
                } else if (m.fuse >= 0 && dist > 7.0) {
                    m.fuse = -1;
                }
            }
            if (d.flags & mobflag::Ranged) {
                m.hasGoal = dist > 10.0;
                m.goalX = target->X();
                m.goalZ = target->Z();
                m.goalY = target->Y();
                if (m.shootCooldown > 0)
                    m.shootCooldown--;
                else if (dist < 16.0) {
                    Shoot(m, *target);
                    m.shootCooldown = 40 + static_cast<int>(rng() % 20);
                }
            } else if (m.fuse < 0) {
                m.goalX = target->X();
                m.goalZ = target->Z();
                m.goalY = target->Y();
                m.hasGoal = true;
                m.goalTicks = 20;
                const double reach = d.width * 2.0 * 0.5 + 0.9;
                if (dist < reach + 0.5 && std::abs(dy) < 2.0 && m.attackCooldown <= 0 && d.damage > 0) {
                    float damage = d.damage;
                    if (server.Config().difficulty == 1)
                        damage = std::min(damage / 2.0f + 1.0f, damage);
                    else if (server.Config().difficulty == 3)
                        damage *= 1.5f;
                    target->Hurt(damage, m.x, m.z);
                    m.attackCooldown = 20;
                    for (Session* s : server.PlayersIn(kOverworld))
                        s->MobEvent(m.id, 4, 0);
                }
            }
            speed *= hostile ? 1.0 : 1.4;
            m.headYaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
            m.pitch = static_cast<float>(-std::atan2(dy + 1.62 - d.height * 0.85, std::max(0.01, dist)) * 180.0 / kPi);
        } else {
            Session* tempter = nullptr;
            if (*d.tempt && m.age % 5 == 0)
                if (Session* s = Nearest(m, 10.0, false)) {
                    const ItemStack& held = s->Inventory().main[static_cast<size_t>(s->Inventory().selected)];
                    if (!held.Empty())
                        if (const CatalogItem* item = BlockCatalog::Instance().ItemById(held.networkId)) {
                            const std::string n = item->name.substr(item->name.find(':') + 1);
                            std::istringstream list(d.tempt);
                            std::string t;
                            while (std::getline(list, t, ','))
                                if (t == n)
                                    tempter = s;
                        }
                }
            if (tempter) {
                m.goalX = tempter->X();
                m.goalZ = tempter->Z();
                m.goalY = tempter->Y();
                const double dx = m.goalX - m.x, dz = m.goalZ - m.z;
                m.hasGoal = dx * dx + dz * dz > 4.0;
                m.goalTicks = 20;
                m.headYaw = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
            } else if (!m.hasGoal || m.goalTicks <= 0) {
                m.hasGoal = false;
                if (rng() % (flying || swimmer ? 40 : 120) == 0) {
                    const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
                    const double r = 3 + rng() % 8;
                    m.goalX = m.x + std::cos(a) * r;
                    m.goalZ = m.z + std::sin(a) * r;
                    m.goalY = m.y + (flying || swimmer ? static_cast<double>(static_cast<int>(rng() % 7) - 3) : 0);
                    m.hasGoal = true;
                    m.goalTicks = 120;
                } else if (m.age % 40 == 0) {
                    if (Session* s = Nearest(m, 8.0, false)) {
                        if (rng() % 3 == 0)
                            m.headYaw = static_cast<float>(std::atan2(-(s->X() - m.x), s->Z() - m.z) * 180.0 / kPi);
                    }
                }
            }
        }
        if (m.fuse >= 0) {
            if (++m.fuse >= 30) {
                Explode(m);
                return;
            }
        }
        if (m.attackCooldown > 0)
            m.attackCooldown--;
        if (m.goalTicks > 0)
            m.goalTicks--;
        if (swimmer && !m.inWater) {
            if (m.onGround && m.age % 10 == 0)
                m.vy = 0.3, m.vx += (rng() % 100 - 50) / 500.0, m.vz += (rng() % 100 - 50) / 500.0;
            if (m.age % 20 == 0)
                Hurt(m, 1.0f, m.x, m.z, nullptr);
            return;
        }
        if (m.hasGoal) {
            const double dx = m.goalX - m.x, dz = m.goalZ - m.z;
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (dist < 0.5 && !target) {
                m.hasGoal = false;
            } else {
                const double step = speed * (m.onGround || flying || m.inWater ? 0.45 : 0.08);
                m.vx += dx / std::max(dist, 0.01) * step * 0.35;
                m.vz += dz / std::max(dist, 0.01) * step * 0.35;
                const double hv = std::sqrt(m.vx * m.vx + m.vz * m.vz);
                if (hv > step && hv > 0) {
                    m.vx *= step / hv;
                    m.vz *= step / hv;
                }
                if (flying || (swimmer && m.inWater))
                    m.vy += std::clamp((m.goalY - m.y) * 0.02, -0.05, 0.05);
                const float want = static_cast<float>(std::atan2(-dx, dz) * 180.0 / kPi);
                float diff = std::fmod(want - m.yaw + 540.0f, 360.0f) - 180.0f;
                m.yaw += std::clamp(diff, -30.0f, 30.0f);
                if (!target)
                    m.headYaw = m.yaw;
                if (!flying && !m.inWater && m.onGround && !target) {
                    const int ax = static_cast<int>(std::floor(m.x + dx / std::max(dist, 0.01)));
                    const int az = static_cast<int>(std::floor(m.z + dz / std::max(dist, 0.01)));
                    const int by = static_cast<int>(std::floor(m.y));
                    if (!Solid(ax + 0.5, by - 0.5, az + 0.5) && !Solid(ax + 0.5, by - 1.5, az + 0.5) &&
                        !Solid(ax + 0.5, by - 2.5, az + 0.5))
                        m.hasGoal = false, m.vx = 0, m.vz = 0;
                    const uint16_t ahead = server.Chunks().GetBlock(ax, by, az);
                    const uint16_t below = server.Chunks().GetBlock(ax, by - 1, az);
                    if (BlockRegistry::Instance().Name(ahead).contains("lava") ||
                        BlockRegistry::Instance().Name(below).contains("lava"))
                        m.hasGoal = false, m.vx = 0, m.vz = 0;
                }
            }
        }
    }

    void MobWorld::Broadcast(Mob& m, bool force) {
        const bool moved = std::abs(static_cast<float>(m.x) - m.lastX) > 0.005f || std::abs(static_cast<float>(m.y) - m.lastY) > 0.005f ||
                           std::abs(static_cast<float>(m.z) - m.lastZ) > 0.005f || Angle(m.yaw) != Angle(m.lastYaw) ||
                           Angle(m.headYaw) != Angle(m.lastHead);
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        if (moved || force) {
            for (Session* s : players)
                s->MoveMob(m);
            m.lastX = static_cast<float>(m.x);
            m.lastY = static_cast<float>(m.y);
            m.lastZ = static_cast<float>(m.z);
            m.lastYaw = m.yaw;
            m.lastHead = m.headYaw;
        }
        const uint64_t flags = Flags(m);
        if (flags != m.lastFlags || force) {
            for (Session* s : players)
                s->MobData(m);
            m.lastFlags = flags;
        }
    }

    void MobWorld::SpawnPack(const std::string& category, int x, int z, int surfaceY, bool chunkGen) {
        const auto entry = server.Chunks().Peek({x >> 4, z >> 4});
        if (!entry)
            return;
        const json::Value* table = server.Chunks().Backend().Spawns(entry->column.GenBiome(x & 15, z & 15));
        if (!table)
            return;
        const json::Value& list = (*table)[category];
        if (!list.IsArray() || list.items.empty())
            return;
        int64_t total = 0;
        for (const auto& e : list.items)
            total += e["weight"].Int(1);
        if (total <= 0)
            return;
        int64_t pick = static_cast<int64_t>(rng() % static_cast<uint64_t>(total));
        const json::Value* chosen = nullptr;
        for (const auto& e : list.items) {
            pick -= e["weight"].Int(1);
            if (pick < 0) {
                chosen = &e;
                break;
            }
        }
        if (!chosen)
            return;
        const std::string type = (*chosen)["type"].text;
        const MobDef* def = FindMob(type.c_str());
        if (!def)
            return;
        int count = 1;
        const json::Value& c = (*chosen)["count"];
        if (c.kind == json::Value::Kind::Number)
            count = static_cast<int>(c.Int());
        else if (c.IsObject())
            count = static_cast<int>(c["min_inclusive"].Int(1) +
                                     static_cast<int64_t>(rng() % static_cast<unsigned>(std::max<int64_t>(1, c["max_inclusive"].Int(1) - c["min_inclusive"].Int(1) + 1))));
        count = std::clamp(count, 1, 8);
        const bool water = (def->flags & mobflag::Aquatic) != 0;
        for (int i = 0; i < count; i++) {
            const int px = x + static_cast<int>(rng() % 7) - 3, pz = z + static_cast<int>(rng() % 7) - 3;
            const int y = chunkGen ? SurfaceY(px, pz) : surfaceY;
            if (y <= kMinY)
                continue;
            const uint16_t ground = server.Chunks().GetBlock(px, y - 1, pz);
            if (water != BlockLiquid(ground) && !water)
                continue;
            if (water && !BlockLiquid(ground))
                continue;
            const double sy = water ? y - 2.0 : y;
            Mob probe;
            probe.def = def;
            if (Collides(probe, px + 0.5, sy, pz + 0.5))
                continue;
            if (!water && category == "creature") {
                const std::string g = BlockRegistry::Instance().Name(ground);
                if (!g.contains("grass_block") && !g.contains("snow") && !g.contains("sand") && !g.contains("podzol") &&
                    !g.contains("mycelium") && !g.contains("dirt") && !g.contains("moss"))
                    continue;
            }
            Mob* m = Spawn(def->id, px + 0.5, sy, pz + 0.5, category == "creature");
            if (m && (def->flags & mobflag::Hostile) == 0 && rng() % 20 == 0)
                m->baby = true;
        }
    }

    void MobWorld::OnChunkReady(int chunkX, int chunkZ) {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32) | static_cast<uint32_t>(chunkZ);
        if (!spawnedChunks.insert(key).second)
            return;
        if (!server.RuleValue("domobspawning"))
            return;
        if (std::uniform_real_distribution<double>(0, 1)(rng) >= 0.1)
            return;
        const int x = chunkX * 16 + static_cast<int>(rng() % 16), z = chunkZ * 16 + static_cast<int>(rng() % 16);
        SpawnPack("creature", x, z, 0, true);
    }

    void MobWorld::NaturalSpawns() {
        if (!server.RuleValue("domobspawning"))
            return;
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        if (players.empty())
            return;
        int monsters = 0, water = 0, ambient = 0;
        for (const Mob& m : mobs) {
            if (m.deathTime || m.projectile)
                continue;
            if (m.def->flags & mobflag::Hostile)
                monsters++;
            else if (m.def->flags & mobflag::Aquatic)
                water++;
            else if (m.def->flags & mobflag::Fly)
                ambient++;
        }
        const size_t n = players.size();
        for (Session* s : players) {
            if (s->Spectator())
                continue;
            const double a = std::uniform_real_distribution<double>(0, 2 * kPi)(rng);
            const double r = 24 + static_cast<double>(rng() % 80);
            const int x = static_cast<int>(std::floor(s->X() + std::cos(a) * r));
            const int z = static_cast<int>(std::floor(s->Z() + std::sin(a) * r));
            if (!server.Chunks().Peek({x >> 4, z >> 4}))
                continue;
            const int surface = SurfaceY(x, z);
            if (surface <= kMinY)
                continue;
            if (server.Config().difficulty > 0 && monsters < static_cast<int>(40 * n)) {
                int y = surface;
                if (rng() % 3 == 0) {
                    y = kMinY + 8 + static_cast<int>(rng() % static_cast<unsigned>(std::max(1, surface - kMinY - 8)));
                    while (y > kMinY + 1 && !BlockSolid(server.Chunks().GetBlock(x, y - 1, z)))
                        y--;
                    if (BlockSolid(server.Chunks().GetBlock(x, y, z)) || BlockSolid(server.Chunks().GetBlock(x, y + 1, z)))
                        y = surface;
                }
                if (Dark(x, y, z) && !BlockLiquid(server.Chunks().GetBlock(x, y - 1, z)))
                    SpawnPack("monster", x, z, y, false);
            }
            if (water < static_cast<int>(5 * n) && rng() % 4 == 0)
                SpawnPack("water_creature", x, z, surface, true);
            if (ambient < static_cast<int>(8 * n) && rng() % 8 == 0 && !Day())
                SpawnPack("ambient", x, z, surface, true);
        }
    }

    void MobWorld::Tick() {
        if (++spawnTick % 20 == 0)
            NaturalSpawns();
        const std::vector<Session*> players = server.PlayersIn(kOverworld);
        for (auto it = mobs.begin(); it != mobs.end();) {
            Mob& m = *it;
            m.age++;
            bool remove = false;
            if (m.deathTime > 0) {
                remove = ++m.deathTime > 20;
            } else if (m.projectile && m.age > 1200 && m.kind == 0) {
                remove = true;
            } else if (server.Chunks().Peek({static_cast<int>(std::floor(m.x)) >> 4, static_cast<int>(std::floor(m.z)) >> 4})) {
                if (!m.persistent && !m.projectile) {
                    double nearest = 1e18;
                    for (Session* s : players) {
                        const double dx = s->X() - m.x, dz = s->Z() - m.z, dy = s->Y() - m.y;
                        nearest = std::min(nearest, dx * dx + dy * dy + dz * dz);
                    }
                    remove = nearest > 128.0 * 128.0 || (nearest > 32.0 * 32.0 && rng() % 800 == 0);
                }
                if (!remove && m.kind != 0) {
                    remove = TickSpecial(m);
                    if (!remove)
                        Broadcast(m, false);
                } else if (!remove) {
                    if (m.hurtTime > 0)
                        m.hurtTime--;
                    Think(m);
                    if (m.deathTime == 0) {
                        if (m.health <= 0)
                            Die(m, nullptr);
                        else {
                            Physics(m);
                            Broadcast(m, false);
                        }
                    }
                }
            }
            if (remove) {
                for (Session* s : players)
                    s->HideItem(m.id);
                it = mobs.erase(it);
            } else {
                ++it;
            }
        }
    }
}
