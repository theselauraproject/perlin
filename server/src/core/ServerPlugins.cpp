#include "core/Server.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "inventory/Inventory.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/Loot.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace perlin::server {
    namespace {
        using abi::Status;
        template <class T> using Result = abi::host::Result<T>;

        std::unexpected<Status> Fail(Status s) {
            return std::unexpected(s);
        }

        std::string FullName(const std::string& n) {
            return !n.contains(':') ? "minecraft:" + n : n;
        }

        std::string Num(double v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6g", v);
            return buf;
        }

        std::string Str(const std::string& s) {
            return "\"" + json::Escape(s) + "\"";
        }

        std::string StateText(const StateValue& v) {
            if (const bool* b = std::get_if<bool>(&v))
                return *b ? "true" : "false";
            if (const int32_t* i = std::get_if<int32_t>(&v))
                return std::to_string(*i);
            return std::get<std::string>(v);
        }

        StateValue ParseState(const std::string& s) {
            if (s == "true")
                return true;
            if (s == "false")
                return false;
            int32_t n = 0;
            const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
            if (ec == std::errc() && end == s.data() + s.size() && !s.empty())
                return n;
            return s;
        }

        Session::DamageCause CauseOf(abi::DamageCause c) {
            const int v = static_cast<int>(c);
            if (v < 0 || v > static_cast<int>(Session::DamageCause::Kill))
                return Session::DamageCause::Generic;
            return static_cast<Session::DamageCause>(v);
        }

        bool ValidDimension(abi::Dimension d) {
            const int v = static_cast<int>(d);
            return v >= 0 && v < kDimensionCount;
        }

        std::string UuidText(const std::array<uint8_t, 16>& b) {
            static const char* hex = "0123456789abcdef";
            std::string out;
            for (size_t i = 0; i < b.size(); i++) {
                if (i == 4 || i == 6 || i == 8 || i == 10)
                    out.push_back('-');
                out.push_back(hex[b[i] >> 4]);
                out.push_back(hex[b[i] & 15]);
            }
            return out;
        }

        abi::ServerItem ItemOf(const ItemStack& s) {
            abi::ServerItem out;
            if (s.Empty())
                return out;
            const CatalogItem* item = BlockCatalog::Instance().ItemById(s.networkId);
            out.name = item ? item->name : std::string();
            out.count = s.count;
            out.damage = s.damage;
            return out;
        }

        std::optional<ItemStack> StackOf(const abi::ServerItem& item) {
            if (item.name.empty() || item.count == 0)
                return ItemStack{};
            const CatalogItem* c = BlockCatalog::Instance().ItemByName(FullName(item.name));
            if (!c)
                return std::nullopt;
            ItemStack s;
            s.networkId = c->networkId;
            s.count = static_cast<uint16_t>(std::min<uint32_t>(item.count, 255));
            s.damage = static_cast<uint16_t>(std::min<uint32_t>(item.damage, 65535));
            return s;
        }

        abi::ServerEntity EntityOf(const Mob& m) {
            abi::ServerEntity e;
            e.id = m.id;
            e.type = m.kind == 1 ? "minecraft:falling_block" : m.kind == 2 ? "minecraft:tnt" : m.def->id;
            e.dimension = abi::Dimension::Overworld;
            e.x = m.x;
            e.y = m.y;
            e.z = m.z;
            e.yaw = m.yaw;
            e.health = m.health;
            e.maxHealth = m.def->health;
            e.baby = m.baby;
            return e;
        }

        std::string FormJson(const abi::Form& f) {
            std::string j;
            if (f.kind == abi::FormKind::Message) {
                const std::string b1 = f.buttons.size() > 0 ? f.buttons[0].text : "gui.yes";
                const std::string b2 = f.buttons.size() > 1 ? f.buttons[1].text : "gui.no";
                return "{\"type\":\"modal\",\"title\":" + Str(f.title) + ",\"content\":" + Str(f.body) +
                       ",\"button1\":" + Str(b1) + ",\"button2\":" + Str(b2) + "}";
            }
            if (f.kind == abi::FormKind::Menu) {
                j = "{\"type\":\"form\",\"title\":" + Str(f.title) + ",\"content\":" + Str(f.body) + ",\"buttons\":[";
                for (size_t i = 0; i < f.buttons.size(); i++) {
                    const abi::FormButton& b = f.buttons[i];
                    j += (i ? ",{" : "{") + std::string("\"text\":") + Str(b.text);
                    if (!b.image.empty()) {
                        const bool url = b.image.starts_with("http://") || b.image.starts_with("https://");
                        j += ",\"image\":{\"type\":" + Str(url ? "url" : "path") + ",\"data\":" + Str(b.image) + "}";
                    }
                    j += "}";
                }
                return j + "]}";
            }
            j = "{\"type\":\"custom_form\",\"title\":" + Str(f.title) + ",\"content\":[";
            for (size_t i = 0; i < f.controls.size(); i++) {
                const abi::FormControl& c = f.controls[i];
                std::string e = "{\"text\":" + Str(c.text);
                const auto options = [&] {
                    std::string o = "[";
                    for (size_t k = 0; k < c.options.size(); k++)
                        o += (k ? "," : "") + Str(c.options[k]);
                    return o + "]";
                };
                switch (c.kind) {
                case abi::FormElement::Label:
                    e += ",\"type\":\"label\"";
                    break;
                case abi::FormElement::Header:
                    e += ",\"type\":\"header\"";
                    break;
                case abi::FormElement::Divider:
                    e += ",\"type\":\"divider\"";
                    break;
                case abi::FormElement::Toggle:
                    e += ",\"type\":\"toggle\",\"default\":" + std::string(c.defaultText == "true" ? "true" : "false");
                    break;
                case abi::FormElement::Slider:
                    e += ",\"type\":\"slider\",\"min\":" + Num(c.min) + ",\"max\":" + Num(c.max) + ",\"step\":" +
                         Num(c.step) + ",\"default\":" + Num(c.defaultNumber);
                    break;
                case abi::FormElement::StepSlider:
                    e += ",\"type\":\"step_slider\",\"steps\":" + options() +
                         ",\"default\":" + std::to_string(static_cast<int>(c.defaultNumber));
                    break;
                case abi::FormElement::Dropdown:
                    e += ",\"type\":\"dropdown\",\"options\":" + options() +
                         ",\"default\":" + std::to_string(static_cast<int>(c.defaultNumber));
                    break;
                case abi::FormElement::Input:
                    e += ",\"type\":\"input\",\"placeholder\":" + Str(c.placeholder) + ",\"default\":" + Str(c.defaultText);
                    break;
                }
                j += (i ? "," : "") + e + "}";
            }
            return j + "]}";
        }

        std::string ValueText(const json::Value& v) {
            switch (v.kind) {
            case json::Value::Kind::Bool:
                return v.boolean ? "true" : "false";
            case json::Value::Kind::Number:
                return Num(v.number);
            case json::Value::Kind::String:
                return v.text;
            default:
                return {};
            }
        }
    }

    void Server::RegisterCommand(const std::string& plugin, const std::string& name, const std::string& description) {
        CommandDef d;
        d.name = str::Lower(name);
        d.description = description;
        d.plugin = plugin;
        commands[d.name] = std::move(d);
    }

    void Server::UnregisterCommand(const std::string& plugin, const std::string& name) {
        const auto it = commands.find(str::Lower(name));
        if (it != commands.end() && it->second.plugin == plugin)
            commands.erase(it);
    }

    Result<void> Server::OpBroadcast(PluginCall&, const abi::TextMessage& m) {
        Broadcast(m.text);
        return {};
    }
    Result<void> Server::OpPlayerMessage(PluginCall&, const abi::PlayerText& m) {
        if (!Message(m.player, m.text))
            return Fail(Status::NotFound);
        return {};
    }
    Result<void> Server::OpKick(PluginCall&, const abi::PlayerText& m) {
        if (!Kick(m.player, m.text))
            return Fail(Status::NotFound);
        return {};
    }
    Result<abi::OnlinePlayerList> Server::OpOnlinePlayers(PluginCall&) {
        abi::OnlinePlayerList list;
        for (Session* s : InGamePlayers())
            list.players.push_back({s->Id(), s->Name(), s->X(), s->Y() - 1.62, s->Z()});
        return list;
    }
    Result<void> Server::OpTeleport(PluginCall&, const abi::ServerTeleport& t) {
        if (!Teleport(t.player, t.x, t.y, t.z))
            return Fail(Status::NotFound);
        return {};
    }
    Result<void> Server::OpSetBlock(PluginCall&, const abi::BlockWrite& b) {
        if (const Status s = SetBlock(b.x, b.y, b.z, b.block); s != Status::Ok)
            return Fail(s);
        return {};
    }
    Result<abi::TextMessage> Server::OpBlockName(PluginCall&, const abi::BlockCoord& at) {
        abi::TextMessage name;
        if (const Status s = GetBlock(at.x, at.y, at.z, name.text); s != Status::Ok)
            return Fail(s);
        return name;
    }
    Result<abi::ServerStatus> Server::OpServerInfo(PluginCall&) {
        return abi::ServerStatus{config.name, static_cast<uint32_t>(InGamePlayers().size()), config.maxPlayers, tick,
                                 chunks ? chunks->BackendName() : ""};
    }
    Result<abi::GiveItemResult> Server::OpGiveItem(PluginCall&, const abi::GiveItemRequest& r) {
        if (r.count == 0 || r.count > 4096)
            return Fail(Status::BadArguments);
        const auto left = GiveItem(r.player, r.item, static_cast<int>(r.count));
        if (!left)
            return Fail(left.error());
        return abi::GiveItemResult{static_cast<uint32_t>(*left)};
    }

