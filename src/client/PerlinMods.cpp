#include "client/PerlinInternal.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/renderer/LevelRenderer.hpp"
#include "client/resources/ServerPacks.hpp"
#include <filesystem>
#include "client/Perlin.hpp"

#include "client/i18n/Lang.hpp"
#include "client/input/Keys.hpp"
#include "client/mods/ModManager.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/player/BlockDestroy.hpp"
#include "client/player/BlockUse.hpp"
#include "client/multiplayer/BlockEntities.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/player/Interaction.hpp"
#include "client/resources/ShaderPack.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/McFont.hpp"
#include "network/Transport.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include "util/Platform.hpp"

namespace perlin {

    namespace {
        constexpr uint8_t kMapWaterFlag = 2, kMapLavaFlag = 4, kMapLeavesFlag = 8, kMapSnowFlag = 16, kMapRockFlag = 32;
        using abi::Status;

        constexpr const char* PlatformName() { return platform::kName; }

        abi::Item ToItem(const BedrockSession::ItemStackInfo& s) {
            abi::Item i;
            if (s.Empty())
                return i;
            i.name = s.name;
            i.count = s.count;
            i.aux = s.aux;
            i.damage = s.damage;
            i.enchanted = s.enchanted;
            i.customName = s.customName;
            for (const auto& e : s.enchants)
                i.enchants.emplace_back(e.first, e.second);
            i.lore = s.lore;
            return i;
        }

        abi::Entity ToEntity(const entity::Entity& e) {
            abi::Entity out;
            out.runtimeId = e.runtimeId;
            out.uniqueId = e.uniqueId;
            out.type = e.type;
            out.name = e.name;
            out.nameTag = e.nameTag;
            out.uuid = e.uuid;
            out.x = e.pos.x;
            out.y = e.pos.y;
            out.z = e.pos.z;
            out.yaw = e.yaw;
            out.pitch = e.pitch;
            out.headYaw = e.headYaw;
            out.health = e.health;
            out.scale = e.scale;
            out.onGround = e.onGround;
            out.isPlayer = e.isPlayer;
            out.isItem = e.isItem;
            out.isSelf = e.isSelf;
            out.flags = abi::EntityFlags::from_bits(e.flags);
            out.itemName = e.itemName;
            out.itemCount = e.itemCount;
            out.mainHand = e.mainHand;
            out.offHand = e.offHand;
            for (int i = 0; i < 4; i++)
                out.armor[static_cast<size_t>(i)] = e.armor[i];
            out.vehicleUid = e.vehicleUid;
            out.ownerUid = e.ownerUid;
            out.targetUid = e.targetUid;
            out.variant = e.variant;
            out.markVariant = e.markVariant;
            out.color = e.color;
            out.dying = e.dying;
            const auto health = e.attributes.find("minecraft:health");
            out.maxHealth = health != e.attributes.end() ? health->second.max : -1.0f;
            for (const auto& [name, a] : e.attributes)
                out.attributes.push_back({name, a.min, a.value, a.max});
            for (const auto& [key, value] : e.props)
                out.properties.emplace_back(key, value);
            for (const auto& [key, text] : e.propText)
                out.propertyText.emplace_back(key, text);
            return out;
        }

        class NameCache {
        public:
            const std::string& Get(uint16_t id) {
                const auto it = names.find(id);
                if (it != names.end())
                    return it->second;
                return names.emplace(id, BlockPalette::Instance().Name(id)).first->second;
            }

        private:
            std::unordered_map<uint16_t, std::string> names;
        };

        std::string StateText(const BlockPalette::StateValue& v) {
            switch (v.kind) {
            case BlockPalette::StateValue::Bool:
                return v.i ? "true" : "false";
            case BlockPalette::StateValue::Int:
                return std::to_string(v.i);
            default:
                return v.s;
            }
        }

        bool MatchName(const std::string& have, const std::string& want, bool exact) {
            if (want.empty())
                return false;
            if (exact)
                return have == want || have == "minecraft:" + want;
            return have.contains(want);
        }

        abi::Item ToItem(const ui::HudItem& s) {
            abi::Item i;
            if (s.Empty())
                return i;
            i.name = s.name;
            i.count = s.count;
            i.aux = s.aux;
            i.damage = s.maxDamage > 0 ? s.aux : 0;
            i.enchanted = s.enchanted;
            i.customName = s.customName;
            for (const auto& e : s.enchants)
                i.enchants.emplace_back(e.first, e.second);
            i.lore = s.lore;
            return i;
        }

        bool Finite(double v) {
            return std::isfinite(v) && std::fabs(v) < 1.0e7;
        }

        struct OptionAccess {
            std::string key;
            std::function<std::string()> get;
            std::function<bool(const std::string&)> set;
        };
    }

    abi::Screen Perlin::CurrentScreen() const {
        if (modHooks.customOpen)
            return abi::Screen::Custom;
        if (death.shown)
            return abi::Screen::Death;
        if (inventory.shown)
            return inventory.workbench ? abi::Screen::Workbench : abi::Screen::Inventory;
        if (pause.shown) {
            switch (pause.view) {
            case 1:
                return abi::Screen::Options;
            case 4:
                return abi::Screen::Mods;
            case 5:
                return abi::Screen::Wardrobe;
            default:
                return abi::Screen::Pause;
            }
        }
        if (chat.IsOpen())
            return abi::Screen::Chat;
        if (disconnect.shown)
            return abi::Screen::Other;
        return abi::Screen::None;
    }

    void Perlin::SetupMods() {
        mods::ModManager::Instance().SetEnvironment(kBedrockVersion, PlatformName());
        SetupModPlayerApi();
        SetupModWorldApi();
        SetupModClientApi();
        SetupModActions();
        SetupModAdvanced();
        SetupModHooks();
        // registered last: these replace any earlier handler for the same op
        SetupModInteractionApi();
    }

