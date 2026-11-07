#include "core/Commands.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"
#include "world/MobData.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/vanilla/BiomeData.hpp"

#include "protocol/Packets.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace perlin::server {
    namespace {

        std::string Fixed(double v) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.2f", v);
            return buf;
        }

        const char* ModeName(int mode) {
            switch (mode) {
            case 1:
                return "Creative Mode";
            case 2:
                return "Adventure Mode";
            case 6:
                return "Spectator Mode";
            default:
                return "Survival Mode";
            }
        }

        const char* DifficultyName(int d) {
            switch (d) {
            case 0:
                return "Peaceful";
            case 2:
                return "Normal";
            case 3:
                return "Hard";
            default:
                return "Easy";
            }
        }

        void CollectText(const json::Value& v, std::string& out) {
            switch (v.kind) {
            case json::Value::Kind::String:
                out += v.text;
                break;
            case json::Value::Kind::Number:
                out += std::to_string(static_cast<long long>(v.number));
                break;
            case json::Value::Kind::Array:
                for (const json::Value& e : v.items)
                    CollectText(e, out);
                break;
            case json::Value::Kind::Object:
                if (v["text"].IsString())
                    out += v["text"].text;
                else if (v["translate"].IsString())
                    out += v["translate"].text;
                for (const json::Value& e : v["extra"].items)
                    CollectText(e, out);
                break;
            default:
                break;
            }
        }

        std::string PlainText(const std::string& raw) {
            const std::string t = str::Trim(raw);
            if (t.empty() || (t[0] != '{' && t[0] != '[' && t[0] != '"'))
                return t;
            json::Value v;
            if (!json::Parse(t, v))
                return t;
            std::string out;
            CollectText(v, out);
            return out;
        }

        std::string BlockPosText(int x, int y, int z) {
            return std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
        }
    }

    void Server::RegisterBuiltins() {
        const auto say = [](Session* to, const std::string& text) {
            if (to)
                to->SendRaw(text);
            else
                LogInfo(text);
        };
        const auto fail = [say](Session* to, const std::string& text) { say(to, "§c" + text); };

        const auto add = [this](std::vector<const char*> names, const char* description, bool op,
                                std::vector<std::string> notations, std::function<void(CommandCtx&)> fn) {
            std::vector<OverloadSpec> specs;
            for (const std::string& n : notations) {
                OverloadSpec spec;
                std::string error;
                if (!ParseOverload(n, spec, &error)) {
                    LogError(std::string("command ") + names.front() + ": bad overload \"" + n +
                             "\": " + error);
                    continue;
                }
                specs.push_back(std::move(spec));
            }
            for (const char* name : names) {
                CommandDef d;
                d.name = name;
                d.description = description;
                d.operatorOnly = op;
                d.overloads = specs;
                d.handler = fn;
                commands[name] = std::move(d);
            }
        };

        add({"help", "?"}, "List the commands you can use, or how to use one", false,
            {"", "<command:string>"}, [this, say, fail](CommandCtx& c) {
                Session* from = c.source;
                const auto usable = [&](const CommandDef& d) {
                    return !d.operatorOnly || !from || from->Operator();
                };
                if (c.Has("command")) {
                    const std::string name =
                        str::Lower(c["command"].text.empty() || c["command"].text[0] != '/'
                                       ? c["command"].text
                                       : c["command"].text.substr(1));
                    const auto it = commands.find(name);
                    if (it == commands.end() || !usable(it->second)) {
                        fail(from, "Unknown command: " + name);
                        return;
                    }
                    if (it->second.overloads.empty())
                        say(from, "/" + name + " - " + it->second.description);
                    for (const OverloadSpec& o : it->second.overloads)
                        say(from, UsageLine(name, o));
                    return;
                }
                for (const auto& [name, d] : commands) {
                    if (!usable(d))
                        continue;
                    say(from, d.overloads.empty() ? "/" + name : UsageLine(name, d.overloads.front()));
                }
            });
        add({"list"}, "Show who is online", false, {"", "uuids"}, [this, say](CommandCtx& c) {
            std::string names;
            const auto players = InGamePlayers();
            for (Session* p : players)
                names += (names.empty() ? "" : ", ") + p->Name();
            say(c.source, "There are " + std::to_string(players.size()) + " of a max of " +
                              std::to_string(config.maxPlayers) + " players online: " + names);
        });
        add({"plugins", "pl"}, "List loaded plugins", false, {""}, [this, say](CommandCtx& c) {
            std::string out = "Plugins:";
            for (const auto& p : plugins->List())
                out += "\n" + p.name + (p.version.empty() ? "" : " " + p.version) +
                       (p.enabled ? "" : " (disabled: " + p.error + ")");
            say(c.source, out);
        });
        add({"seed"}, "Show the world seed", true, {""}, [this, say](CommandCtx& c) {
            say(c.source, "Seed: [" + std::to_string(static_cast<int64_t>(config.seed)) + "]");
        });

        add({"say"}, "Broadcast a message to everyone", true, {"<message:text>"}, [this](CommandCtx& c) {
            Broadcast("[" + (c.source ? c.source->Name() : std::string("Server")) + "] " + c["message"].text);
        });
        add({"me"}, "Show a message about yourself", false, {"<action:text>"}, [this](CommandCtx& c) {
            Broadcast("* " + (c.source ? c.source->Name() : std::string("Server")) + " " + c["action"].text);
        });
        add({"tell", "msg", "w"}, "Send a private message", false, {"<targets:targets> <message:text>"},
            [say](CommandCtx& c) {
                const std::string from = c.source ? c.source->Name() : "Server";
                for (Session* s : c["targets"].players) {
                    say(c.source, "§7You whisper to " + s->Name() + ": " + c["message"].text);
                    s->SendRaw("§7" + from + " whispers to you: " + c["message"].text);
                }
            });
        add({"tellraw"}, "Send a message to players", true, {"<targets:targets> <message:text>"},
            [](CommandCtx& c) {
                const std::string text = PlainText(c["message"].text);
                for (Session* s : c["targets"].players)
                    s->SendRaw(text);
            });
        add({"title"}, "Show a title, subtitle or action bar message", true,
            {"<targets:targets> <kind:enum(title|subtitle|actionbar)> <text:text>",
             "<targets:targets> <kind:enum(clear|reset)>",
             "<targets:targets> times <fadeIn:int> <stay:int> <fadeOut:int>"},
            [say](CommandCtx& c) {
                const std::string kind = c.Has("kind") ? c["kind"].text : "times";
                for (Session* s : c["targets"].players) {
                    if (kind == "clear")
                        s->SendTitle(0, "");
                    else if (kind == "reset")
                        s->SendTitle(1, "");
                    else if (kind == "title")
                        s->SendTitle(2, PlainText(c["text"].text));
                    else if (kind == "subtitle")
                        s->SendTitle(3, PlainText(c["text"].text));
                    else if (kind == "actionbar")
                        s->SendTitle(4, PlainText(c["text"].text));
                    else
                        s->SendTitle(5, "", static_cast<int>(c["fadeIn"].integer),
                                     static_cast<int>(c["stay"].integer),
                                     static_cast<int>(c["fadeOut"].integer));
                }
                const size_t n = c["targets"].players.size();
                say(c.source, kind == "times" ? "Set the title times for " + std::to_string(n) + " player(s)"
                                              : (kind == "clear" || kind == "reset"
                                                     ? std::string(kind == "clear" ? "Cleared" : "Reset") +
                                                           " the titles of "
                                                     : "Showed the " + kind + " to ") +
                                                    std::to_string(n) + " player(s)");
            });

        add({"teleport", "tp"}, "Teleport entities", true,
            {"<destination:target>", "<location:pos>", "<targets:targets> <destination:target>",
             "<targets:targets> <location:pos> [yaw:float] [pitch:float]"},
            [this, say, fail](CommandCtx& c) {
                std::vector<Session*> who = c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "Only a player can teleport themselves; use /tp <targets> "
                                      "<destination> from the console");
                        return;
                    }
                    who.push_back(c.source);
                }
                if (c.Has("destination")) {
                    Session* to = c["destination"].players.front();
                    for (Session* s : who) {
                        if (s == to)
                            continue;
                        if (s->DimensionId() != to->DimensionId()) {
                            ChangeDimension(*s, to->DimensionId(), to->X(), to->Y(), to->Z());
                            say(c.source, "Teleported " + s->Name() + " to " + to->Name());
                            continue;
                        }
                        s->Teleport(to->X(), to->Y(), to->Z());
                        say(c.source, "Teleported " + s->Name() + " to " + to->Name());
                    }
                    return;
                }
                const auto& p = c["location"].pos;
                const int dim = CommandDimension(c.source);
                for (Session* s : who) {
                    if (s->DimensionId() != dim) {
                        ChangeDimension(*s, dim, p[0], p[1] + 1.62, p[2]);
                        say(c.source, "Teleported " + s->Name() + " to " + Fixed(p[0]) + ", " + Fixed(p[1]) + ", " + Fixed(p[2]));
                        continue;
                    }
                    if (c.Has("yaw"))
                        s->Teleport(p[0], p[1] + 1.62, p[2], static_cast<float>(c["yaw"].number),
                                    static_cast<float>(c["pitch"].number));
                    else
                        s->Teleport(p[0], p[1] + 1.62, p[2]);
                    say(c.source, "Teleported " + s->Name() + " to " + Fixed(p[0]) + ", " + Fixed(p[1]) +
                                      ", " + Fixed(p[2]));
                }
            });

        add({"kick"}, "Kick a player off the server", true, {"<targets:targets> [reason:text]"},
            [say](CommandCtx& c) {
                const std::string reason = c.Has("reason") ? c["reason"].text : "Kicked by an operator";
                for (Session* s : c["targets"].players) {
                    const std::string name = s->Name();
                    s->Kick(reason);
                    say(c.source, "Kicked " + name + ": " + reason);
                }
            });
        add({"op"}, "Make a player a server operator", true, {"<targets:profiles>"},
            [this, say](CommandCtx& c) {
                for (const std::string& n : c["targets"].names)
                    say(c.source, SetOperator(n, true));
            });
        add({"deop"}, "Remove a player's operator status", true, {"<targets:profiles>"},
            [this, say](CommandCtx& c) {
                for (const std::string& n : c["targets"].names)
                    say(c.source, SetOperator(n, false));
            });
        add({"gamemode"}, "Set a player's game mode", true,
            {"<mode:enum(survival|creative|adventure|spectator)> [target:targets]"}, [say](CommandCtx& c) {
                const std::string m = c["mode"].text;
                const int value = m == "survival" ? 0 : m == "creative" ? 1 : m == "adventure" ? 2 : 6;
                std::vector<Session*> list = c.Has("target") ? c["target"].players : std::vector<Session*>{};
                if (!c.Has("target")) {
                    if (!c.source) {
                        say(nullptr,
                            "§cThe console has no game mode; name a player: /gamemode <mode> <target>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    s->SetGameMode(value);
                    if (s != c.source)
                        s->SendRaw(std::string("Your game mode has been updated to ") + ModeName(value));
                    say(c.source, s == c.source ? std::string("Set own game mode to ") + ModeName(value)
                                                : "Set " + s->Name() + "'s game mode to " + ModeName(value));
                }
            });
        add({"defaultgamemode"}, "Set the game mode new players start in", true,
            {"<mode:enum(survival|creative|adventure|spectator)>"}, [this, say](CommandCtx& c) {
                const std::string m = c["mode"].text;
                config.gameMode = m == "survival" ? 0 : m == "creative" ? 1 : m == "adventure" ? 2 : 6;
                say(c.source, std::string("The default game mode is now ") + ModeName(config.gameMode));
            });
        add({"flyspeed"}, "Change your spectator flying speed (1x, 2x, 5x, 10x or 20x)", false,
            {"", "<speed:string>"}, [fail](CommandCtx& c) {
                if (!c.source) {
                    fail(nullptr, "Only a player in spectator mode has a flying speed to change");
                    return;
                }
                Session* from = c.source;
                if (!from->Spectator()) {
                    from->SendActionBar("Flying speed can only be changed in spectator mode");
                    return;
                }
                if (!c.Has("speed")) {
                    from->SendActionBar("Flying speed: " + std::to_string(from->FlySpeedLevel()) + "x");
                    return;
                }
                std::string text = c["speed"].text;
                if (!text.empty() && (text.back() == 'x' || text.back() == 'X'))
                    text.pop_back();
                const int level = std::atoi(text.c_str());
                if (!from->SetFlySpeedLevel(level)) {
                    fail(from, "Flying speed must be 1x, 2x, 5x, 10x or 20x");
                    return;
                }
                from->SendActionBar("Flying speed: " + std::to_string(level) + "x");
            });
        add({"give"}, "Give items to players", true, {"<targets:targets> <item:item> [count:int]"},
            [say, fail](CommandCtx& c) {
                const int64_t count = c.Has("count") ? c["count"].integer : 1;
                if (count < 1) {
                    fail(c.source, "Integer must not be less than 1, found " + std::to_string(count));
                    return;
                }
                const int stack = static_cast<int>(std::min<int64_t>(count, 2304));
                for (Session* s : c["targets"].players) {
                    const int left = s->Inventory().Give(static_cast<int16_t>(c["item"].index), stack);
                    s->SendInventory();
                    const std::string shown = c["item"].text.substr(c["item"].text.find(':') + 1);
                    say(c.source, "Gave " + std::to_string(stack - left) + " " + shown + " to " + s->Name() +
                                      (left ? " (inventory full)" : ""));
                }
            });
        std::string mobNames;
        for (int i = 0; i < kMobDefCount; i++) {
            const std::string id = kMobDefs[i].id;
            if (!mobNames.empty())
                mobNames += '|';
            mobNames += id.substr(id.find(':') + 1);
        }
        add({"summon"}, "Summons an entity", true,
            {"<entityType:enum(" + mobNames + ")>", "<entityType:enum(" + mobNames + ")> <spawnPos:pos>"},
            [this, say, fail](CommandCtx& c) {
                double x = SpawnX() + 0.5, y = SpawnY(), z = SpawnZ() + 0.5;
                if (c.source) {
                    x = c.source->X();
                    y = c.source->Y() - 1.62;
                    z = c.source->Z();
                }
                if (c.Has("spawnPos")) {
                    x = c["spawnPos"].pos[0];
                    y = c["spawnPos"].pos[1];
                    z = c["spawnPos"].pos[2];
                }
                Mob* m = Mobs().Spawn("minecraft:" + c["entityType"].text, x, y, z, true);
                if (!m) {
                    fail(c.source, "Unable to summon entity");
                    return;
                }
                say(c.source, "Summoned new " + c["entityType"].text);
            });
        add({"kill"}, "Kills entities (players, mobs, etc.)", true, {"", "<selector:text>"},
            [this, say, fail](CommandCtx& c) {
                if (c.Has("selector")) {
                    const std::string sel = c["selector"].text;
                    if (!sel.starts_with("@e")) {
                        std::vector<Session*> list;
                        std::string error;
                        if (!ResolveSelector(*this, c.source, sel, list, error) || list.empty()) {
                            fail(c.source, error.empty() ? "No player was found" : error);
                            return;
                        }
                        for (Session* s : list) {
                            s->Kill();
                            say(c.source, "Killed " + s->Name());
                        }
                        return;
                    }
                    std::string type;
                    if (const size_t at = sel.find("type="); at != std::string::npos) {
                        type = sel.substr(at + 5);
                        type = type.substr(0, type.find_first_of(",]"));
                        if (type.starts_with("minecraft:"))
                            type = type.substr(10);
                    }
                    const int n = Mobs().KillAll(type);
                    if (n == 0)
                        fail(c.source, "No entity was found");
                    else
                        say(c.source, "Killed " + std::to_string(n) + " entities");
                    return;
                }
                std::vector<Session*> list;
                if (c.source)
                    list.push_back(c.source);
                for (Session* s : list) {
                    s->Kill();
                    say(c.source, "Killed " + s->Name());
                }
            });
        add({"xp", "experience"}, "Adds or removes player experience", true,
            {"<amount:string>", "<amount:string> <player:targets>"}, [say, fail](CommandCtx& c) {
                std::string text = c["amount"].text;
                const bool levels = !text.empty() && (text.back() == 'L' || text.back() == 'l');
                if (levels)
                    text.pop_back();
                char* end = nullptr;
                const long amount = std::strtol(text.c_str(), &end, 10);
                if (text.empty() || !end || *end) {
                    fail(c.source, "Expected a number, optionally followed by L: " + c["amount"].text);
                    return;
                }
                std::vector<Session*> list = c.Has("player") ? c["player"].players : std::vector<Session*>{};
                if (!c.Has("player")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no experience; name a player: /xp <amount> <player>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    if (levels) {
                        s->AddLevels(static_cast<int>(amount));
                        say(c.source, (amount >= 0 ? "Gave " : "Took ") + std::to_string(std::labs(amount)) +
                                          " levels " + (amount >= 0 ? "to " : "from ") + s->Name());
                    } else {
                        if (amount < 0) {
                            fail(c.source, "Cannot remove experience points; use levels, for example /xp -5L");
                            return;
                        }
                        s->AddExperience(static_cast<int>(amount));
                        say(c.source, "Gave " + std::to_string(amount) + " experience to " + s->Name());
                    }
                }
            });
        {
            static const char* const kEffects[] = {
                "", "speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage",
                "jump_boost", "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing",
                "invisibility", "blindness", "night_vision", "hunger", "weakness", "poison", "wither", "health_boost",
                "absorption", "saturation", "levitation", "fatal_poison", "conduit_power", "slow_falling", "bad_omen",
                "village_hero", "darkness"};
            std::string names = "clear";
            for (size_t i = 1; i < std::size(kEffects); i++)
                names += std::string("|") + kEffects[i];
            add({"effect"}, "Add or remove status effects", true,
                {"<player:targets> <effect:enum(" + names + ")>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int> <amplifier:int>",
                 "<player:targets> <effect:enum(" + names + ")> <seconds:int> <amplifier:int> <hideParticles:enum(true|false)>"},
                [say, fail](CommandCtx& c) {
                    const std::string effect = c["effect"].text;
                    int id = 0;
                    for (size_t i = 1; i < std::size(kEffects); i++)
                        if (effect == kEffects[i])
                            id = static_cast<int>(i);
                    for (Session* s : c["player"].players) {
                        if (effect == "clear") {
                            s->ClearEffects();
                            say(c.source, "Took all effects from " + s->Name());
                            continue;
                        }
                        const int64_t seconds = c.Has("seconds") ? c["seconds"].integer : 30;
                        const int64_t amplifier = c.Has("amplifier") ? c["amplifier"].integer : 0;
                        if (amplifier < 0 || amplifier > 255) {
                            fail(c.source, "The amplifier must be between 0 and 255");
                            return;
                        }
                        if (seconds <= 0) {
                            if (s->RemoveEffect(id))
                                say(c.source, "Took " + effect + " from " + s->Name());
                            else
                                fail(c.source, s->Name() + " does not have " + effect);
                            continue;
                        }
                        const bool hide = c.Has("hideParticles") && c["hideParticles"].text == "true";
                        s->AddEffect(id, static_cast<int>(amplifier),
                                     static_cast<int>(std::min<int64_t>(seconds, 1000000) * 20), !hide);
                        say(c.source, "Gave " + effect + " * " + std::to_string(amplifier) + " to " + s->Name() +
                                          " for " + std::to_string(seconds) + " seconds");
                    }
                });
        }
        add({"clear"}, "Remove items from player inventories", true,
            {"", "<targets:targets>", "<targets:targets> <item:item>",
             "<targets:targets> <item:item> <maxCount:int>"},
            [say, fail](CommandCtx& c) {
                std::vector<Session*> list =
                    c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no inventory; name a player: /clear <targets>");
                        return;
                    }
                    list.push_back(c.source);
                }
                const bool filtered = c.Has("item");
                const int16_t wanted = filtered ? static_cast<int16_t>(c["item"].index) : 0;
                const int64_t limit = c.Has("maxCount") ? c["maxCount"].integer : -1;
                for (Session* s : list) {
                    int64_t removed = 0;
                    const auto sweep = [&](ItemStack& slot) {
                        if (slot.Empty() || (filtered && slot.networkId != wanted))
                            return;
                        const int64_t take =
                            limit < 0 ? slot.count
                                      : std::min<int64_t>(slot.count, std::max<int64_t>(0, limit - removed));
                        removed += take;
                        if (limit != 0 && take > 0)
                            slot.count = static_cast<decltype(slot.count)>(slot.count - take);
                        if (slot.count == 0)
                            slot = ItemStack{};
                    };
                    for (ItemStack& slot : s->Inventory().main)
                        sweep(slot);
                    for (ItemStack& slot : s->Inventory().armor)
                        sweep(slot);
                    sweep(s->Inventory().offhand);
                    if (limit != 0 && removed > 0)
                        s->SendInventory();
                    if (removed == 0)
                        fail(c.source, "No items were found on player " + s->Name());
                    else if (limit == 0)
                        say(c.source,
                            "Found " + std::to_string(removed) + " matching item(s) on player " + s->Name());
                    else
                        say(c.source,
                            "Removed " + std::to_string(removed) + " item(s) from player " + s->Name());
                }
            });

        const auto allowlistCommand = [this, say](CommandCtx& c) {
            const std::string action = str::Lower(c.raw.substr(0, c.raw.find(' ')));
            const auto persist = [this] {
                if (config.loopbackOnly)
                    return;
                std::error_code ec;
                const std::string legacy = config.dataDir + "/server.json";
                if (!std::filesystem::exists(config.dataDir + "/server.properties", ec) &&
                    std::filesystem::exists(legacy, ec))
                    config.Save(legacy);
                else
                    config.SaveProperties(config.dataDir + "/server.properties");
            };
            if (action == "on" || action == "off") {
                config.whitelist = action == "on";
                persist();
                say(c.source,
                    std::string("The allowlist is now ") + (config.whitelist ? "turned on" : "turned off"));
            } else if (action == "list") {
                std::string out =
                    "There are " + std::to_string(allowList.Entries().size()) + " allowlisted player(s)";
                if (!allowList.Entries().empty())
                    out += ": ";
                for (size_t i = 0; i < allowList.Entries().size(); i++) {
                    const AccessEntry& e = allowList.Entries()[i];
                    out += (i ? ", " : "") + (e.name.empty() ? e.xuid : e.name);
                }
                say(c.source, out);
            } else if (action == "reload") {
                allowList.Load(AllowlistPath());
                say(c.source, "Reloaded the allowlist");
            } else if (action == "add") {
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool added =
                        allowList.Add(online ? online->Name() : who, online ? online->Xuid() : std::string());
                    say(c.source,
                        added ? "Added " + who + " to the allowlist" : "Player is already allowlisted");
                }
            } else if (action == "remove") {
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool removed =
                        allowList.Remove(who) || (online && allowList.Remove(online->Xuid()));
                    say(c.source,
                        removed ? "Removed " + who + " from the allowlist" : "Player is not allowlisted");
                }
            }
        };
        const std::vector<std::string> allowOverloads = {
            "on", "off", "list", "reload", "add <targets:profiles>", "remove <targets:profiles>"};
        add({"allowlist", "whitelist"}, "Manage the allowlist", true, allowOverloads, allowlistCommand);
        add({"permission"}, "List or reload permissions.json", true, {"list", "reload"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0) {
                    std::string out = "Permissions (default " + config.defaultPermission + "), " +
                                      std::to_string(permissionList.Entries().size()) + " entries:";
                    for (const AccessEntry& e : permissionList.Entries())
                        out += "\n" + (e.name.empty() ? e.xuid : e.name) + ": " + e.permission;
                    say(c.source, out);
                } else {
                    permissionList.Load(PermissionsPath());
                    for (Session* s : InGamePlayers())
                        s->SetOperator(IsOperator(s->Name(), s->Xuid()));
                    say(c.source, "Reloaded " + PermissionsPath());
                }
            });
        add({"ban"}, "Ban players from the server", true, {"<targets:profiles> [reason:text]"},
            [this, say](CommandCtx& c) {
                const std::string reason = c.Has("reason") ? c["reason"].text : "Banned by an operator";
                for (const std::string& who : c["targets"].names) {
                    Session* online = FindByName(who);
                    const bool added =
                        banList.Add(online ? online->Name() : who, online ? online->Xuid() : std::string());
                    if (online)
                        online->Kick("You are banned from this server: " + reason);
                    say(c.source, added ? "Banned " + who + ": " + reason
                                        : "Nothing changed. The player is already banned");
                }
            });
        add({"pardon", "unban"}, "Lift a ban", true, {"<targets:profiles>"}, [this, say](CommandCtx& c) {
            for (const std::string& who : c["targets"].names)
                say(c.source,
                    banList.Remove(who) ? "Unbanned " + who : "Nothing changed. The player isn't banned");
        });
        add({"banlist"}, "List the banned players", true, {"", "players"}, [this, say](CommandCtx& c) {
            std::string out = "There are " + std::to_string(banList.Entries().size()) + " ban(s)";
            for (const AccessEntry& e : banList.Entries())
                out += "\n" + (e.name.empty() ? e.xuid : e.name);
            say(c.source, out);
        });

        add({"difficulty"}, "Show or set the difficulty", true,
            {"", "<difficulty:enum(peaceful|easy|normal|hard)>"}, [this, say](CommandCtx& c) {
                if (!c.Has("difficulty")) {
                    say(c.source, std::string("The difficulty is ") + DifficultyName(config.difficulty));
                    return;
                }
                const std::string d = c["difficulty"].text;
                config.difficulty = d == "peaceful" ? 0 : d == "easy" ? 1 : d == "normal" ? 2 : 3;
                for (Session* s : InGamePlayers())
                    s->SendDifficulty(config.difficulty);
                say(c.source,
                    std::string("The difficulty has been set to ") + DifficultyName(config.difficulty));
            });
        add({"changesetting"}, "Change a server setting", true,
            {"allow-cheats <value:enum(true|false)>", "difficulty <value:enum(peaceful|easy|normal|hard)>"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0) {
                    config.allowCheats = c["value"].text == "true";
                    say(c.source, "allow-cheats is now " + c["value"].text);
                } else {
                    const std::string d = c["value"].text;
                    config.difficulty = d == "peaceful" ? 0 : d == "easy" ? 1 : d == "normal" ? 2 : 3;
                    for (Session* s : InGamePlayers())
                        s->SendDifficulty(config.difficulty);
                    say(c.source, "difficulty is now " + d);
                }
            });
        add({"time"}, "Change or query the world time", true,
            {"set <value:enum(day|noon|night|midnight)>", "set <ticks:int>", "add <ticks:int>",
             "query <what:enum(daytime|gametime|day)>"},
            [this, say](CommandCtx& c) {
                if (c.overload == 0 || c.overload == 1) {
                    int64_t t = 0;
                    if (c.overload == 0) {
                        const std::string v = c["value"].text;
                        t = v == "day" ? 1000 : v == "noon" ? 6000 : v == "night" ? 13000 : 18000;
                    } else {
                        t = c["ticks"].integer;
                    }
                    SetDayTime((dayTime / 24000) * 24000 + ((t % 24000) + 24000) % 24000);
                    say(c.source, "Set the time to " + std::to_string(t));
                } else if (c.overload == 2) {
                    SetDayTime(dayTime + c["ticks"].integer);
                    say(c.source, "Set the time to " + std::to_string(dayTime % 24000));
                } else {
                    const std::string w = c["what"].text;
                    if (w == "daytime")
                        say(c.source, "The time is " + std::to_string(dayTime % 24000));
                    else if (w == "gametime")
                        say(c.source, "The time is " + std::to_string(TickCount()));
                    else
                        say(c.source, "The time is " + std::to_string(dayTime / 24000));
                }
            });
        add({"weather"}, "Set the weather", true, {"<kind:enum(clear|rain|thunder)> [duration:int]"},
            [this, say, fail](CommandCtx& c) {
                const std::string kind = c["kind"].text;
                if (c.Has("duration") && (c["duration"].integer < 1 || c["duration"].integer > 1000000)) {
                    fail(c.source,
                         "Integer must not be less than 1, found " + std::to_string(c["duration"].integer));
                    return;
                }
                const int ticks = c.Has("duration") ? static_cast<int>(c["duration"].integer) * 20
                                                    : (kind == "clear" ? 0 : 6000);
                SetWeather(kind, ticks);
                say(c.source, kind == "clear"  ? "Changed the weather to clear"
                              : kind == "rain" ? "Changed the weather to rain"
                                               : "Changed the weather to rain and thunder");
            });
        add({"gamerule"}, "Show or change a game rule", true,
            {"<rule:string>", "<rule:string> <value:string>"}, [this, say, fail](CommandCtx& c) {
                const auto it = gameRules.find(str::Lower(c["rule"].text));
                if (it == gameRules.end()) {
                    fail(c.source, "Incorrect argument for command");
                    fail(c.source, "/gamerule " + c["rule"].text + "<--[HERE]");
                    return;
                }
                const GameRule& rule = it->second;
                const auto shown = [&](int v) {
                    return rule.boolean ? std::string(v ? "true" : "false") : std::to_string(v);
                };
                if (!c.Has("value")) {
                    say(c.source, "Game rule " + rule.display + " is currently set to: " + shown(rule.value));
                    return;
                }
                const std::string v = str::Lower(c["value"].text);
                int value = 0;
                if (rule.boolean) {
                    if (v != "true" && v != "false") {
                        fail(c.source, "Invalid boolean, expected 'true' or 'false' but found '" +
                                           c["value"].text + "'");
                        return;
                    }
                    value = v == "true";
                } else {
                    char* end = nullptr;
                    const long parsed = std::strtol(v.c_str(), &end, 10);
                    if (v.empty() || *end != '\0') {
                        fail(c.source, "Expected integer");
                        return;
                    }
                    value = static_cast<int>(parsed);
                }
                SetRule(it->first, value);
                say(c.source, "Game rule " + rule.display + " has been updated to " + shown(rule.value));
            });
        add({"setworldspawn"}, "Set the world spawn point", true, {"", "<position:blockpos>"},
            [this, say](CommandCtx& c) {
                int x, y, z;
                if (c.Has("position")) {
                    x = static_cast<int>(c["position"].pos[0]);
                    y = static_cast<int>(c["position"].pos[1]);
                    z = static_cast<int>(c["position"].pos[2]);
                } else {
                    x = static_cast<int>(std::floor(c.source ? c.source->X() : SpawnX()));
                    y = static_cast<int>(std::floor(c.source ? c.source->Y() - 1.62 : SpawnY()));
                    z = static_cast<int>(std::floor(c.source ? c.source->Z() : SpawnZ()));
                }
                SetWorldSpawn(x, y, z);
                say(c.source, "Set the world spawn point to " + BlockPosText(x, y, z) + " [0.0]");
            });
        add({"spawnpoint"}, "Set a player's spawn point", true,
            {"", "<targets:targets>", "<targets:targets> <position:blockpos>"}, [say, fail](CommandCtx& c) {
                std::vector<Session*> list =
                    c.Has("targets") ? c["targets"].players : std::vector<Session*>{};
                if (!c.Has("targets")) {
                    if (!c.source) {
                        fail(nullptr, "The console has no spawn point; name a player: /spawnpoint <targets>");
                        return;
                    }
                    list.push_back(c.source);
                }
                for (Session* s : list) {
                    const int x = c.Has("position") ? static_cast<int>(c["position"].pos[0])
                                                    : static_cast<int>(std::floor(s->X()));
                    const int y = c.Has("position") ? static_cast<int>(c["position"].pos[1])
                                                    : static_cast<int>(std::floor(s->Y() - 1.62));
                    const int z = c.Has("position") ? static_cast<int>(c["position"].pos[2])
                                                    : static_cast<int>(std::floor(s->Z()));
                    s->SendSpawnPosition(x, y, z, false);
                    say(c.source, "Set spawn point to " + BlockPosText(x, y, z) + " for " + s->Name());
                }
            });

        const auto loaded = [this](int dim, int x, int z) { return World(dim).Peek({x >> 4, z >> 4}) != nullptr; };
        const auto registryOf = [](const ArgValue& v) -> uint16_t {
            if (v.block >= 0)
                return static_cast<uint16_t>(v.block);
            const CatalogItem* item = BlockCatalog::Instance().ItemById(static_cast<int16_t>(v.index));
            return item ? BlockCatalog::Instance().RegistryId(item->blockHash) : BlockRegistry::kAir;
        };
        add({"setblock"}, "Change a block", true,
            {"<position:blockpos> <block:block> [mode:enum(destroy|keep|replace)]"},
            [this, say, fail, loaded, registryOf](CommandCtx& c) {
                const int x = static_cast<int>(c["position"].pos[0]),
                          y = static_cast<int>(c["position"].pos[1]),
                          z = static_cast<int>(c["position"].pos[2]);
                const int dim = CommandDimension(c.source);
                const DimensionRange range = RangeOf(dim);
                if (y < range.minY || y > range.MaxY()) {
                    fail(c.source, "That position is outside the world");
                    return;
                }
                if (!loaded(dim, x, z)) {
                    fail(c.source, "That position is not loaded");
                    return;
                }
                const uint16_t block = registryOf(c["block"]);
                const std::string mode = c.Has("mode") ? c["mode"].text : "replace";
                if (mode == "keep" && World(dim).GetBlock(x, y, z) != BlockRegistry::kAir) {
                    fail(c.source, "Could not set the block");
                    return;
                }
                ApplyBlock(dim, x, y, z, block);
                say(c.source, "Changed the block at " + BlockPosText(x, y, z));
            });
        add({"fill"}, "Fill a region with a block", true,
            {"<from:blockpos> <to:blockpos> <block:block> [mode:enum(destroy|hollow|keep|outline|replace)]",
             "<from:blockpos> <to:blockpos> <block:block> replace <filter:block>"},
            [this, say, fail, loaded, registryOf](CommandCtx& c) {
                int lo[3], hi[3];
                for (int k = 0; k < 3; k++) {
                    const int a = static_cast<int>(c["from"].pos[static_cast<size_t>(k)]),
                              b = static_cast<int>(c["to"].pos[static_cast<size_t>(k)]);
                    lo[k] = std::min(a, b);
                    hi[k] = std::max(a, b);
                }
                const int64_t volume =
                    static_cast<int64_t>(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
                if (volume > 32768) {
                    fail(c.source, "Too many blocks in the specified area (maximum 32768, specified " +
                                       std::to_string(volume) + ")");
                    return;
                }
                const int dim = CommandDimension(c.source);
                const DimensionRange range = RangeOf(dim);
                if (lo[1] < range.minY || hi[1] > range.MaxY()) {
                    fail(c.source, "That position is outside the world");
                    return;
                }
                for (int x = lo[0] >> 4; x <= hi[0] >> 4; x++)
                    for (int z = lo[2] >> 4; z <= hi[2] >> 4; z++)
                        if (!loaded(dim, x * 16, z * 16)) {
                            fail(c.source, "That position is not loaded");
                            return;
                        }
                const uint16_t block = registryOf(c["block"]);
                const std::string mode =
                    c.overload == 1 ? "replace" : (c.Has("mode") ? c["mode"].text : "replace");
                const bool filtered = c.overload == 1;
                const uint16_t filter = filtered ? registryOf(c["filter"]) : 0;
                int changed = 0;
                for (int x = lo[0]; x <= hi[0]; x++)
                    for (int y = lo[1]; y <= hi[1]; y++)
                        for (int z = lo[2]; z <= hi[2]; z++) {
                            const bool edge = x == lo[0] || x == hi[0] || y == lo[1] || y == hi[1] ||
                                              z == lo[2] || z == hi[2];
                            const uint16_t current = World(dim).GetBlock(x, y, z);
                            uint16_t put = block;
                            if (mode == "keep" && current != BlockRegistry::kAir)
                                continue;
                            if (mode == "outline" && !edge)
                                continue;
                            if (mode == "hollow" && !edge)
                                put = BlockRegistry::kAir;
                            if (filtered && current != filter)
                                continue;
                            if (current == put)
                                continue;
                            ApplyBlock(dim, x, y, z, put);
                            changed++;
                        }
                if (changed == 0)
                    fail(c.source, "No blocks were filled");
                else
                    say(c.source, std::to_string(changed) + " blocks filled");
            });

        add({"locate"}, "Find the nearest biome or structure", true,
            {"biome <biome:biome>", "structure <structure:string>"}, [this, say, fail](CommandCtx& c) {
                if (c.overload == 1) {
                    fail(c.source, "Could not find a structure of type \"" + c["structure"].text +
                                       "\": this server does not generate structures yet");
                    return;
                }
                TerrainBackend& backend = World(CommandDimension(c.source)).Backend();
                if (!backend.HasBiomes()) {
                    fail(c.source, "This world has no biomes to locate (it is a flat world)");
                    return;
                }
                const uint64_t id = c.source ? c.source->Id() : 0;
                if (locating.contains(id)) {
                    fail(c.source, "A search is already running; it answers when it is done");
                    return;
                }
                locating.insert(id);
                const CommandOrigin* origin = ActiveOrigin();
                const int fromX = static_cast<int>(std::floor(origin ? origin->x : c.source ? c.source->X() : SpawnX()));
                const int fromZ = static_cast<int>(std::floor(origin ? origin->z : c.source ? c.source->Z() : SpawnZ()));
                const int biome = c["biome"].index;
                const std::string name = c["biome"].text;
                say(c.source, "Searching for " + name + " ...");
                locateThreads.emplace_back([this, &backend, id, biome, name, fromX, fromZ] {
                    int x = 0, y = 0, z = 0;
                    const bool found = backend.LocateBiome(biome, fromX, fromZ, 6400, locateCancel, x, y, z);
                    if (locateCancel)
                        return;
                    std::string text;
                    if (found) {
                        const int distance = static_cast<int>(std::lround(
                            std::hypot(static_cast<double>(x - fromX), static_cast<double>(z - fromZ))));
                        text = "The nearest " + name + " is at [" + BlockPosText(x, y, z) + "] (" +
                               std::to_string(distance) + " blocks away)";
                    } else {
                        text = "§cCould not find a biome of type \"" + name + "\" within 6400 blocks";
                    }
                    std::lock_guard<std::mutex> lock(locateMutex);
                    locateDone.emplace_back(id, std::move(text));
                });
            });

        add({"execute"}, "Execute another command", true, {"<subcommands:text>"},
            [this](CommandCtx& c) { RunExecute(c.source, c["subcommands"].text); });

        add({"tick"}, "Query or control the game's tick rate", true,
            {"query", "rate <rate:float>", "freeze", "unfreeze", "step", "step <time:string>", "sprint stop",
             "sprint <time:string>"},
            [this](CommandCtx& c) { RunTickCommand(c.source, c.raw); });
        add({"stop"}, "Stop the server", true, {""}, [this, say](CommandCtx& c) {
            say(c.source, "Stopping the server");
            RequestStop();
        });
        add({"save-all"}, "Save the world", true, {"", "flush"}, [say](CommandCtx& c) {
            say(c.source, "Saved the game (this server keeps no world file yet, so nothing was written)");
        });
        add({"save-on"}, "Turn automatic saving on", true, {""},
            [say](CommandCtx& c) { say(c.source, "Automatic saving is already turned on"); });
        add({"save-off"}, "Turn automatic saving off", true, {""},
            [say](CommandCtx& c) { say(c.source, "Automatic saving is already turned off"); });
    }
}