#define PERLIN_PLAYER(id)                                                                                    \
    Session* s = FindSession(id);                                                                            \
    if (!s || !s->InGame())                                                                                  \
        return Fail(Status::NotFound);

    Result<abi::PlayerDetails> Server::OpPlayerInfo(PluginCall&, const abi::PlayerInfoRequest& r) {
        PERLIN_PLAYER(r.player)
        abi::PlayerDetails d;
        d.id = s->Id();
        d.name = s->Name();
        d.xuid = s->Xuid();
        d.uuid = UuidText(s->UuidBytes());
        d.dimension = static_cast<abi::Dimension>(s->DimensionId());
        d.x = s->X();
        d.y = s->Y() - 1.62;
        d.z = s->Z();
        d.yaw = s->Yaw();
        d.pitch = s->Pitch();
        d.gameMode = static_cast<abi::GameMode>(s->GameMode());
        d.health = s->Health();
        d.maxHealth = s->MaxHealth();
        d.absorption = s->Absorption();
        d.food = s->Food();
        d.saturation = s->Saturation();
        d.xpLevel = s->XpLevel();
        d.xpProgress = s->XpProgress();
        d.onGround = s->OnGround();
        d.sneaking = s->Sneaking();
        d.sprinting = s->Sprinting();
        d.dead = s->Dead();
        d.isOperator = s->Operator();
        for (const Session::EffectState& e : s->Effects())
            d.effects.push_back({static_cast<abi::EffectId>(e.id), e.amplifier, e.ticks, e.ambient, e.particles});
        return d;
    }
    Result<void> Server::OpPlayerSetGameMode(PluginCall&, const abi::PlayerSetGameModeRequest& r) {
        PERLIN_PLAYER(r.player)
        const int mode = static_cast<int>(r.mode);
        if (mode != 0 && mode != 1 && mode != 2 && mode != 6)
            return Fail(Status::BadArguments);
        s->SetGameMode(mode);
        return {};
    }
    Result<void> Server::OpPlayerSetHealth(PluginCall&, const abi::PlayerSetHealthRequest& r) {
        PERLIN_PLAYER(r.player)
        if (!std::isfinite(r.health))
            return Fail(Status::BadArguments);
        s->SetHealth(r.health);
        return {};
    }
    Result<bool> Server::OpPlayerDamage(PluginCall&, const abi::PlayerDamageRequest& r) {
        PERLIN_PLAYER(r.player)
        if (!std::isfinite(r.amount) || r.amount <= 0)
            return Fail(Status::BadArguments);
        return s->Damage(r.amount, CauseOf(r.cause), s->X(), s->Z(), "plugin", nullptr, r.knockback ? 0.4f : 0.0f);
    }
    Result<void> Server::OpPlayerKill(PluginCall&, const abi::PlayerKillRequest& r) {
        PERLIN_PLAYER(r.player)
        s->Kill();
        return {};
    }
    Result<void> Server::OpPlayerSetFood(PluginCall&, const abi::PlayerSetFoodRequest& r) {
        PERLIN_PLAYER(r.player)
        s->SetFood(r.food, r.saturation);
        return {};
    }
    Result<void> Server::OpPlayerAddExperience(PluginCall&, const abi::PlayerAddExperienceRequest& r) {
        PERLIN_PLAYER(r.player)
        if (r.points)
            s->AddExperience(r.points);
        if (r.levels)
            s->AddLevels(r.levels);
        return {};
    }
    Result<void> Server::OpPlayerEffectAdd(PluginCall&, const abi::PlayerEffectAddRequest& r) {
        PERLIN_PLAYER(r.player)
        const int id = static_cast<int>(r.effect);
        if (id <= 0 || id > 63 || r.ticks <= 0)
            return Fail(Status::BadArguments);
        s->AddEffect(id, std::clamp(r.amplifier, 0, 255), r.ticks, r.particles);
        return {};
    }
    Result<void> Server::OpPlayerEffectRemove(PluginCall&, const abi::PlayerEffectRemoveRequest& r) {
        PERLIN_PLAYER(r.player)
        if (r.effect == 0)
            s->ClearEffects();
        else if (!s->RemoveEffect(r.effect))
            return Fail(Status::NotFound);
        return {};
    }
    Result<abi::ServerInventory> Server::OpPlayerInventory(PluginCall&, const abi::PlayerInventoryRequest& r) {
        PERLIN_PLAYER(r.player)
        abi::ServerInventory out;
        const PlayerInventory& inv = s->Inventory();
        for (const ItemStack& i : inv.main)
            out.main.push_back(ItemOf(i));
        for (const ItemStack& i : inv.armor)
            out.armor.push_back(ItemOf(i));
        out.offhand = ItemOf(inv.offhand);
        out.selected = inv.selected;
        return out;
    }
    Result<void> Server::OpPlayerSetSlot(PluginCall&, const abi::PlayerSetSlotRequest& r) {
        PERLIN_PLAYER(r.player)
        const auto stack = StackOf(r.item);
        if (!stack)
            return Fail(Status::BadArguments);
        PlayerInventory& inv = s->Inventory();
        ItemStack* slot = nullptr;
        if (r.container == 0 && r.slot >= 0 && r.slot < PlayerInventory::kMainSlots)
            slot = &inv.main[static_cast<size_t>(r.slot)];
        else if (r.container == 1 && r.slot >= 0 && r.slot < 4)
            slot = &inv.armor[static_cast<size_t>(r.slot)];
        else if (r.container == 2)
            slot = &inv.offhand;
        if (!slot)
            return Fail(Status::BadArguments);
        *slot = *stack;
        if (!slot->Empty())
            slot->stackId = inv.NewStackId();
        s->SendInventory();
        if (r.container != 0)
            BroadcastArmor(*s);
        return {};
    }
    Result<void> Server::OpPlayerClearInventory(PluginCall&, const abi::PlayerClearInventoryRequest& r) {
        PERLIN_PLAYER(r.player)
        PlayerInventory& inv = s->Inventory();
        inv.main.fill({});
        inv.armor.fill({});
        inv.offhand = {};
        inv.cursor = {};
        s->SendInventory();
        BroadcastArmor(*s);
        return {};
    }
    Result<void> Server::OpPlayerSetVelocity(PluginCall&, const abi::PlayerSetVelocityRequest& r) {
        PERLIN_PLAYER(r.player)
        s->SetVelocity(r.x, r.y, r.z);
        return {};
    }
    Result<void> Server::OpPlayerTransferDimension(PluginCall&, const abi::PlayerTransferDimensionRequest& r) {
        PERLIN_PLAYER(r.player)
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        ChangeDimension(*s, static_cast<int>(r.dimension), r.x, r.y + 1.62, r.z);
        return {};
    }
    Result<void> Server::OpPlayerTitle(PluginCall&, const abi::PlayerTitleRequest& r) {
        PERLIN_PLAYER(r.player)
        s->SendTitle(5, "", r.fadeIn, r.stay, r.fadeOut);
        if (!r.subtitle.empty())
            s->SendTitle(3, r.subtitle, r.fadeIn, r.stay, r.fadeOut);
        s->SendTitle(r.title.empty() ? 0 : 2, r.title, r.fadeIn, r.stay, r.fadeOut);
        return {};
    }
    Result<void> Server::OpPlayerActionBar(PluginCall&, const abi::PlayerActionBarRequest& r) {
        PERLIN_PLAYER(r.player)
        s->SendActionBar(r.text);
        return {};
    }
    Result<void> Server::OpPlayerSound(PluginCall&, const abi::PlayerSoundRequest& r) {
        if (r.sound.empty())
            return Fail(Status::BadArguments);
        if (r.player == 0) {
            for (Session* s : InGamePlayers())
                s->PlaySoundTo(r.sound, r.x, r.y, r.z, r.volume, r.pitch);
            return {};
        }
        PERLIN_PLAYER(r.player)
        s->PlaySoundTo(r.sound, r.x, r.y, r.z, r.volume, r.pitch);
        return {};
    }
    Result<void> Server::OpPlayerTransfer(PluginCall&, const abi::PlayerTransferRequest& r) {
        PERLIN_PLAYER(r.player)
        if (r.host.empty() || r.port == 0)
            return Fail(Status::BadArguments);
        s->Transfer(r.host, r.port);
        return {};
    }
    Result<void> Server::OpPlayerSetOperator(PluginCall&, const abi::PlayerSetOperatorRequest& r) {
        PERLIN_PLAYER(r.player)
        LogInfo(SetOperator(s->Name(), r.isOperator));
        return {};
    }
    Result<uint32_t> Server::OpPlayerForm(PluginCall&, const abi::PlayerFormRequest& r) {
        PERLIN_PLAYER(r.player)
        if (r.form.buttons.size() > 256 || r.form.controls.size() > 256)
            return Fail(Status::BadArguments);
        const uint32_t id = s->ShowForm(FormJson(r.form));
        if (formKinds.size() > 4096)
            formKinds.clear();
        formKinds[{s->Id(), id}] = r.form.kind;
        return id;
    }
    Result<void> Server::OpPlayerRunCommand(PluginCall&, const abi::PlayerRunCommandRequest& r) {
        PERLIN_PLAYER(r.player)
        RunCommand(s, r.line);
        return {};
    }
    Result<void> Server::OpPlayerSetSpawn(PluginCall&, const abi::PlayerSetSpawnRequest& r) {
        PERLIN_PLAYER(r.player)
        s->SetSpawnPoint(r.x, r.y, r.z);
        return {};
    }
    Result<uint64_t> Server::OpPlayerByName(PluginCall&, const abi::PlayerByNameRequest& r) {
        Session* s = FindByName(r.name);
        if (!s || !s->InGame())
            return Fail(Status::NotFound);
        return s->Id();
    }