    void Perlin::SetupModPlayerApi() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using mods::CallContext;
        mm.Register(Op::PlayerState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::PlayerState p;
            const physics::State& st = player.State();
            p.x = modLive ? st.x : camera.x;
            p.y = modLive ? st.y : camera.y - 1.62;
            p.z = modLive ? st.z : camera.z;
            p.vx = st.vx;
            p.vy = st.vy;
            p.vz = st.vz;
            p.yaw = camera.yaw - 90.0f;
            p.pitch = -camera.pitch;
            p.headYaw = p.yaw;
            p.onGround = st.onGround;
            p.sprinting = player.Sprinting();
            p.sneaking = player.Sneaking();
            p.swimming = player.Swimming();
            p.gliding = player.Gliding();
            p.eyeHeight = p.sneaking ? 1.27f : 1.62f;
            p.selectedSlot = hudState.selectedSlot;
            if (modLive) {
                const BedrockSession::Vitals v = modLive->GetVitals();
                p.runtimeId = modLive->GetRuntimeId();
                p.name = modLive->PlayerName();
                p.flying = modLive->IsFlying();
                p.mayFly = modLive->MayFly();
                p.spectator = modLive->Spectator();
                p.riding = modLive->RidingUid() != 0;
                p.connected = modLive->GetState() == BedrockSession::State::InGame;
                p.gameMode = static_cast<abi::GameMode>(modLive->GameType());
                p.dimension = static_cast<abi::Dimension>(modLive->DimensionId());
                p.permission = static_cast<abi::PermissionLevel>(modLive->OwnPermission());
                p.walkSpeed = modLive->WalkSpeed();
                p.health = v.health;
                p.maxHealth = v.maxHealth;
                p.absorption = v.absorption;
                p.hunger = v.hunger;
                p.saturation = v.saturation;
                p.xpProgress = v.xpProgress;
                p.xpLevel = v.level;
                p.air = v.air;
                p.maxAir = v.maxAir;
                p.onFire = v.onFire;
            } else {
                p.name = options.playerName;
                p.health = hudState.health;
            }
            w.write(p);
            return Status::Ok;
        });

        mm.Register(Op::PlayerEffects, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!modLive)
                return Status::Unavailable;
            const auto list = modLive->GetEffects();
            w.put<uint32_t>(static_cast<uint32_t>(list.size()));
            for (const auto& e : list) {
                abi::Effect out;
                out.id = static_cast<abi::EffectId>(e.id);
                out.amplifier = e.amplifier;
                out.ticks = e.ticks;
                out.ambient = e.ambient;
                out.particles = e.particles;
                w.write(out);
            }
            return Status::Ok;
        });

        mm.Register(Op::PlayerLook, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            w.put(camera.yaw - 90.0f).put(-camera.pitch);
            return Status::Ok;
        });

        mm.Register(Op::Inventory, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::Inventory inv;
            if (!modLive) {
                for (int i = 0; i < 9; i++)
                    inv.slots[static_cast<size_t>(i)] = ToItem(hudState.hotbar[i]);
                inv.offhand = ToItem(hudState.offhand);
                inv.selected = hudState.selectedSlot;
                w.write(inv);
                return Status::Ok;
            }
            const BedrockSession::InventoryView view = modLive->GetInventory();
            for (int i = 0; i < 9; i++)
                inv.slots[static_cast<size_t>(i)] = ToItem(view.hotbar[i]);
            for (int i = 0; i < 27; i++)
                inv.slots[static_cast<size_t>(9 + i)] = ToItem(view.main[i]);
            for (int i = 0; i < 4; i++)
                inv.armor[static_cast<size_t>(i)] = ToItem(view.armor[i]);
            inv.offhand = ToItem(view.offhand);
            inv.cursor = ToItem(view.cursor);
            inv.selected = view.selected;
            inv.open = modLive->InventoryIsOpen();
            w.write(inv);
            return Status::Ok;
        });

        mm.Register(Op::InventorySelect, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t slot = r.get<int32_t>();
            if (!r.Ok() || slot < 0 || slot > 8)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            hudState.selectedSlot = slot;
            modLive->SelectHotbarSlot(slot);
            return Status::Ok;
        });

        mm.Register(Op::InventoryDrop, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t count = r.get<int32_t>();
            if (!r.Ok() || count < 1 || count > 64)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            modLive->SendDropSelected(count);
            return Status::Ok;
        });

        mm.Register(Op::InventorySwap, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t a = r.get<int32_t>(), b = r.get<int32_t>();
            if (!r.Ok() || a < 0 || b < 0 || a >= BedrockSession::kInvSlots || b >= BedrockSession::kInvSlots)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            modLive->SwapInventorySlots(a, b);
            return Status::Ok;
        });

        mm.Register(Op::InventoryClick, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t slot = r.get<int32_t>();
            const bool right = r.boolean();
            const bool shift = r.boolean();
            if (!r.Ok() || slot < 0 || slot >= BedrockSession::kInvSlots)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            if (shift)
                modLive->InvShiftClick(slot);
            else
                modLive->InvClick(slot, right);
            return Status::Ok;
        });

        mm.Register(Op::InventoryOpen, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!modLive)
                return Status::Unavailable;
            return modLive->OpenInventory() ? Status::Ok : Status::Failed;
        });

        mm.Register(Op::InventoryClose, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!modLive)
                return Status::Unavailable;
            return modLive->CloseInventory() ? Status::Ok : Status::Failed;
        });
    }

    void Perlin::SetupModWorldApi() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using mods::CallContext;
        mm.Register(Op::WorldState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::WorldState s;
            s.rain = rainLevel;
            s.thunder = thunderLevel;
            if (modLive) {
                s.dimension = static_cast<abi::Dimension>(modLive->DimensionId());
                const auto world = modLive->World();
                s.difficulty = static_cast<abi::Difficulty>(world.difficulty);
                s.worldGameMode = static_cast<abi::GameMode>(world.worldGameMode);
                s.commandsEnabled = world.commandsEnabled;
                for (const auto& rule : world.rules)
                    s.rules.emplace_back(rule.first, rule.second);
                int64_t day = 0;
                int tod = 0;
                if (modLive->GetWorldTime(day, tod)) {
                    s.day = day;
                    s.timeOfDay = tod;
                }
                double ticks = 0;
                if (modLive->GetWorldTicks(ticks))
                    s.totalTicks = ticks;
                int spawn[3];
                if (modLive->HaveWorldSpawn()) {
                    modLive->GetWorldSpawn(spawn);
                    s.haveSpawn = true;
                    s.spawnX = spawn[0];
                    s.spawnY = spawn[1];
                    s.spawnZ = spawn[2];
                }
            }
            w.write(s);
            return Status::Ok;
        });

        mm.Register(Op::Block, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), z = r.get<int32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLevel)
                return Status::Unavailable;
            abi::BlockInfo info;
            bool loaded = false;
            const uint16_t id = modLevel->GetBlock(x, y, z, loaded);
            info.loaded = loaded;
            if (loaded) {
                BlockPalette& palette = BlockPalette::Instance();
                info.runtimeId = static_cast<uint32_t>(palette.RuntimeId(id));
                info.name = palette.Name(id);
                for (const auto& s : palette.States(id))
                    info.states.emplace_back(s.first, StateText(s.second));
                info.biome = modLevel->GetBiome(x, y, z);
                info.liquid = modLevel->GetLiquid(x, y, z);
            }
            w.write(info);
            return Status::Ok;
        });

        mm.Register(Op::BlockProps, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            std::string name = r.str();
            if (!r.Ok() || name.empty() || name.size() > 128)
                return Status::BadArguments;
            if (!name.contains(':'))
                name = "minecraft:" + name;
            abi::BlockProps out;
            const destroy::BlockInfo info = destroy::Lookup(name);
            out.known = info.known;
            out.hardness = info.hardness;
            out.tool = static_cast<abi::Tool>(std::clamp(info.tool, 0, 6));
            out.harvestLevel = info.harvestLevel;
            out.needsSupportBelow = destroy::NeedsSupportBelow(name);
            out.breaksWholeStack = destroy::BreaksWholeStack(name);
            out.displayName = ui::UiAssets::Instance().ItemName(name);
            out.properties = BlockPalette::Instance().BlockProperties(name);
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::BlockEntityData, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), z = r.get<int32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            abi::BlockEntityInfo out;
            BlockEntity be;
            if (modLive->BlockEntities().Get(x, y, z, be)) {
                out.present = true;
                out.id = be.id;
                out.color = be.color;
                out.waxed = be.waxed;
                out.rotation = be.rotation;
                out.frontText = be.front.text;
                out.backText = be.back.text;
                out.frontGlow = be.front.glow;
                out.backGlow = be.back.glow;
                out.base = be.base;
                out.bannerType = be.bannerType;
                for (const auto& [code, colour] : be.patterns)
                    out.patterns.emplace_back(code, colour);
                out.nbt = be.nbt;
            }
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::DisplayName, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            const auto kind = static_cast<abi::NameKind>(r.get<uint8_t>());
            const std::string id = r.str();
            const int32_t aux = r.get<int32_t>();
            if (!r.Ok() || id.empty() || id.size() > 128)
                return Status::BadArguments;
            std::string text;
            if (kind == abi::NameKind::Entity) {
                std::string shortName = id;
                if (const size_t colon = shortName.find(':'); colon != std::string::npos)
                    shortName = shortName.substr(colon + 1);
                if (!i18n::Lang::Get().Find("entity." + shortName + ".name", text) &&
                    !i18n::Lang::Get().Find("entity." + id + ".name", text))
                    text = id;
            } else {
                text = ui::UiAssets::Instance().ItemName(id, aux);
            }
            w.raw(text.data(), text.size());
            return Status::Ok;
        });

        mm.Register(Op::BreakProgress, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            const LocalBreakState state = LocalBreak();
            abi::BreakState out;
            out.active = state.active;
            out.x = state.x;
            out.y = state.y;
            out.z = state.z;
            out.progress = state.progress;
            w.write(out);
            return Status::Ok;
        });

        mm.imageLoader = [](const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba) {
            Texture t;
            if (!ui::UiAssets::Instance().Image(path, t) || t.width <= 0 || t.height <= 0)
                return false;
            width = t.width;
            height = t.height;
            rgba.assign(t.pixels.begin(), t.pixels.end());
            return true;
        };

        mm.Register(Op::MapChunk, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const int32_t cx = r.get<int32_t>(), cz = r.get<int32_t>(), caveY = r.get<int32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLevel || !levelRenderer)
                return Status::Unavailable;
            static LodStore colours;
            static const void* atlasSeen = nullptr;
            static size_t atlasSize = 0;
            static std::vector<uint8_t> traits;
            enum : uint8_t { known = 1, water = 2, lava = 4, snow = 8, leaves = 16, cube = 32 };
            const BlockLibrary& blocks = levelRenderer->Blocks();
            const BiomeTints& tints = levelRenderer->Tints();
            if (blocks.AtlasPixels().data() != atlasSeen || blocks.AtlasPixels().size() != atlasSize) {
                atlasSeen = blocks.AtlasPixels().data();
                atlasSize = blocks.AtlasPixels().size();
                colours.InvalidateColours();
                traits.clear();
            }
            const auto traitsOf = [&](uint16_t id) -> uint8_t {
                if (id >= traits.size())
                    traits.resize(static_cast<size_t>(id) + 1, 0);
                uint8_t& t = traits[id];
                if (!(t & known)) {
                    const std::string& name = BlockPalette::Instance().Name(id);
                    t = known;
                    if (name.contains("water"))
                        t |= water;
                    if (name.contains("lava"))
                        t |= lava;
                    if (name.contains("snow_layer"))
                        t |= snow;
                    if (name.contains("leaves"))
                        t |= leaves;
                    if (id != 0 && blocks.Look(id).shape == BlockAppearance::Shape::Cube)
                        t |= cube;
                }
                return t;
            };
            std::unique_lock<std::mutex> lock(modLevel->mutex, std::try_to_lock);
            if (!lock.owns_lock())
                return Status::Unavailable;
            const auto it = modLevel->chunks.find({cx, cz});
            int lo = 0, hi = 0;
            if (it == modLevel->chunks.end() || !it->second.YRange(lo, hi)) {
                w.boolean(false).put<uint64_t>(0);
                return Status::Ok;
            }
            const Chunk& chunk = it->second;
            w.boolean(true).put<uint64_t>(chunk.version * 31 + static_cast<uint64_t>(caveY == INT32_MIN ? 0 : caveY + 1000));
            const auto rgb = [](const LodStore::Rgb& c) {
                return static_cast<uint32_t>(c.r) << 16 | static_cast<uint32_t>(c.g) << 8 | c.b;
            };
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    int16_t top = static_cast<int16_t>(lo - 1);
                    for (int ty = hi; ty >= lo; ty--) {
                        const uint16_t id = chunk.Get(x, ty, z);
                        if (id == 0)
                            continue;
                        const uint8_t t = traitsOf(id);
                        if ((t & cube) && !(t & leaves) && !(t & water)) {
                            top = static_cast<int16_t>(ty);
                            break;
                        }
                    }
                    int y = hi;
                    int bottom = lo;
                    bool rock = false;
                    if (caveY != INT32_MIN) {
                        y = std::min(hi, caveY);
                        int ceiling = 0;
                        while (y >= lo && chunk.Get(x, y, z) != 0 && chunk.GetLiquid(x, y, z) == 0 && ceiling < 4) {
                            y--;
                            ceiling++;
                        }
                        rock = ceiling >= 4;
                        bottom = std::max(lo, y - 64);
                    }
                    int waterTop = Chunk::MIN_Y - 1;
                    uint16_t waterId = 0, floorId = 0;
                    int floorY = lo - 1;
                    uint8_t flags = 1;
                    if (rock) {
                        w.put<uint32_t>(0x1A1A1Eu).put<int16_t>(static_cast<int16_t>(caveY)).put<int16_t>(top);
                        w.put<uint8_t>(0).put<uint8_t>(static_cast<uint8_t>(1 | kMapRockFlag));
                        continue;
                    }
                    for (; y >= bottom; y--) {
                        const uint16_t id = chunk.Get(x, y, z);
                        const uint16_t liquid = chunk.GetLiquid(x, y, z);
                        if (id == 0 && liquid == 0)
                            continue;
                        const bool isWater = liquid != 0 || (traitsOf(id) & water);
                        if (isWater) {
                            if (waterTop < Chunk::MIN_Y) {
                                waterTop = y;
                                waterId = liquid ? liquid : id;
                            }
                            if (!liquid || id == 0)
                                continue;
                        }
                        if (id == 0)
                            continue;
                        const uint8_t t = traitsOf(id);
                        if (t & lava)
                            flags |= kMapLavaFlag;
                        if (t & snow)
                            flags |= kMapSnowFlag;
                        else if (!(t & cube) && !(t & lava))
                            continue;
                        if (t & leaves)
                            flags |= kMapLeavesFlag;
                        floorY = y;
                        floorId = id;
                        break;
                    }
                    const int biome = chunk.GetBiome(x, std::max(floorY, Chunk::MIN_Y), z);
                    uint32_t colour = 0;
                    int16_t height = static_cast<int16_t>(floorY + 1);
                    uint8_t depth = 0;
                    if (waterTop >= Chunk::MIN_Y) {
                        flags |= kMapWaterFlag;
                        const LodStore::Rgb wc = colours.ColourOf(waterId, biome, true, blocks, tints);
                        const LodStore::Rgb fc = floorId ? colours.ColourOf(floorId, biome, true, blocks, tints) : wc;
                        const int d = floorId ? std::max(1, waterTop - floorY) : 32;
                        depth = static_cast<uint8_t>(std::min(d, 255));
                        const float a = std::min(0.92f, 0.55f + d * 0.05f);
                        const LodStore::Rgb mix{static_cast<uint8_t>(wc.r * a + fc.r * (1 - a)),
                                                static_cast<uint8_t>(wc.g * a + fc.g * (1 - a)),
                                                static_cast<uint8_t>(wc.b * a + fc.b * (1 - a))};
                        colour = rgb(mix);
                        height = static_cast<int16_t>(waterTop + 1);
                    } else if (floorId) {
                        colour = rgb(colours.ColourOf(floorId, biome, true, blocks, tints));
                    } else {
                        flags = 0;
                    }
                    w.put<uint32_t>(colour).put<int16_t>(height).put<int16_t>(top).put<uint8_t>(depth).put<uint8_t>(flags);
                }
            return Status::Ok;
        });

        mm.Register(Op::ChunkLoaded, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const int32_t cx = r.get<int32_t>(), cz = r.get<int32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLevel)
                return Status::Unavailable;
            w.boolean(modLevel->HasChunk(cx, cz));
            return Status::Ok;
        });

        mm.Register(Op::Region, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            int32_t a[6];
            for (int32_t& v : a)
                v = r.get<int32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLevel)
                return Status::Unavailable;
            abi::Region region;
            region.x0 = std::min(a[0], a[3]);
            region.y0 = std::min(a[1], a[4]);
            region.z0 = std::min(a[2], a[5]);
            region.sx = std::abs(a[3] - a[0]) + 1;
            region.sy = std::abs(a[4] - a[1]) + 1;
            region.sz = std::abs(a[5] - a[2]) + 1;
            if (static_cast<int64_t>(region.sx) * region.sy * region.sz > 262144)
                return Status::BadArguments;
            NameCache names;
            std::unordered_map<std::string, uint16_t> index;
            region.palette.emplace_back();
            index[std::string()] = 0;
            region.cells.reserve(static_cast<size_t>(region.sx) * static_cast<size_t>(region.sy) *
                                 static_cast<size_t>(region.sz));
            for (int y = 0; y < region.sy; y++)
                for (int z = 0; z < region.sz; z++)
                    for (int x = 0; x < region.sx; x++) {
                        bool loaded = false;
                        const uint16_t id =
                            modLevel->GetBlock(region.x0 + x, region.y0 + y, region.z0 + z, loaded);
                        const std::string& name = loaded ? names.Get(id) : region.palette[0];
                        auto it = index.find(name);
                        if (it == index.end()) {
                            if (region.palette.size() >= 65535)
                                return Status::Failed;
                            it = index.emplace(name, static_cast<uint16_t>(region.palette.size())).first;
                            region.palette.push_back(name);
                        }
                        region.cells.push_back(it->second);
                    }
            w.write(region);
            return Status::Ok;
        });

        mm.Register(Op::Raycast, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            double v[6];
            for (double& d : v)
                d = r.get<double>();
            const double maxDistance = r.get<double>();
            const bool liquids = r.boolean();
            if (!r.Ok() || !Finite(v[0]) || !Finite(v[1]) || !Finite(v[2]) || !Finite(v[3]) ||
                !Finite(v[4]) || !Finite(v[5]) || !Finite(maxDistance))
                return Status::BadArguments;
            if (!modLevel || !levelRenderer)
                return Status::Unavailable;
            glm::dvec3 dir(v[3], v[4], v[5]);
            if (glm::length(dir) < 1.0e-9)
                return Status::BadArguments;
            dir = glm::normalize(dir);
            const glm::dvec3 origin(v[0], v[1], v[2]);
            const double reach = std::clamp(maxDistance, 0.0, 256.0);
            const AimHit hit = Raycast(
                *modLevel, &levelRenderer->Blocks(), {},
                [](const entity::Entity&) { return glm::vec2(0.6f, 1.8f); }, origin, dir, reach);
            abi::RayHit out;
            double blockDistance = reach;
            if (hit.kind == AimHit::Kind::Block) {
                out.hit = true;
                out.x = hit.bx;
                out.y = hit.by;
                out.z = hit.bz;
                out.face = static_cast<abi::Face>(hit.face);
                out.hx = hit.point.x;
                out.hy = hit.point.y;
                out.hz = hit.point.z;
                out.distance = hit.distance;
                bool loaded = false;
                out.block = BlockPalette::Instance().Name(modLevel->GetBlock(hit.bx, hit.by, hit.bz, loaded));
                blockDistance = hit.distance;
            }
            if (liquids) {
                glm::ivec3 previous(static_cast<int>(std::floor(origin.x)),
                                    static_cast<int>(std::floor(origin.y)),
                                    static_cast<int>(std::floor(origin.z)));
                for (double t = 0.0; t < blockDistance; t += 0.05) {
                    const glm::dvec3 p = origin + dir * t;
                    const glm::ivec3 cell(static_cast<int>(std::floor(p.x)),
                                          static_cast<int>(std::floor(p.y)),
                                          static_cast<int>(std::floor(p.z)));
                    if (modLevel->GetLiquid(cell.x, cell.y, cell.z) != 0) {
                        out = abi::RayHit{};
                        out.hit = true;
                        out.x = cell.x;
                        out.y = cell.y;
                        out.z = cell.z;
                        const glm::ivec3 d = previous - cell;
                        out.face = d.y < 0   ? abi::Face::Down
                                   : d.y > 0 ? abi::Face::Up
                                   : d.z < 0 ? abi::Face::North
                                   : d.z > 0 ? abi::Face::South
                                   : d.x < 0 ? abi::Face::West
                                             : abi::Face::East;
                        out.hx = p.x;
                        out.hy = p.y;
                        out.hz = p.z;
                        out.distance = t;
                        out.block = "liquid";
                        break;
                    }
                    previous = cell;
                }
            }
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::Aim, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::AimState a;
            if (modLevel && levelRenderer) {
                const std::vector<entity::Entity> entities =
                    modLive ? modLive->Entities().Snapshot() : previewEntities.Snapshot();
                const bool creative = hudState.mode == ui::GameMode::Creative;
                const AimHit hit = Raycast(
                    *modLevel, &levelRenderer->Blocks(), entities,
                    [&](const entity::Entity& e) {
                        const auto hb = entityRenderer.HitBox(e);
                        return glm::vec2(hb.first, hb.second);
                    },
                    {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), 5.7,
                    creative ? 7.0 : 3.0);
                a.distance = hit.distance;
                if (hit.kind == AimHit::Kind::Block) {
                    a.kind = abi::AimKind::Block;
                    a.block.hit = true;
                    a.block.x = hit.bx;
                    a.block.y = hit.by;
                    a.block.z = hit.bz;
                    a.block.face = static_cast<abi::Face>(hit.face);
                    a.block.hx = hit.point.x;
                    a.block.hy = hit.point.y;
                    a.block.hz = hit.point.z;
                    a.block.distance = hit.distance;
                    bool loaded = false;
                    a.block.block =
                        BlockPalette::Instance().Name(modLevel->GetBlock(hit.bx, hit.by, hit.bz, loaded));
                } else if (hit.kind == AimHit::Kind::Entity) {
                    a.kind = abi::AimKind::Entity;
                    a.entity = hit.entity;
                    for (const entity::Entity& e : entities)
                        if (e.runtimeId == hit.entity)
                            a.entityType = e.type;
                }
            }
            w.write(a);
            return Status::Ok;
        });

        mm.Register(Op::FindBlocks, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const int32_t cx = r.get<int32_t>(), cy = r.get<int32_t>(), cz = r.get<int32_t>();
            const int32_t radius = std::clamp(r.get<int32_t>(), 1, 32);
            const int32_t limit = std::clamp(r.get<int32_t>(), 1, 4096);
            const bool exact = r.boolean();
            const std::string name = r.str();
            if (!r.Ok() || name.empty())
                return Status::BadArguments;
            if (!modLevel)
                return Status::Unavailable;
            NameCache names;
            std::unordered_map<uint16_t, bool> matches;
            std::vector<abi::FoundBlock> found;
            for (int y = cy - radius; y <= cy + radius; y++)
                for (int z = cz - radius; z <= cz + radius; z++)
                    for (int x = cx - radius; x <= cx + radius; x++) {
                        bool loaded = false;
                        const uint16_t id = modLevel->GetBlock(x, y, z, loaded);
                        if (!loaded)
                            continue;
                        auto it = matches.find(id);
                        if (it == matches.end())
                            it = matches.emplace(id, MatchName(names.Get(id), name, exact)).first;
                        if (it->second) {
                            abi::FoundBlock b;
                            b.x = x;
                            b.y = y;
                            b.z = z;
                            b.name = names.Get(id);
                            found.push_back(std::move(b));
                        }
                    }
            const auto dist = [&](const abi::FoundBlock& b) {
                const double dx = b.x - cx, dy = b.y - cy, dz = b.z - cz;
                return dx * dx + dy * dy + dz * dz;
            };
            std::sort(found.begin(), found.end(),
                      [&](const abi::FoundBlock& a, const abi::FoundBlock& b) { return dist(a) < dist(b); });
            if (found.size() > static_cast<size_t>(limit))
                found.resize(static_cast<size_t>(limit));
            w.put<uint32_t>(static_cast<uint32_t>(found.size()));
            for (const auto& b : found)
                w.write(b);
            return Status::Ok;
        });

        mm.Register(Op::BlockNames, [](CallContext&, abi::Reader&, abi::Writer& w) {
            const std::vector<std::string> list = BlockPalette::Instance().AllNames();
            w.put<uint32_t>(static_cast<uint32_t>(list.size()));
            for (const auto& n : list)
                w.str(n);
            return Status::Ok;
        });

        mm.Register(Op::Entities, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const double radius = r.get<double>();
            const std::string type = r.str();
            const bool playersOnly = r.boolean();
            const bool includeItems = r.boolean();
            if (!r.Ok())
                return Status::BadArguments;
            const std::vector<entity::Entity> list =
                modLive ? modLive->Entities().Snapshot() : previewEntities.Snapshot();
            const physics::State& st = player.State();
            const double rx = modLive ? st.x : camera.x, ry = modLive ? st.y : camera.y - 1.62,
                         rz = modLive ? st.z : camera.z;
            std::vector<const entity::Entity*> picked;
            for (const entity::Entity& e : list) {
                if (e.isSelf)
                    continue;
                if (playersOnly && !e.isPlayer)
                    continue;
                if (!includeItems && e.isItem)
                    continue;
                if (!type.empty() && !e.type.contains(type))
                    continue;
                if (radius > 0) {
                    const double dx = e.pos.x - rx, dy = e.pos.y - ry, dz = e.pos.z - rz;
                    if (dx * dx + dy * dy + dz * dz > radius * radius)
                        continue;
                }
                picked.push_back(&e);
                if (picked.size() >= 2048)
                    break;
            }
            w.put<uint32_t>(static_cast<uint32_t>(picked.size()));
            for (const entity::Entity* e : picked)
                w.write(ToEntity(*e));
            return Status::Ok;
        });

        mm.Register(Op::EntityById, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const uint64_t id = r.get<uint64_t>();
            if (!r.Ok())
                return Status::BadArguments;
            entity::Entity e;
            const bool ok = modLive ? modLive->Entities().Find(id, e) : previewEntities.Find(id, e);
            if (!ok)
                return Status::NotFound;
            w.write(ToEntity(e));
            return Status::Ok;
        });

        mm.Register(Op::PlayerList, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!modLive)
                return Status::Unavailable;
            const auto list = modLive->PlayerList();
            w.put<uint32_t>(static_cast<uint32_t>(list.size()));
            for (const auto& p : list) {
                abi::PlayerEntry e;
                e.uuid = p.uuid;
                e.name = p.name;
                e.uid = p.uid;
                e.permission = static_cast<abi::PermissionLevel>(p.perm);
                w.write(e);
            }
            return Status::Ok;
        });
    }

    void Perlin::SetupModClientApi() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using mods::CallContext;
        const auto optionTable = [this]() {
            std::vector<OptionAccess> t;
            const auto boolean = [&](const char* key, bool& ref) {
                t.push_back({key, [&ref] { return std::string(ref ? "true" : "false"); },
                             [&ref](const std::string& v) {
                                 if (v != "true" && v != "false")
                                     return false;
                                 ref = v == "true";
                                 return true;
                             }});
            };
            const auto integer = [&](const char* key, int& ref, int lo, int hi) {
                t.push_back({key, [&ref] { return std::to_string(ref); },
                             [&ref, lo, hi](const std::string& v) {
                                 char* end = nullptr;
                                 const long n = std::strtol(v.c_str(), &end, 10);
                                 if (!end || *end || v.empty())
                                     return false;
                                 ref = static_cast<int>(std::clamp<long>(n, lo, hi));
                                 return true;
                             }});
            };
            const auto real = [&](const char* key, float& ref, float lo, float hi) {
                t.push_back({key, [&ref] { return std::to_string(ref); },
                             [&ref, lo, hi](const std::string& v) {
                                 char* end = nullptr;
                                 const float n = std::strtof(v.c_str(), &end);
                                 if (!end || *end || v.empty() || !std::isfinite(n))
                                     return false;
                                 ref = std::clamp(n, lo, hi);
                                 return true;
                             }});
            };
            const auto choice = [&](const char* key, std::string& ref, std::vector<std::string> allowed) {
                t.push_back({key, [&ref] { return ref; },
                             [&ref, allowed](const std::string& v) {
                                 if (std::find(allowed.begin(), allowed.end(), v) == allowed.end())
                                     return false;
                                 ref = v;
                                 return true;
                             }});
            };
            integer("maxFps", options.maxFps, 0, 260);
            boolean("vsync", options.vsync);
            integer("renderDistance", options.renderDistance, 2, 256);
            real("fov", options.fov, 30.0f, 110.0f);
            real("gamma", options.gamma, 0.0f, 1.0f);
            real("mouseSensitivity", options.mouseSensitivity, 0.0f, 1.0f);
            boolean("invertMouse", options.invertMouse);
            boolean("fancyGraphics", options.fancyGraphics);
            integer("cloudRange", options.cloudRange, 0, 512);
            integer("guiScale", options.guiScale, 0, 6);
            boolean("shadows", options.shadows);
            boolean("fovEffects", options.fovEffects);
            real("motionBlur", options.motionBlur, 0.0f, 1.0f);
            integer("damageTilt", options.damageTilt, 0, 100);
            boolean("damageTint", options.damageTint);
            boolean("thirdPersonCrosshair", options.thirdPersonCrosshair);
            boolean("thirdPersonNametag", options.thirdPersonNametag);
            integer("menuBlur", options.menuBlur, 0, 10);
            boolean("betterGrass", options.betterGrass);
            boolean("showHud", options.showHud);
            choice("glass", options.glass, {"normal", "connected", "connectedClear", "clear"});
            choice("dynamicLights", options.dynamicLights, {"off", "fast", "fancy"});
            choice("sprintMode", options.sprintMode, {"hold", "toggle"});
            choice("sneakMode", options.sneakMode, {"hold", "toggle"});
            choice("capePhysics", options.capePhysics, {"off", "noshoulder", "shoulder"});
            t.push_back({"renderer", [this] { return options.renderer; }, nullptr});
            t.push_back({"language", [this] { return options.language; },
                         [this](const std::string& v) {
                             if (v.empty() || v.size() > 16)
                                 return false;
                             options.language = v;
                             return true;
                         }});
            t.push_back({"playerName", [this] { return options.playerName; }, nullptr});
            t.push_back({"server", [this] { return options.server; }, nullptr});
            t.push_back({"texturePack", [this] { return options.texturePack; }, nullptr});
            t.push_back({"shaderPack", [this] { return options.shaderPack; }, nullptr});
            for (const ShaderSetting& st : ShaderPack::Instance().Settings())
                t.push_back({"shader." + st.id,
                             [this, st] {
                                 const auto found = options.shaderSettings.find(st.id);
                                 const double v =
                                     found == options.shaderSettings.end() ? st.def : found->second;
                                 return std::to_string(v);
                             },
                             [this, st](const std::string& v) {
                                 char* end = nullptr;
                                 const double d = std::strtod(v.c_str(), &end);
                                 if (end == v.c_str() || !std::isfinite(d))
                                     return false;
                                 options.shaderSettings[st.id] = std::clamp(d, st.min, st.max);
                                 return true;
                             }});
            return t;
        };

        mm.Register(Op::Options, [this, optionTable](CallContext&, abi::Reader&, abi::Writer& w) {
            ui::Json json = ui::Json::MakeObject();
            for (const OptionAccess& o : optionTable())
                json.Set(o.key, ui::Json(o.get()));
            const std::string text = json.Dump();
            w.raw(text.data(), text.size());
            return Status::Ok;
        });

        mm.Register(Op::OptionGet, [optionTable](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string key = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            for (const OptionAccess& o : optionTable())
                if (o.key == key) {
                    const std::string v = o.get();
                    w.raw(v.data(), v.size());
                    return Status::Ok;
                }
            return Status::NotFound;
        });

        mm.Register(Op::OptionSet, [this, optionTable](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string key = r.str();
            const std::string value = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            for (const OptionAccess& o : optionTable())
                if (o.key == key) {
                    if (!o.set)
                        return Status::Denied;
                    if (!o.set(value))
                        return Status::BadArguments;
                    options.Save();
                    return Status::Ok;
                }
            return Status::NotFound;
        });

        mm.Register(Op::Screen, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::ScreenState s;
            s.guiWidth = modGuiW;
            s.guiHeight = modGuiH;
            s.guiScale = modHud ? modHud->Scale() : 1;
            int fw = 0, fh = 0;
            if (renderer)
                renderer->FramebufferSize(fw, fh);
            s.framebufferWidth = fw;
            s.framebufferHeight = fh;
            s.fps = static_cast<float>(debug.fps);
            s.frameMs = debug.fps > 0 ? 1000.0f / static_cast<float>(debug.fps) : 0.0f;
            s.screen = CurrentScreen();
            s.paused = pause.shown;
            s.connected = modLive && modLive->GetState() == BedrockSession::State::InGame;
            s.hudHidden = !options.showHud;
            s.debug = debug.on;
            s.server = options.server;
            s.language = options.language;
            s.platform = PlatformName();
            s.gameVersion = kBedrockVersion;
            w.write(s);
            return Status::Ok;
        });

        mm.Register(Op::Translate, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string key = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            const std::string text = i18n::Tr(key);
            w.raw(text.data(), text.size());
            return Status::Ok;
        });

        mm.Register(Op::ReloadResources, [this](CallContext&, abi::Reader&, abi::Writer&) {
            packUi.foundAt = {};
            hotTexStamp = "reload";
            return Status::Ok;
        });

        mm.Register(Op::InputActions, [](CallContext&, abi::Reader&, abi::Writer& w) {
            w.put<uint32_t>(static_cast<uint32_t>(kActionCount));
            for (size_t i = 0; i < kActionCount; i++)
                w.str(ActionId(static_cast<Action>(i)));
            return Status::Ok;
        });

        mm.Register(Op::InputDown, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string name = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            for (size_t i = 0; i < kActionCount; i++)
                if (name == ActionId(static_cast<Action>(i))) {
                    w.boolean(inputManager.IsDown(static_cast<Action>(i)));
                    return Status::Ok;
                }
            const Binding b = Binding::Parse(name);
            if (b.device == Binding::Device::Keyboard) {
                w.boolean(input.IsKeyDown(b.code) && !input.TextMode());
                return Status::Ok;
            }
            if (b.device == Binding::Device::Mouse) {
                w.boolean(input.IsMouseDown(b.code));
                return Status::Ok;
            }
            return Status::NotFound;
        });

        mm.Register(Op::PerspectiveGet, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            w.write(static_cast<abi::Perspective>(camera.mode));
            return Status::Ok;
        });

        mm.Register(Op::PerspectiveSet, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const auto mode = r.read<abi::Perspective>();
            if (!r.Ok() || static_cast<int>(mode) < 0 || static_cast<int>(mode) > 2)
                return Status::BadArguments;
            camera.mode = static_cast<PerspectiveMode>(static_cast<int>(mode));
            return Status::Ok;
        });

        mm.Register(Op::ChatHistory, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const uint32_t count = std::min<uint32_t>(r.get<uint32_t>(), 100);
            std::vector<abi::ChatLine> out;
            for (std::string& line : chat.RecentLines(count ? count : 20)) {
                abi::ChatLine l;
                l.text = std::move(line);
                out.push_back(std::move(l));
            }
            w.write(out);
            return Status::Ok;
        });

        mm.Register(Op::TitleState, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::TitleStateData t;
            t.title = hudState.title;
            t.subtitle = hudState.subtitle;
            t.actionBar = hudState.actionBar;
            t.titleAlpha = hudState.titleAlpha;
            t.actionBarAlpha = hudState.actionBarAlpha;
            w.write(t);
            return Status::Ok;
        });

        mm.Register(Op::ResourcePacks, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            std::vector<abi::PackEntry> out;
            out.reserve(options.texturePacks.size());
            for (const std::string& name : options.texturePacks)
                out.push_back({name, false});
            for (const std::string& dir : ServerPacks::Instance().Directories())
                out.push_back({std::filesystem::path(dir).filename().string(), true});
            w.write(out);
            return Status::Ok;
        });
    }

    void Perlin::SetupModInteractionApi() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using mods::CallContext;
        const auto ModEntityClick = [this](const entity::Entity& e) {
            const auto hb = entityRenderer.HitBox(e);
            const double scale = e.scale > 0 ? e.scale : 1.0;
            const double halfWidth = hb.first * 0.5 * scale, height = hb.second * scale;
            const glm::dvec3 centre(e.pos.x, e.pos.y + height * 0.5, e.pos.z);
            return EntityClickOffset({camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), e.pos,
                                     halfWidth, height, centre);
        };

        mm.Register(Op::MousePosition, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            const float scale = static_cast<float>(modHud ? std::max(1, modHud->Scale()) : 1);
            w.put(input.MouseX() / scale).put(input.MouseY() / scale).boolean(input.Captured());
            return Status::Ok;
        });

        mm.Register(Op::GfxDisplay, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            abi::GfxDisplayInfo d;
            d.width = modFbW.load();
            d.height = modFbH.load();
            d.guiScale = modHud ? std::max(1, modHud->Scale()) : 1;
            d.mouseX = input.MouseX();
            d.mouseY = input.MouseY();
            d.mouseFree = !input.Captured();
            d.inGame = modLive != nullptr;
            w.write(d);
            return Status::Ok;
        });

        mm.Register(Op::NativeInfo, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            std::lock_guard lock(modNativeMutex);
            if (!modNativeReady)
                return Status::Unavailable;
            w.write(modNative);
            return Status::Ok;
        });

        mm.Register(Op::InputOverride, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const abi::InputOverrideState s = r.read<abi::InputOverrideState>();
            if (!r.Ok())
                return Status::BadArguments;
            const auto until = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(std::clamp(s.ticks, 0, 200) * 50);
            const auto epoch = std::chrono::steady_clock::time_point{};
            inputManager.Hold(Action::Forward, s.forward > 0.3f ? until : epoch);
            inputManager.Hold(Action::Back, s.forward < -0.3f ? until : epoch);
            inputManager.Hold(Action::Right, s.strafe > 0.3f ? until : epoch);
            inputManager.Hold(Action::Left, s.strafe < -0.3f ? until : epoch);
            inputManager.Hold(Action::Jump, s.jump ? until : epoch);
            inputManager.Hold(Action::Sneak, s.sneak ? until : epoch);
            inputManager.Hold(Action::Sprint, s.sprint ? until : epoch);
            inputManager.Hold(Action::Attack, s.attack ? until : epoch);
            inputManager.Hold(Action::Use, s.use ? until : epoch);
            return Status::Ok;
        });

        mm.Register(Op::Jump, [this](CallContext&, abi::Reader&, abi::Writer&) {
            inputManager.Hold(Action::Jump,
                              std::chrono::steady_clock::now() + std::chrono::milliseconds(120));
            return Status::Ok;
        });

        mm.Register(Op::Chat, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            std::string text = r.str();
            if (!r.Ok() || text.empty() || text.size() > 512)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            if (text[0] == '/')
                modLive->SendCommand(text.substr(1));
            else
                modLive->SendChat(text);
            return Status::Ok;
        });

        mm.Register(Op::Command, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            std::string text = r.str();
            if (!r.Ok() || text.empty() || text.size() > 512)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            modLive->SendCommand(text[0] == '/' ? text.substr(1) : text);
            return Status::Ok;
        });

        mm.Register(Op::LocalMessage, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string text = r.str();
            if (!r.Ok() || text.size() > 2000)
                return Status::BadArguments;
            chat.AddMessage(text, hudAnim.ticks);
            return Status::Ok;
        });

        mm.Register(Op::ServerCommands, [this](CallContext&, abi::Reader&, abi::Writer& w) {
            if (!modLive)
                return Status::Unavailable;
            const auto list = modLive->Commands();
            w.put<uint32_t>(static_cast<uint32_t>(list.size()));
            for (const auto& c : list)
                w.str(c.name);
            return Status::Ok;
        });

        mm.Register(Op::Toast, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string text = r.str();
            const int32_t ms = r.get<int32_t>();
            if (!r.Ok() || text.size() > 256)
                return Status::BadArguments;
            modToastText = text;
            modToastUntil =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(std::clamp(ms, 100, 30000));
            return Status::Ok;
        });

        mm.Register(Op::Title, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            modTitleText = r.str();
            modSubtitleText = r.str();
            modTitleFadeIn = std::clamp(r.get<int32_t>(), 0, 10000);
            modTitleStay = std::clamp(r.get<int32_t>(), 0, 30000);
            modTitleFadeOut = std::clamp(r.get<int32_t>(), 0, 10000);
            if (!r.Ok() || modTitleText.size() > 128 || modSubtitleText.size() > 256)
                return Status::BadArguments;
            modTitleStart = std::chrono::steady_clock::now();
            return Status::Ok;
        });

        mm.Register(Op::SwingArm, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!modLive)
                return Status::Unavailable;
            handRenderer.Swing(options.hand);
            modLive->SendSwingArm("build");
            return Status::Ok;
        });

        mm.Register(Op::UseItem, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!modLive)
                return Status::Unavailable;
            const float eye[3] = {camera.x, camera.y, camera.z};
            modLive->SendUseItemInAir(eye, false);
            return Status::Ok;
        });

        mm.Register(Op::UseOnBlock, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), z = r.get<int32_t>(),
                          face = r.get<int32_t>();
            if (!r.Ok() || face < 0 || face > 5)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            static const int kOff[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                           {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
            const auto nameAt = [&](int bx, int by, int bz) {
                bool loaded = false;
                const uint16_t id = modLive->NetLevel().GetBlock(bx, by, bz, loaded);
                return loaded ? BlockPalette::Instance().Name(id) : std::string();
            };
            const ui::HudItem& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
            int clicked[3] = {x, y, z};
            const blockuse::Result use = blockuse::Classify(
                nameAt(x, y, z), nameAt(x + kOff[face][0], y + kOff[face][1], z + kOff[face][2]), clicked,
                face, !held.Empty(), !held.Empty() && held.blockRuntimeId != 0, player.Sneaking());
            const float click[3] = {0.5f, 0.5f, 0.5f};
            const float eye[3] = {camera.x, camera.y, camera.z};
            int dest[3];
            std::copy(use.dest, use.dest + 3, dest);
            handRenderer.Swing(options.hand);
            modLive->SendSwingArm("build");
            modLive->SendUseItemOnBlock(x, y, z, face, click, eye, static_cast<int>(use.use), dest);
            return Status::Ok;
        });

        mm.Register(Op::AttackEntity, [this, ModEntityClick](CallContext&, abi::Reader& r, abi::Writer&) {
            const uint64_t id = r.get<uint64_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            entity::Entity e;
            if (!modLive->Entities().Find(id, e))
                return Status::NotFound;
            const float eye[3] = {camera.x, camera.y, camera.z};
            const glm::vec3 offset = ModEntityClick(e);
            const float click[3] = {offset.x, offset.y, offset.z};
            handRenderer.Swing(options.hand);
            modLive->SendSwingArm("attack");
            modLive->SendAttackEntity(id, eye, click);
            return Status::Ok;
        });

        mm.Register(Op::InteractEntity, [this, ModEntityClick](CallContext&, abi::Reader& r, abi::Writer&) {
            const uint64_t id = r.get<uint64_t>();
            if (!r.Ok())
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            entity::Entity e;
            if (!modLive->Entities().Find(id, e))
                return Status::NotFound;
            const float eye[3] = {camera.x, camera.y, camera.z};
            const glm::vec3 offset = ModEntityClick(e);
            const float click[3] = {offset.x, offset.y, offset.z};
            handRenderer.Swing(options.hand);
            modLive->SendSwingArm("interact");
            modLive->SendInteractEntity(id, eye, click);
            return Status::Ok;
        });

        mm.Register(Op::BreakBlock, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), z = r.get<int32_t>(),
                          face = r.get<int32_t>();
            if (!r.Ok() || face < 0 || face > 5)
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            handRenderer.Swing(options.hand);
            modLive->SendSwingArm("mine");
            modLive->SendPlayerAuthInputBlockAction(0, x, y, z, face);
            if (modLive->GameType() == 1)
                modLive->SendPlayerAuthInputBlockAction(1, x, y, z, face);
            return Status::Ok;
        });

        mm.Register(Op::Look, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const float yaw = r.get<float>(), pitch = r.get<float>();
            if (!r.Ok() || !std::isfinite(yaw) || !std::isfinite(pitch))
                return Status::BadArguments;
            camera.yaw = yaw + 90.0f;
            camera.pitch = std::clamp(-pitch, -89.0f, 89.0f);
            return Status::Ok;
        });

        mm.Register(Op::Respawn, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!modLive)
                return Status::Unavailable;
            modLive->RequestRespawn();
            return Status::Ok;
        });

        mm.Register(Op::Disconnect, [this](CallContext&, abi::Reader&, abi::Writer&) {
            if (!platform)
                return Status::Unavailable;
            platform->isRunning = false;
            return Status::Ok;
        });

        mm.Register(Op::PlaySound, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            const bool positional = r.boolean();
            const double x = r.get<float>(), y = r.get<float>(), z = r.get<float>();
            const float volume = r.get<float>(), pitch = r.get<float>();
            if (!r.Ok() || name.empty() || name.size() > 128)
                return Status::BadArguments;
            const glm::dvec3 at = positional ? glm::dvec3(x, y, z) : glm::dvec3(camera.x, camera.y, camera.z);
            sounds.PlayNamed(name, at, std::clamp(volume, 0.0f, 4.0f), std::clamp(pitch, 0.25f, 4.0f),
                             positional);
            return Status::Ok;
        });

        mm.Register(Op::UiMeasure, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string text = r.str();
            const float scale = r.get<float>();
            if (!r.Ok() || text.size() > 4000)
                return Status::BadArguments;
            w.put(ui::McFont::Instance().Width(ui::StripFormatCodes(text), std::clamp(scale, 0.25f, 16.0f)));
            return Status::Ok;
        });

        mm.Register(Op::HttpRequest, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            std::string method = r.str();
            std::string url = r.str();
            const int32_t timeout = r.get<int32_t>();
            const uint32_t count = r.get<uint32_t>();
            std::vector<std::pair<std::string, std::string>> headers;
            for (uint32_t i = 0; i < count && i < 64 && r.Ok(); i++) {
                std::string k = r.str();
                headers.emplace_back(std::move(k), r.str());
            }
            std::vector<uint8_t> body = r.bytes();
            if (!r.Ok() || body.size() > (4u << 20))
                return Status::BadArguments;
            if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE" &&
                method != "PATCH" && method != "HEAD")
                return Status::BadArguments;
            if (modHttpActive.load() >= 6)
                return Status::Unavailable;
            StartModHttp(ctx.plugin, id, std::move(method), std::move(url), std::clamp(timeout, 500, 60000),
                         std::move(headers), std::move(body));
            return Status::Ok;
        });

        mm.Register(Op::PacketSend, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            std::vector<uint8_t> body = r.bytes();
            if (!r.Ok() || body.size() > (64u << 20))
                return Status::BadArguments;
            if (!modLive)
                return Status::Unavailable;
            return modLive->SendRaw(id, body) ? Status::Ok : Status::Failed;
        });

        mm.Register(Op::PacketName, [](CallContext&, abi::Reader& r, abi::Writer& w) {
            const uint32_t id = r.get<uint32_t>();
            if (!r.Ok())
                return Status::BadArguments;
            const char* name = BedrockSession::PacketName(id);
            const std::string text = name ? name : "";
            w.raw(text.data(), text.size());
            return Status::Ok;
        });
    }

    void Perlin::ToggleMod(const std::string& name) {
        auto& mm = mods::ModManager::Instance();
        auto& d = options.disabledMods;
        const auto it = std::find(d.begin(), d.end(), name);
        bool pending = false;
        uint32_t requested = 0;
        for (const mods::ModInfo& info : mm.List())
            if (info.name == name) {
                pending = info.needsApproval;
                requested = info.requested;
            }
        if (pending) {
            auto& a = options.approvedMods;
            a.erase(std::remove_if(a.begin(), a.end(),
                                   [&](const std::string& e) { return e.starts_with(name + "="); }),
                    a.end());
            a.push_back(name + "=" + std::to_string(requested));
            if (it != d.end())
                d.erase(it);
            mm.SetApproved(a);
        } else if (it == d.end()) {
            d.push_back(name);
        } else {
            d.erase(it);
        }
        mm.SetDisabled(d);
        options.Save();
    }

    void Perlin::SetupModActions() {
        auto& mm = mods::ModManager::Instance();
        using abi::Op;
        using abi::Status;
        using mods::CallContext;

        mm.Register(Op::OpenScreen, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            const auto screen = r.read<abi::Screen>();
            if (!r.Ok())
                return Status::BadArguments;
            int view = 0;
            switch (screen) {
            case abi::Screen::Pause:
                view = 0;
                break;
            case abi::Screen::Options:
                view = 1;
                break;
            case abi::Screen::Mods:
                view = 4;
                break;
            case abi::Screen::Wardrobe:
                view = 5;
                break;
            default:
                return Status::BadArguments;
            }
            mods::ModManager::Instance().PostMain([this, view] {
                modOpenView = view;
                modOpenPage = 0;
                modSettingsFor.clear();
            });
            return Status::Ok;
        });

        mm.Register(Op::OpenModSettings, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            std::string target = r.str();
            if (!r.Ok())
                return Status::BadArguments;
            if (target.empty())
                target = ctx.plugin;
            if (!mods::ModManager::Instance().HasSettings(target))
                return Status::NotFound;
            mods::ModManager::Instance().PostMain([this, target] {
                modOpenView = 1;
                modOpenPage = 11;
                modSettingsFor = target;
            });
            return Status::Ok;
        });

        mm.Register(Op::TakeScreenshot, [this](CallContext&, abi::Reader&, abi::Writer&) {
            mods::ModManager::Instance().PostMain([this] { TakeScreenshot(); });
            return Status::Ok;
        });

        mm.Register(Op::CyclePerspective, [this](CallContext&, abi::Reader&, abi::Writer&) {
            mods::ModManager::Instance().PostMain([this] {
                camera.mode = static_cast<PerspectiveMode>((static_cast<int>(camera.mode) + 1) % 3);
            });
            return Status::Ok;
        });

        mm.Register(Op::CopyToClipboard, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            std::string text = r.str();
            if (!r.Ok() || text.empty() || text.size() > 65536)
                return Status::BadArguments;
            mods::ModManager::Instance().PostMain([this, text = std::move(text)] {
                if (platform)
                    platform->SetClipboard(text);
            });
            return Status::Ok;
        });

        mm.Register(Op::OpenUrl, [this](CallContext&, abi::Reader& r, abi::Writer&) {
            std::string url = r.str();
            if (!r.Ok() || !url.starts_with("https://") || url.size() > 2048)
                return Status::BadArguments;
            mods::ModManager::Instance().PostMain([this, url = std::move(url)] {
                if (platform)
                    platform->OpenUrl(url);
            });
            return Status::Ok;
        });

        mm.Register(Op::ToggleHud, [this](CallContext&, abi::Reader&, abi::Writer&) {
            mods::ModManager::Instance().PostMain([this] { options.showHud = !options.showHud; });
            return Status::Ok;
        });

        const auto resolve = [](const CallContext& ctx, const abi::FileLocation& at,
                                std::filesystem::path& out) {
            const std::string& rel = at.path;
            if (rel.empty() || rel.size() > 300 || rel[0] == '/' || rel[0] == '\\' || rel.contains("..") ||
                rel.contains('\\') || rel.contains(':'))
                return false;
            std::filesystem::path root;
            switch (at.area) {
            case abi::Area::Mod:
                root = ctx.storageRoot;
                break;
            case abi::Area::Screenshots:
                root = std::filesystem::path(paths::DataDir()) / "screenshots";
                break;
            case abi::Area::ResourcePacks:
                root = std::filesystem::path(paths::DataDir()) / "texturepacks";
                break;
            case abi::Area::ShaderPacks:
                root = std::filesystem::path(paths::DataDir()) / "shaderpacks";
                break;
            default:
                return false;
            }
            out = root / rel;
            return true;
        };
        const auto transfer = [resolve](bool move, const CallContext& ctx, abi::Reader& r) {
            const auto from = r.read<abi::FileLocation>();
            const auto to = r.read<abi::FileLocation>();
            std::filesystem::path src, dst;
            if (!r.Ok() || !resolve(ctx, from, src) || !resolve(ctx, to, dst))
                return Status::BadArguments;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(src, ec))
                return Status::NotFound;
            if (from.area != abi::Area::Mod && from.area != abi::Area::Screenshots)
                return Status::Denied;
            if (move && from.area != abi::Area::Mod)
                return Status::Denied;
            if (to.area != abi::Area::Mod && std::filesystem::exists(dst, ec))
                return Status::Failed;
            std::filesystem::create_directories(dst.parent_path(), ec);
            if (move)
                std::filesystem::rename(src, dst, ec);
            if (!move || ec) {
                ec.clear();
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec && move)
                    std::filesystem::remove(src, ec);
            }
            return ec ? Status::Failed : Status::Ok;
        };
        mm.Register(Op::CopyFile, [transfer](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            return transfer(false, ctx, r);
        });
        mm.Register(Op::MoveFile, [transfer](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            return transfer(true, ctx, r);
        });
        mm.Register(Op::DownloadFile, [this, resolve](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t request = r.get<uint32_t>();
            const std::string url = r.str();
            const auto to = r.read<abi::FileLocation>();
            std::filesystem::path dst;
            if (!r.Ok() || !url.starts_with("https://") || url.size() > 2048 || !resolve(ctx, to, dst))
                return Status::BadArguments;
            std::error_code ec;
            if (to.area != abi::Area::Mod && std::filesystem::exists(dst, ec))
                return Status::Failed;
            if (modHttpActive.load() >= 6)
                return Status::Unavailable;
            StartModHttp(ctx.plugin, request, "GET", url, 30000, {}, {}, dst.string());
            return Status::Ok;
        });
    }
}