#undef PERLIN_PLAYER

    Result<abi::BlockInfo> Server::OpBlockState(PluginCall&, const abi::BlockStateRequest& r) {
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        ChunkManager& w = World(static_cast<int>(r.dimension));
        abi::BlockInfo info;
        if (!w.Peek({r.x >> 4, r.z >> 4}))
            return info;
        const uint16_t id = w.GetBlock(r.x, r.y, r.z);
        info.loaded = true;
        info.name = BlockRegistry::Instance().Name(id);
        info.runtimeId = static_cast<uint32_t>(BlockRegistry::Instance().Hash(id));
        for (const auto& [k, v] : BlockRegistry::Instance().States(id))
            info.states.emplace_back(k, StateText(v));
        info.liquid = BlockLiquid(id) ? 1u : 0u;
        return info;
    }
    Result<void> Server::OpBlockStateSet(PluginCall&, const abi::BlockStateSetRequest& r) {
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        const int dim = static_cast<int>(r.dimension);
        const DimensionRange range = RangeOf(dim);
        if (r.y < range.minY || r.y > range.MaxY())
            return Fail(Status::BadArguments);
        if (!World(dim).Peek({r.x >> 4, r.z >> 4}))
            return Fail(Status::Unavailable);
        const std::string name = FullName(r.name);
        uint16_t block = BlockRegistry::kAir;
        if (name != "minecraft:air") {
            if (r.states.empty()) {
                if (!BlockCatalog::Instance().Knows(name))
                    return Fail(Status::BadArguments);
                block = BlockCatalog::Instance().DefaultBlock(name);
            } else {
                StateList states;
                for (const auto& [k, v] : r.states)
                    states.emplace_back(k, ParseState(v));
                std::ranges::sort(states, [](const auto& a, const auto& b) { return a.first < b.first; });
                const int32_t hash = BlockNetworkHash(name, states);
                if (!BlockCatalog::Instance().KnownHash(hash))
                    return Fail(Status::BadArguments);
                block = BlockCatalog::Instance().RegistryId(hash);
            }
        }
        ApplyBlock(dim, r.x, r.y, r.z, block);
        return {};
    }
    Result<uint32_t> Server::OpFill(PluginCall&, const abi::FillRequest& r) {
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        const int dim = static_cast<int>(r.dimension);
        const DimensionRange range = RangeOf(dim);
        const int x0 = std::min(r.x0, r.x1), x1 = std::max(r.x0, r.x1);
        const int y0 = std::max(std::min(r.y0, r.y1), range.minY), y1 = std::min(std::max(r.y0, r.y1), range.MaxY());
        const int z0 = std::min(r.z0, r.z1), z1 = std::max(r.z0, r.z1);
        const int64_t volume = int64_t{x1 - x0 + 1} * (y1 - y0 + 1) * (z1 - z0 + 1);
        if (volume <= 0 || volume > (1 << 20))
            return Fail(Status::BadArguments);
        const std::string name = FullName(r.block);
        if (name != "minecraft:air" && !BlockCatalog::Instance().Knows(name))
            return Fail(Status::BadArguments);
        const uint16_t block = name == "minecraft:air" ? BlockRegistry::kAir : BlockCatalog::Instance().DefaultBlock(name);
        ChunkManager& w = World(dim);
        uint32_t changed = 0;
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) {
                if (!w.Peek({x >> 4, z >> 4}))
                    continue;
                for (int y = y0; y <= y1; y++)
                    if (w.GetBlock(x, y, z) != block) {
                        ApplyBlock(dim, x, y, z, block);
                        changed++;
                    }
            }
        return changed;
    }
    Result<abi::TimeGetResponse> Server::OpTimeGet(PluginCall&) {
        return abi::TimeGetResponse{dayTime, dayTime / 24000, static_cast<int32_t>(dayTime % 24000)};
    }
    Result<void> Server::OpTimeSet(PluginCall&, const abi::TimeSetRequest& r) {
        SetDayTime(r.time);
        return {};
    }
    Result<void> Server::OpWeatherSet(PluginCall&, const abi::WeatherSetRequest& r) {
        static const char* kinds[] = {"clear", "rain", "thunder"};
        const int k = static_cast<int>(r.weather);
        if (k < 0 || k > 2)
            return Fail(Status::BadArguments);
        SetWeather(kinds[k], r.ticks > 0 ? r.ticks : 6000);
        return {};
    }
    Result<int32_t> Server::OpGameRuleGet(PluginCall&, const abi::GameRuleGetRequest& r) {
        const auto it = gameRules.find(str::Lower(r.name));
        if (it == gameRules.end())
            return Fail(Status::NotFound);
        return it->second.value;
    }
    Result<void> Server::OpGameRuleSet(PluginCall&, const abi::GameRuleSetRequest& r) {
        if (!SetRule(str::Lower(r.name), r.value))
            return Fail(Status::NotFound);
        return {};
    }
    Result<void> Server::OpDifficultySet(PluginCall&, const abi::DifficultySetRequest& r) {
        const int d = static_cast<int>(r.difficulty);
        if (d < 0 || d > 3)
            return Fail(Status::BadArguments);
        config.difficulty = d;
        for (Session* s : InGamePlayers())
            s->SendDifficulty(d);
        return {};
    }
    Result<void> Server::OpExplode(PluginCall&, const abi::ExplodeRequest& r) {
        if (r.dimension != abi::Dimension::Overworld)
            return Fail(Status::Unavailable);
        if (!std::isfinite(r.power) || r.power <= 0 || r.power > 32)
            return Fail(Status::BadArguments);
        mobWorld->Explosion(r.x, r.y, r.z, r.power, 0, r.breakBlocks);
        return {};
    }
    Result<int32_t> Server::OpHighestBlock(PluginCall&, const abi::HighestBlockRequest& r) {
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        const int dim = static_cast<int>(r.dimension);
        ChunkManager& w = World(dim);
        if (!w.Peek({r.x >> 4, r.z >> 4}))
            return Fail(Status::Unavailable);
        const DimensionRange range = RangeOf(dim);
        for (int y = range.MaxY(); y >= range.minY; y--)
            if (w.GetBlock(r.x, y, r.z) != BlockRegistry::kAir)
                return y;
        return Fail(Status::NotFound);
    }
    Result<void> Server::OpParticleAt(PluginCall&, const abi::ParticleAtRequest& r) {
        if (!ValidDimension(r.dimension) || r.name.empty())
            return Fail(Status::BadArguments);
        for (Session* s : PlayersIn(static_cast<int>(r.dimension)))
            s->SpawnParticle(r.name, r.x, r.y, r.z);
        return {};
    }
    Result<void> Server::OpDropItem(PluginCall&, const abi::DropItemRequest& r) {
        if (!ValidDimension(r.dimension))
            return Fail(Status::BadArguments);
        const auto stack = StackOf(r.item);
        if (!stack || stack->Empty())
            return Fail(Status::BadArguments);
        SpawnItem(*stack, r.x, r.y, r.z, 0, 0.2, 0, 10, static_cast<int>(r.dimension));
        return {};
    }
    Result<abi::BlockCoord> Server::OpWorldSpawnGet(PluginCall&) {
        return abi::BlockCoord{spawnX, spawnY, spawnZ};
    }
    Result<void> Server::OpWorldSpawnSet(PluginCall&, const abi::BlockCoord& at) {
        SetWorldSpawn(at.x, at.y, at.z);
        return {};
    }

    Result<uint64_t> Server::OpEntitySpawn(PluginCall&, const abi::EntitySpawnRequest& r) {
        Mob* m = mobWorld->Spawn(FullName(r.type), r.x, r.y, r.z, true);
        if (!m)
            return Fail(Status::BadArguments);
        if (r.baby) {
            m->baby = true;
            for (Session* s : PlayersIn(kOverworld))
                s->MobData(*m);
        }
        return m->id;
    }
    Result<abi::EntityListResponse> Server::OpEntityList(PluginCall&, const abi::EntityListRequest& r) {
        abi::EntityListResponse out;
        const std::string type = r.type.empty() ? std::string() : FullName(r.type);
        for (const Mob& m : mobWorld->All()) {
            if (m.deathTime > 0)
                continue;
            const abi::ServerEntity e = EntityOf(m);
            if (!type.empty() && e.type != type)
                continue;
            if (r.radius > 0) {
                const double dx = m.x - r.x, dy = m.y - r.y, dz = m.z - r.z;
                if (dx * dx + dy * dy + dz * dz > r.radius * r.radius)
                    continue;
            }
            out.entities.push_back(e);
            if (out.entities.size() >= 4096)
                break;
        }
        return out;
    }
    Result<void> Server::OpEntityRemove(PluginCall&, const abi::EntityRemoveRequest& r) {
        if (!mobWorld->Remove(r.entity))
            return Fail(Status::NotFound);
        return {};
    }
    Result<bool> Server::OpEntityDamage(PluginCall&, const abi::EntityDamageRequest& r) {
        if (!mobWorld->Find(r.entity))
            return Fail(Status::NotFound);
        return mobWorld->Damage(r.entity, r.amount);
    }
    Result<void> Server::OpEntityTeleport(PluginCall&, const abi::EntityTeleportRequest& r) {
        Mob* m = mobWorld->Find(r.entity);
        if (!m)
            return Fail(Status::NotFound);
        m->x = r.x;
        m->y = r.y;
        m->z = r.z;
        m->vx = m->vy = m->vz = 0;
        mobWorld->Moved(*m);
        return {};
    }
    Result<void> Server::OpEntitySetVelocity(PluginCall&, const abi::EntitySetVelocityRequest& r) {
        Mob* m = mobWorld->Find(r.entity);
        if (!m)
            return Fail(Status::NotFound);
        m->vx = r.x;
        m->vy = r.y;
        m->vz = r.z;
        mobWorld->Moved(*m);
        return {};
    }
    Result<abi::ServerEntity> Server::OpEntityInfo(PluginCall&, const abi::EntityInfoRequest& r) {
        const Mob* m = mobWorld->Find(r.entity);
        if (!m)
            return Fail(Status::NotFound);
        return EntityOf(*m);
    }
    Result<void> Server::OpEntitySetHealth(PluginCall&, const abi::EntitySetHealthRequest& r) {
        Mob* m = mobWorld->Find(r.entity);
        if (!m || m->deathTime > 0)
            return Fail(Status::NotFound);
        if (r.health <= 0) {
            mobWorld->Kill(r.entity);
            return {};
        }
        m->health = std::min(r.health, m->def->health);
        return {};
    }

    Result<abi::RestText> Server::OpRunCommand(PluginCall& c, const abi::RunCommandRequest& r) {
        LogInfo("[" + c.plugin + "] ran /" + r.line);
        abi::RestText out;
        {
            LogCapture capture;
            RunConsoleCommand(r.line);
            out.value = capture.Text();
        }
        return out;
    }
    Result<void> Server::OpMotdSet(PluginCall&, const abi::MotdSetRequest& r) {
        if (r.motd.empty() || r.motd.size() > 256)
            return Fail(Status::BadArguments);
        config.motd = r.motd;
        config.name = r.motd;
        return {};
    }
    Result<void> Server::OpLootTableOverride(PluginCall& c, const abi::LootTableOverrideRequest& r) {
        std::string error;
        if (!LootTables::Instance().Override(r.key, r.json, error)) {
            LogWarn("[" + c.plugin + "] loot table " + r.key + ": " + error);
            return Fail(Status::BadArguments);
        }
        return {};
    }
    Result<abi::RestText> Server::OpLootTableGet(PluginCall&, const abi::LootTableGetRequest& r) {
        std::string text = LootTables::Instance().Source(r.key);
        if (text.empty())
            return Fail(Status::NotFound);
        return abi::RestText{std::move(text)};
    }

    bool Server::HookPreLogin(Session& who, std::string& reason) {
        if (!plugins->AnyoneListens(abi::Event::PreLogin))
            return false;
        const bool cancelled = plugins->Dispatch<abi::Event::PreLogin>({who.Name(), who.Xuid(), who.Address()});
        if (cancelled)
            reason = plugins->AmendedText("reason").value_or("");
        return cancelled;
    }

    bool Server::HookPlayerDamage(Session& who, float& amount, Session::DamageCause cause, Session* attacker,
                                  const std::string& source) {
        if (!plugins->AnyoneListens(abi::Event::PlayerDamage))
            return false;
        abi::EventServerDamage e;
        e.player = who.Id();
        e.amount = amount;
        e.cause = static_cast<abi::DamageCause>(static_cast<int>(cause));
        e.attacker = attacker ? attacker->Id() : 0;
        e.attackerType = attacker ? "minecraft:player" : source;
        if (plugins->Dispatch<abi::Event::PlayerDamage>(e))
            return true;
        if (const auto v = plugins->AmendedNumber("amount"))
            amount = static_cast<float>(*v);
        return false;
    }

    void Server::HookPlayerDeath(Session& who, Session::DamageCause cause, const std::string& source) {
        if (!plugins->AnyoneListens(abi::Event::PlayerDeath))
            return;
        Session* killer = source.empty() ? nullptr : FindByName(source);
        plugins->Dispatch<abi::Event::PlayerDeath>(
            {who.Id(), static_cast<abi::DamageCause>(static_cast<int>(cause)), killer ? killer->Id() : 0, source});
    }

    void Server::HookPlayerRespawn(Session& who) {
        if (plugins->AnyoneListens(abi::Event::PlayerRespawn))
            plugins->Dispatch<abi::Event::PlayerRespawn>({who.Id(), who.Name()});
    }

    bool Server::HookPlayerUse(Session& who, int x, int y, int z, int face) {
        if (!plugins->AnyoneListens(abi::Event::PlayerUseBlock))
            return false;
        abi::EventPlayerUse e;
        e.player = who.Id();
        e.x = x;
        e.y = y;
        e.z = z;
        e.face = static_cast<abi::Face>(std::clamp(face, 0, 5));
        ChunkManager& w = World(who.DimensionId());
        if (w.Peek({x >> 4, z >> 4}))
            e.block = BlockRegistry::Instance().Name(w.GetBlock(x, y, z));
        e.item = ItemOf(who.Inventory().Held()).name;
        return plugins->Dispatch<abi::Event::PlayerUseBlock>(e);
    }

    bool Server::HookPlayerAttack(Session& who, uint64_t target, float& damage) {
        if (!plugins->AnyoneListens(abi::Event::PlayerAttack))
            return false;
        std::string type;
        if (FindSession(target))
            type = "minecraft:player";
        else if (const Mob* m = mobWorld->Find(target))
            type = EntityOf(*m).type;
        if (plugins->Dispatch<abi::Event::PlayerAttack>({who.Id(), target, type, damage}))
            return true;
        if (const auto v = plugins->AmendedNumber("damage"))
            damage = static_cast<float>(*v);
        return false;
    }

    bool Server::HookEntityDamage(Mob& mob, float& amount, Session* attacker) {
        if (!plugins->AnyoneListens(abi::Event::EntityDamage))
            return false;
        abi::EventEntityDamage e;
        e.entity = mob.id;
        e.type = mob.def->id;
        e.amount = amount;
        e.cause = attacker ? abi::DamageCause::Player : abi::DamageCause::Generic;
        e.attacker = attacker ? attacker->Id() : 0;
        if (plugins->Dispatch<abi::Event::EntityDamage>(e))
            return true;
        if (const auto v = plugins->AmendedNumber("amount"))
            amount = static_cast<float>(*v);
        return false;
    }

    void Server::HookEntityDeath(Mob& mob, Session* killer) {
        if (plugins->AnyoneListens(abi::Event::EntityDeath))
            plugins->Dispatch<abi::Event::EntityDeath>(
                {mob.id, mob.def->id, mob.x, mob.y, mob.z, killer ? killer->Id() : 0});
    }

    bool Server::HookEntitySpawn(const std::string& type, uint64_t id, double x, double y, double z, bool natural) {
        if (!plugins->AnyoneListens(abi::Event::EntitySpawn))
            return false;
        return plugins->Dispatch<abi::Event::EntitySpawn>({id, type, x, y, z, natural});
    }

    bool Server::HookItemConsume(Session& who, const std::string& item) {
        if (!plugins->AnyoneListens(abi::Event::ItemConsume))
            return false;
        return plugins->Dispatch<abi::Event::ItemConsume>({who.Id(), item, 1});
    }

    void Server::HookGameModeChange(Session& who, int from, int to) {
        if (plugins->AnyoneListens(abi::Event::GameModeChange))
            plugins->Dispatch<abi::Event::GameModeChange>(
                {who.Id(), static_cast<abi::GameMode>(from), static_cast<abi::GameMode>(to)});
    }

    void Server::HookFormResponse(Session& who, uint32_t form, const std::string& data) {
        const auto it = formKinds.find({who.Id(), form});
        if (it == formKinds.end())
            return;
        const abi::FormKind kind = it->second;
        formKinds.erase(it);
        if (!plugins->AnyoneListens(abi::Event::FormResponse))
            return;
        abi::EventFormResponse e;
        e.player = who.Id();
        e.form = form;
        json::Value v;
        const bool parsed = !data.empty() && json::Parse(data, v);
        e.closed = !parsed || v.kind == json::Value::Kind::Null;
        if (!e.closed) {
            if (kind == abi::FormKind::Message)
                e.button = v.kind == json::Value::Kind::Bool ? (v.boolean ? 0 : 1) : -1;
            else if (kind == abi::FormKind::Menu)
                e.button = v.kind == json::Value::Kind::Number ? static_cast<int32_t>(v.number) : -1;
            else if (v.kind == json::Value::Kind::Array)
                for (const json::Value& item : v.items)
                    e.values.push_back(ValueText(item));
        }
        plugins->Dispatch<abi::Event::FormResponse>(e);
    }

    void Server::HookPlayerMove(Session& who) {
        if (!plugins->AnyoneListens(abi::Event::PlayerMove))
            return;
        plugins->Dispatch<abi::Event::PlayerMove>({who.Id(), static_cast<abi::Dimension>(who.DimensionId()), who.X(),
                                                   who.Y() - 1.62, who.Z(), who.Yaw(), who.Pitch(), who.OnGround()});
    }

    bool Server::HookExplosion(double x, double y, double z, float& power, uint64_t source) {
        if (!plugins->AnyoneListens(abi::Event::Explosion))
            return false;
        std::string type;
        if (const Mob* m = mobWorld->Find(source))
            type = EntityOf(*m).type;
        if (plugins->Dispatch<abi::Event::Explosion>({x, y, z, power, source, type}))
            return true;
        if (const auto v = plugins->AmendedNumber("power"))
            power = static_cast<float>(*v);
        return false;
    }

    bool Server::HookItemDrop(Session& who, const ItemStack& stack) {
        if (!plugins->AnyoneListens(abi::Event::ItemDrop))
            return false;
        const abi::ServerItem item = ItemOf(stack);
        return plugins->Dispatch<abi::Event::ItemDrop>({who.Id(), item.name, item.count});
    }

    bool Server::HookItemPickup(Session& who, const ItemStack& stack) {
        if (!plugins->AnyoneListens(abi::Event::ItemPickup))
            return false;
        const abi::ServerItem item = ItemOf(stack);
        return plugins->Dispatch<abi::Event::ItemPickup>({who.Id(), item.name, item.count});
    }

    bool Server::HookContainerOpen(Session& who, int x, int y, int z, const std::string& block) {
        if (!plugins->AnyoneListens(abi::Event::ContainerOpen))
            return false;
        return plugins->Dispatch<abi::Event::ContainerOpen>({who.Id(), x, y, z, block});
    }

    void Server::HookDimensionChange(Session& who, int from, int to) {
        if (plugins->AnyoneListens(abi::Event::DimensionChange))
            plugins->Dispatch<abi::Event::DimensionChange>(
                {who.Id(), static_cast<abi::Dimension>(from), static_cast<abi::Dimension>(to)});
    }

    bool Server::HookServerCommand(Session* from, std::string& line) {
        if (!plugins->AnyoneListens(abi::Event::ServerCommand))
            return false;
        if (plugins->Dispatch<abi::Event::ServerCommand>({from ? from->Id() : 0, line}))
            return true;
        if (const auto v = plugins->AmendedText("line"); v && !v->empty())
            line = *v;
        return false;
    }
}
