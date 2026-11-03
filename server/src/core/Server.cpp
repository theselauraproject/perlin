#include "core/Server.hpp"
#include "inventory/Recipes.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include "protocol/Packets.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace perlin::server {
    namespace {

        // a block name with or without the namespace
        std::string FullBlockName(const std::string& n) {
            return !n.contains(':') ? "minecraft:" + n : n;
        }
    }

    Server::Server(ServerConfig cfg) : config(std::move(cfg)) {
        spawnY = FlatSurfaceY(config);
        InitGameRules();
        tickRateTarget = std::max<uint32_t>(1, config.tickRate);
        loopback = std::make_unique<LoopbackListener>();
        plugins = std::make_unique<PluginHost>(*this);
        mobWorld = std::make_unique<MobWorld>(*this);
    }

    Server::~Server() {
        Stop();
    }

    bool Server::Start(std::string& error) {
        if (started)
            return true;
        if (config.generator != "flat" && config.generator != "vanilla") {
            error = "unknown generator '" + config.generator + "' (\"flat\" or \"vanilla\")";
            return false;
        }
        if (config.transport == "raknet" && !config.loopbackOnly) {
            error = "transport=raknet: this server only speaks NetherNet, set transport=nethernet";
            return false;
        }
        if (config.randomSeed) {
            std::random_device device;
            config.seed = (static_cast<uint64_t>(device()) << 32) ^ device();
            config.randomSeed = false;
            LogInfo("level-seed is empty: random seed " + std::to_string(static_cast<int64_t>(config.seed)));
        }
        if (config.compressionAlgorithm == "snappy") {
            LogWarn("compression-algorithm=snappy is not built; using zlib");
            config.compressionAlgorithm = "zlib";
        }
        if (config.udpPublicBegin != 0 || !config.udpPublicAddress.empty())
            LogWarn("server-udp-ports: the public side of the mapping is not announced to clients (webrtc "
                    "picks the candidate address); forward the same port numbers");
        if (!config.blockNetworkIdsAreHashes) {
            LogWarn("block-network-ids-are-hashes=false is not supported; block ids stay hashes");
            config.blockNetworkIdsAreHashes = true;
        }
        {
            BlockCatalog& catalog = BlockCatalog::Instance();
            std::string catalogError;
            const std::string states = BlockCatalog::FindFile(config.blockDataDir, "block_states.json");
            const std::string names = BlockCatalog::FindFile(config.blockDataDir, "block_destroy.tsv");
            const std::string extra = BlockCatalog::FindFile(config.blockDataDir, "extra_items.txt");
            if (states.empty() || !catalog.Load(states, names, catalogError, extra)) {
                catalog.LoadBuiltin();
                LogWarn(
                    "block data not found" +
                    (catalogError.empty() ? std::string() : " (" + catalogError + ")") +
                    ": using the built-in list of " + std::to_string(catalog.Items().size()) +
                    " blocks; set block_data_dir to a folder with block_states.json and block_destroy.tsv");
            } else {
                LogInfo("block data: " + std::to_string(catalog.StateCount()) + " states, " +
                        std::to_string(catalog.Items().size()) + " items");
            }
            BuildItemPackets();
        Recipes::Instance().Load();
        }
        BackendChoice choice = CreateTerrainBackend(config);
        if (!choice.backend) {
            error = "no terrain backend: " + choice.note;
            return false;
        }
        LogInfo("terrain: " + std::string(choice.backend->Name()) +
                (choice.note.empty() ? "" : " (" + choice.note + ")"));
        {
            // the spawn: the backend's dry land nearest the origin, or the origin on the flat surface
            int x = 0, y = FlatSurfaceY(config), z = 0;
            const auto spawnStart = std::chrono::steady_clock::now();
            if (config.generator == "vanilla" && choice.backend->FindSpawn(x, y, z))
                LogInfo("spawn at " + std::to_string(x) + ", " + std::to_string(y) + ", " +
                        std::to_string(z) + " (" +
                        std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(
                                                            std::chrono::steady_clock::now() - spawnStart)
                                                            .count())) +
                        " ms)");
            spawnX = x;
            spawnY = y;
            spawnZ = z;
        }
        chunks = std::make_unique<ChunkManager>(std::move(choice.backend), config.maxThreads);

        RegisterBuiltins();

        std::string listenError;
        if (!loopback->Start(events, listenError)) {
            error = "loopback: " + listenError;
            return false;
        }
        if (config.nethernet && !config.loopbackOnly) {
            nethernet = CreateNetherNetListener(config, [this] {
                std::ostringstream o;
                o << "{\"name\":\"" << json::Escape(config.name)
                  << "\",\"protocol\":2225,\"version\":\"1.26.60\",\"level\":\""
                  << json::Escape(config.levelName) << "\",\"players\":" << InGamePlayers().size()
                  << ",\"maxPlayers\":" << config.maxPlayers << ",\"gameType\":" << config.gameMode << "}";
                return o.str();
            });
            if (!nethernet) {
                error = "this build has no NetherNet support (rebuild with PERLIN_SERVER_NETHERNET=ON), or "
                        "set loopback_only";
                return false;
            }
            if (!nethernet->Start(events, error))
                return false;
            LogInfo("nethernet signalling on " + config.bindAddress + ":" + std::to_string(config.port));
        }
        {
            const bool persist = !config.loopbackOnly;
            allowList.Load(persist ? AllowlistPath() : std::string());
            banList.Load(persist ? config.dataDir + "/banned-players.json" : std::string());
            const std::string permissionsPath = PermissionsPath();
            const bool fresh = persist && !std::filesystem::exists(permissionsPath);
            permissionList.Load(persist ? permissionsPath : std::string());
            if (fresh) {
                // ops.json from before the bds file names: its entries become operators once
                AccessList legacyOps;
                legacyOps.Load(config.dataDir + "/ops.json");
                for (const AccessEntry& e : legacyOps.Entries())
                    permissionList.SetPermission(e.name, e.xuid, "operator");
                if (!legacyOps.Entries().empty())
                    LogInfo("imported " + std::to_string(legacyOps.Entries().size()) +
                            " operators from ops.json into permissions.json");
            }
            // the old operators key in server.json still counts
            for (const std::string& o : config.operators)
                permissionList.SetPermission(o, std::string(), "operator");
            if (persist) {
                // written out on first run so there is a file to edit
                permissionList.Save();
                allowList.Save();
            }
            if (config.whitelist && !config.loopbackOnly)
                LogInfo("allow-list is on: " + std::to_string(allowList.Entries().size()) + " entries in " +
                        allowList.Path());
        }
        plugins->LoadDirectory(config.pluginDir);
        started = true;
        LogInfo("server '" + config.name + "' ready");
        return true;
    }

    void Server::Stop() {
        if (!started)
            return;
        started = false;
        // searches in flight stop at their next sample
        locateCancel = true;
        for (std::thread& t : locateThreads)
            if (t.joinable())
                t.join();
        locateThreads.clear();
        for (auto& [id, session] : sessions)
            session->Kick("Server closed");
        sessions.clear();
        if (nethernet)
            nethernet->Stop();
        loopback->Stop();
        plugins->UnloadAll();
        chunks.reset();
    }

    void Server::Run() {
        using clock = std::chrono::steady_clock;
        auto next = clock::now();
        while (!stopRequested) {
            Tick();
            next += TickDelay();
            const auto now = clock::now();
            if (next < now) {
                // fell behind: drop the backlog instead of spiralling
                next = now;
            } else {
                std::this_thread::sleep_until(next);
            }
        }
    }

    std::chrono::nanoseconds Server::TickDelay() const {
        if (sprintRemaining > 0 || (frozen && stepRemaining > 0))
            return std::chrono::nanoseconds(0);
        return std::chrono::nanoseconds(static_cast<int64_t>(1'000'000'000.0 / tickRateTarget));
    }

    void Server::Tick() {
        const auto began = std::chrono::steady_clock::now();
        chunks->Pump();
        Pump();
        DrainLocate();
        // frozen: the game clock holds still but the network, chat and commands keep working; a step lets single ticks through
        const bool advance = !frozen || stepRemaining > 0 || sprintRemaining > 0;
        if (advance) {
            tick++;
            if (RuleValue("dodaylightcycle"))
                dayTime++;
            if (weatherTicks > 0 && --weatherTicks == 0 && (raining || thundering))
                SetWeather("clear", 0);
            if (frozen && stepRemaining > 0)
                stepRemaining--;
            TickItems();
            for (const ChunkPos& ready : chunks->TakeReady())
                mobWorld->OnChunkReady(ready.x, ready.z);
            mobWorld->Tick();
            TickContainers();
            if (plugins->AnyoneListens(abi::Event::Tick))
                plugins->Dispatch<abi::Event::Tick>({.dt = 0.05f, .tick = tick});
        }
        // position broadcast: once per tick for players that moved is plenty at 20 Hz
        std::vector<Session*> players = InGamePlayers();
        for (Session* s : players)
            if (s->TakeMoved())
                for (Session* other : players)
                    if (other != s)
                        other->SendMove(*s, 0);
        for (auto& [id, session] : sessions)
            session->Tick(tick);
        ReapClosed();
        if (advance) {
            tickMillis.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
            if (tickMillis.size() > 100)
                tickMillis.pop_front();
            if (sprintRemaining > 0 && --sprintRemaining == 0) {
                const double seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - sprintStarted).count();
                char report[160];
                std::snprintf(report, sizeof report,
                              "Sprint completed with %.1f ticks per second, or %.2f ms per tick",
                              seconds > 0 ? static_cast<double>(sprintTotal) / seconds : 0.0,
                              sprintTotal > 0 ? seconds * 1000.0 / static_cast<double>(sprintTotal) : 0.0);
                Broadcast(report);
            }
        }
    }

    std::string Server::TickQuery() const {
        char line[200];
        std::string out = sprintRemaining > 0 ? "The game is sprinting"
                          : frozen            ? "The game is frozen"
                                              : "The game is running normally";
        std::snprintf(line, sizeof line, "\nTarget tick rate: %.1f per second.", tickRateTarget);
        out += line;
        if (!tickMillis.empty()) {
            std::vector<double> sorted(tickMillis.begin(), tickMillis.end());
            std::sort(sorted.begin(), sorted.end());
            double sum = 0;
            for (double v : sorted)
                sum += v;
            const auto pct = [&](double p) {
                return sorted[std::min(sorted.size() - 1,
                                       static_cast<size_t>(p * static_cast<double>(sorted.size())))];
            };
            std::snprintf(line, sizeof line, "\nAverage time per tick: %.1fms (Target: %.1fms)",
                          sum / static_cast<double>(sorted.size()), 1000.0 / tickRateTarget);
            out += line;
            std::snprintf(line, sizeof line,
                          "\nPercentiles: P50: %.1fms P95: %.1fms P99: %.1fms, sample: %zu", pct(0.5),
                          pct(0.95), pct(0.99), sorted.size());
            out += line;
        }
        return out;
    }

    void Server::RunTickCommand(Session* from, const std::string& args) {
        const auto reply = [&](const std::string& text) {
            if (from)
                from->SendRaw(text);
            else
                LogInfo(text);
        };
        std::istringstream in(args);
        std::string sub, arg;
        in >> sub >> arg;
        sub = str::Lower(sub);
        // a time is ticks, or a number with t (ticks), s (seconds) or d (days) after it
        const auto parseTime = [](std::string text, int64_t& ticks) {
            if (text.empty())
                return false;
            double unit = 1;
            const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
            if (suffix == 't' || suffix == 's' || suffix == 'd') {
                unit = suffix == 's' ? 20.0 : suffix == 'd' ? 24000.0 : 1.0;
                text.pop_back();
            }
            try {
                size_t used = 0;
                const double v = std::stod(text, &used);
                if (used != text.size() || v < 1 || v * unit > 10'000'000)
                    return false;
                ticks = static_cast<int64_t>(v * unit);
                return true;
            } catch (...) {
                return false;
            }
        };
        if (sub == "query") {
            reply(TickQuery());
        } else if (sub == "rate") {
            try {
                size_t used = 0;
                const double v = std::stod(arg, &used);
                if (arg.empty() || used != arg.size() || v < 1.0 || v > 10000.0)
                    throw std::invalid_argument("range");
                tickRateTarget = v;
                char text[96];
                std::snprintf(text, sizeof text, "Set the target tick rate to %.1f per second", v);
                reply(text);
            } catch (...) {
                reply("§cUsage: /tick rate <1.0 - 10000.0>");
            }
        } else if (sub == "freeze") {
            frozen = true;
            reply("The game is frozen");
        } else if (sub == "unfreeze") {
            frozen = false;
            stepRemaining = 0;
            reply("The game is running normally");
        } else if (sub == "step") {
            if (arg == "stop") {
                if (stepRemaining > 0) {
                    stepRemaining = 0;
                    reply("Stopped tick stepping");
                } else {
                    reply("§cNo tick stepping in progress");
                }
                return;
            }
            int64_t n = 1;
            if (!arg.empty() && !parseTime(arg, n)) {
                reply("§cUsage: /tick step [time]");
                return;
            }
            if (!frozen) {
                reply("§cCannot step while the game is running; use /tick freeze first");
                return;
            }
            stepRemaining = n;
            reply("Stepping " + std::to_string(n) + " tick(s)");
        } else if (sub == "sprint") {
            if (arg == "stop") {
                if (sprintRemaining > 0) {
                    sprintRemaining = 0;
                    reply("Interrupted the current tick sprint");
                } else {
                    reply("§cNo tick sprint in progress");
                }
                return;
            }
            int64_t n = 0;
            if (!parseTime(arg, n)) {
                reply("§cUsage: /tick sprint <time|stop>");
                return;
            }
            sprintRemaining = sprintTotal = n;
            sprintStarted = std::chrono::steady_clock::now();
            reply("Sprinting " + std::to_string(n) + " tick(s)");
        } else {
            reply("§cUsage: /tick <query|rate|freeze|unfreeze|step|sprint>");
        }
    }

    void Server::SpawnItem(const ItemStack& stack, double x, double y, double z, double vx, double vy,
                           double vz, int pickupDelay) {
        if (stack.Empty())
            return;
        ItemEntity item;
        item.id = nextEntityId++;
        item.stack = stack;
        item.stack.stackId = 0;
        item.x = x;
        item.y = y;
        item.z = z;
        item.vx = vx;
        item.vy = vy;
        item.vz = vz;
        item.pickupDelay = pickupDelay;
        itemEntities.push_back(item);
        for (Session* s : InGamePlayers())
            s->ShowItem(itemEntities.back());
    }

    void Server::DropFromPlayer(Session& by, const ItemStack& stack) {
        // thrown from the eyes, the way the game does: a short arc in the direction the player faces
        const double yaw = static_cast<double>(by.Yaw()) * 3.14159265358979323846 / 180.0;
        const double dx = -std::sin(yaw), dz = std::cos(yaw);
        SpawnItem(stack, by.X() + dx * 0.3, by.Y() - 0.3, by.Z() + dz * 0.3, dx * 0.3, 0.2, dz * 0.3, 40);
    }

    void Server::TickItems() {
        if (itemEntities.empty())
            return;
        const auto solid = [&](double x, double y, double z) {
            const int bx = static_cast<int>(std::floor(x)), by = static_cast<int>(std::floor(y)),
                      bz = static_cast<int>(std::floor(z));
            if (by < kMinY)
                return true;
            if (!chunks->Peek({bx >> 4, bz >> 4}))
                return true;
            return chunks->GetBlock(bx, by, bz) != BlockRegistry::kAir;
        };
        const std::vector<Session*> players = InGamePlayers();
        for (size_t i = 0; i < itemEntities.size();) {
            ItemEntity& it = itemEntities[i];
            it.age++;
            const double ox = it.x, oy = it.y, oz = it.z;
            // gravity, then drag; resting on a block stops the fall and slows the slide
            it.vy -= 0.04;
            double ny = it.y + it.vy;
            it.onGround = false;
            if (it.vy < 0 && solid(it.x, ny, it.z)) {
                ny = std::floor(ny) + 1.0;
                it.vy = 0;
                it.onGround = true;
            }
            it.y = ny;
            const double nx = it.x + it.vx, nz = it.z + it.vz;
            if (!solid(nx, it.y + 0.1, it.z))
                it.x = nx;
            else
                it.vx = 0;
            if (!solid(it.x, it.y + 0.1, nz))
                it.z = nz;
            else
                it.vz = 0;
            const double friction = it.onGround ? 0.6 * 0.98 : 0.98;
            it.vx *= friction;
            it.vz *= friction;
            it.vy *= 0.98;
            if (it.age > 6000) {
                for (Session* s : players)
                    s->HideItem(it.id);
                itemEntities.erase(itemEntities.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            if (std::abs(it.x - ox) + std::abs(it.y - oy) + std::abs(it.z - oz) > 0.001)
                for (Session* s : players)
                    s->MoveItem(it);
            bool gone = false;
            if (it.age >= it.pickupDelay)
                for (Session* s : players) {
                    // spectators pass through items
                    if (s->Spectator())
                        continue;
                    const double dx = it.x - s->X(), dy = it.y - (s->Y() - 0.9), dz = it.z - s->Z();
                    if (dx * dx + dy * dy + dz * dz > 1.5 * 1.5)
                        continue;
                    const int left = s->Inventory().Give(it.stack.networkId, it.stack.count);
                    if (left == it.stack.count)
                        continue;
                    s->SendInventory();
                    for (Session* o : players)
                        o->TakeItem(it.id, s->Id());
                    if (left == 0) {
                        for (Session* o : players)
                            o->HideItem(it.id);
                        gone = true;
                    } else {
                        // part of the stack fit: the rest stays where it lies
                        it.stack.count = static_cast<uint16_t>(left);
                    }
                    break;
                }
            if (gone)
                itemEntities.erase(itemEntities.begin() + static_cast<std::ptrdiff_t>(i));
            else
                i++;
        }
    }

    void Server::Pump() {
        std::vector<NetEvent> batch;
        events.Drain(batch);
        for (NetEvent& e : batch) {
            switch (e.type) {
            case NetEvent::Type::Open: {
                const uint64_t id = nextRuntimeId++;
                sessions[id] = std::make_unique<Session>(*this, e.connection, id);
                e.connection->id = id;
                break;
            }
            case NetEvent::Type::Message:
                if (const auto it = sessions.find(e.connection->id); it != sessions.end())
                    it->second->OnMessage(e.data);
                break;
            case NetEvent::Type::Close:
                if (const auto it = sessions.find(e.connection->id); it != sessions.end())
                    it->second->Close();
                break;
            }
        }
    }

    void Server::ReapClosed() {
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (it->second->IsClosed())
                it = sessions.erase(it);
            else
                ++it;
        }
    }

    std::vector<Session*> Server::InGamePlayers() {
        std::vector<Session*> out;
        for (auto& [id, s] : sessions)
            if (s->InGame())
                out.push_back(s.get());
        return out;
    }

    Session* Server::FindSession(uint64_t id) {
        const auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second.get();
    }

    Session* Server::FindByName(const std::string& name) {
        const std::string wanted = str::Lower(name);
        for (auto& [id, s] : sessions)
            if (!s->Name().empty() && str::Lower(s->Name()) == wanted)
                return s.get();
        return nullptr;
    }

    std::string Server::AllowlistPath() const {
        // bds: a whitelist.json that exists is used instead of allowlist.json
        const std::string legacy = config.dataDir + "/whitelist.json";
        std::error_code ec;
        return std::filesystem::exists(legacy, ec) ? legacy : config.dataDir + "/allowlist.json";
    }

    std::string Server::PermissionsPath() const {
        return config.dataDir + "/permissions.json";
    }

    std::string Server::PermissionOf(const std::string& name, const std::string& xuid) const {
        if (const AccessEntry* e = permissionList.Find(name, xuid))
            if (e->permission == "operator" || e->permission == "member" || e->permission == "visitor")
                return e->permission;
        return config.defaultPermission;
    }

    bool Server::IgnoresPlayerLimit(const std::string& name, const std::string& xuid) const {
        const AccessEntry* e = allowList.Find(name, xuid);
        return e && e->ignoresPlayerLimit;
    }

    bool Server::IsOperatorName(const std::string& name) const {
        // embedded singleplayer: the one player owns the world
        return config.loopbackOnly || PermissionOf(name, std::string()) == "operator";
    }

    bool Server::IsOperator(const std::string& name, const std::string& xuid) const {
        return config.loopbackOnly || PermissionOf(name, xuid) == "operator";
    }

    std::string Server::SetOperator(const std::string& name, bool op) {
        Session* online = FindByName(name);
        const std::string shown = online ? online->Name() : name;
        const std::string xuid = online ? online->Xuid() : std::string();
        const bool already = PermissionOf(shown, xuid) == "operator";
        // bds: op writes "operator", deop writes "member" (so it also overrides a default of operator)
        permissionList.SetPermission(shown, xuid, op ? "operator" : "member");
        if (online)
            online->SetOperator(op);
        if (op)
            return already ? shown + " is already an operator" : "Opped " + shown;
        return already ? "De-opped " + shown : shown + " is not an operator";
    }

    bool Server::Full() const {
        size_t n = 0;
        for (const auto& [id, s] : sessions)
            if (s->GetState() != Session::State::Closed && !s->Name().empty())
                n++;
        return n > config.maxPlayers;
    }

    void Server::BroadcastArmor(const Session& wearer) {
        for (Session* s : InGamePlayers())
            s->ShowArmor(wearer);
    }

    void Server::OnPlayerJoined(Session& joined) {
        LogInfo(joined.Name() + " joined (" + joined.Conn().Describe() + ")");
        for (Session* other : InGamePlayers()) {
            if (other == &joined)
                continue;
            other->ShowPlayer(joined);
            joined.ShowPlayer(*other);
            other->ShowArmor(joined);
            joined.ShowArmor(*other);
        }
        joined.ShowArmor(joined);
        for (const ItemEntity& item : itemEntities)
            joined.ShowItem(item);
        mobWorld->Show(joined);
        SendWorldState(joined);
        plugins->Dispatch<abi::Event::PlayerJoin>({joined.Id(), joined.Name()});
        Broadcast("§e" + joined.Name() + " joined the game");
    }

    void Server::OnPlayerLeft(Session& left) {
        LogInfo(left.Name() + " left");
        for (Session* other : InGamePlayers())
            if (other != &left)
                other->HidePlayer(left);
        plugins->Dispatch<abi::Event::PlayerQuit>({left.Id(), left.Name()});
        for (Session* other : InGamePlayers())
            if (other != &left)
                other->SendRaw("§e" + left.Name() + " left the game");
    }

    void Server::HandleChat(Session& from, const std::string& message) {
        if (!message.empty() && message[0] == '/') {
            RunPlayerCommand(from, message);
            return;
        }
        if (plugins->Dispatch<abi::Event::PlayerChat>({from.Id(), from.Name(), message}))
            return;
        LogInfo("<" + from.Name() + "> " + message);
        for (Session* s : InGamePlayers())
            s->SendText(1, from.Name(), message);
    }

    namespace {
        // java answers an unknown command, and one you may not use, the same way
        void UnknownCommand(const std::string& name, const std::function<void(const std::string&)>& say) {
            say("§cUnknown or incomplete command, see below for error");
            say("§c/" + name + "<--[HERE]");
        }
    }

    void Server::RunPlayerCommand(Session& from, const std::string& line) {
        RunCommand(&from, line);
    }

    void Server::RunConsoleCommand(const std::string& line) {
        RunCommand(nullptr, line);
    }

    void Server::RunCommand(Session* from, const std::string& line) {
        std::string text = str::Trim(line);
        if (!text.empty() && text[0] == '/')
            text.erase(0, 1);
        if (text.empty())
            return;
        const auto say = [from](const std::string& m) {
            if (from)
                from->SendRaw(m);
            else
                LogInfo(m);
        };
        const size_t space = text.find(' ');
        const std::string name = str::Lower(text.substr(0, space));
        const std::string args = space == std::string::npos ? "" : str::Trim(text.substr(space + 1));
        const auto it = commands.find(name);
        if (it == commands.end() || (it->second.operatorOnly && from && !from->Operator())) {
            UnknownCommand(name, say);
            return;
        }
        const CommandDef& def = it->second;
        if (!def.plugin.empty()) {
            plugins->DispatchCommand(def.plugin, from ? from->Id() : 0, name, args);
            return;
        }
        MatchResult m = MatchCommand(*this, from, name, def.overloads, args);
        if (!m.ok) {
            say("§c" + m.error);
            if (!m.context.empty())
                say("§c" + m.context);
            return;
        }
        CommandCtx ctx{*this, from, args, m.overload, std::move(m.values)};
        def.handler(ctx);
    }
    void Server::Broadcast(const std::string& text) {
        LogInfo(text);
        for (Session* s : InGamePlayers())
            s->SendRaw(text);
    }

    bool Server::Message(uint64_t player, const std::string& text) {
        // player 0 is the console
        if (player == 0) {
            LogInfo(text);
            return true;
        }
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return false;
        s->SendRaw(text);
        return true;
    }

    bool Server::Kick(uint64_t player, const std::string& reason) {
        Session* s = FindSession(player);
        if (!s)
            return false;
        s->Kick(reason);
        return true;
    }

    void Server::RegisterCommand(const std::string& plugin, const std::string& name,
                                 const std::string& description) {
        CommandDef d;
        d.name = str::Lower(name);
        d.description = description;
        d.plugin = plugin;
        commands[d.name] = std::move(d);
    }

    std::vector<PluginApi::PlayerInfo> Server::Players() {
        std::vector<PlayerInfo> out;
        for (Session* s : InGamePlayers())
            out.push_back({s->Id(), s->Name(), s->X(), s->Y() - 1.62, s->Z()});
        return out;
    }

    bool Server::Teleport(uint64_t player, double x, double y, double z) {
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return false;
        s->Teleport(x, y + 1.62, z);
        return true;
    }

    void Server::ApplyBlock(int x, int y, int z, uint16_t block) {
        chunks->SetBlock(x, y, z, block);
        // the cached payload for this column rebuilds on its version bump; players who already have it get the single change now
        const int32_t hash = BlockRegistry::Instance().Hash(block);
        for (Session* s : InGamePlayers())
            if (s->HasSent({x >> 4, z >> 4}))
                s->SendBlockUpdate(x, y, z, hash);
        if (plugins->AnyoneListens(abi::Event::BlockChanged))
            plugins->Dispatch<abi::Event::BlockChanged>(
                {.x = x, .y = y, .z = z, .to = BlockRegistry::Instance().Name(block)});
    }

    void Server::SendBlockTo(Session& to, int x, int y, int z) {
        if (!chunks->Peek({x >> 4, z >> 4}))
            return;
        to.SendBlockUpdate(x, y, z, BlockRegistry::Instance().Hash(chunks->GetBlock(x, y, z)));
    }

    abi::Status Server::SetBlock(int x, int y, int z, const std::string& block) {
        if (y < kMinY || y >= kMinY + kHeight)
            return abi::Status::BadArguments;
        // by name means the default state; a block with no item is not one the catalog can place
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(FullBlockName(block));
        const bool air = FullBlockName(block) == "minecraft:air";
        if ((!item || !item->block) && !air)
            return abi::Status::BadArguments;
        if (!chunks->Peek({x >> 4, z >> 4}))
            return abi::Status::Unavailable;
        ApplyBlock(x, y, z, air ? BlockRegistry::kAir : BlockCatalog::Instance().RegistryId(item->blockHash));
        return abi::Status::Ok;
    }

    namespace {
        // block face index to the neighbour it points at, the order the game numbers faces in
        constexpr int kFaceOffset[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                           {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
    }

    bool Server::PlayerBreak(Session& by, int x, int y, int z) {
        if (y < kMinY || y >= kMinY + kHeight || !by.InReach(x, y, z) || !chunks->Peek({x >> 4, z >> 4})) {
            by.CorrectBlock(x, y, z);
            return false;
        }
        const uint16_t current = chunks->GetBlock(x, y, z);
        if (current == BlockRegistry::kAir) {
            // a second message for a break that already happened: confirm, change nothing
            by.CorrectBlock(x, y, z);
            return true;
        }
        const std::string name = BlockRegistry::Instance().Name(current);
        if (!by.Creative() && name == "minecraft:bedrock") {
            by.CorrectBlock(x, y, z);
            return false;
        }
        if (plugins->AnyoneListens(abi::Event::BlockBreak)) {
            if (plugins->Dispatch<abi::Event::BlockBreak>({by.Id(), x, y, z, name})) {
                by.CorrectBlock(x, y, z);
                return false;
            }
        }
        DropContainer(x, y, z);
        ApplyBlock(x, y, z, BlockRegistry::kAir);
        // survival: the block leaves an item where it stood, to be picked up
        if (!by.Creative())
            if (const CatalogItem* item = BlockCatalog::Instance().ItemByName(name)) {
                ItemStack drop;
                drop.networkId = item->networkId;
                drop.count = 1;
                // a little sideways kick so stacked drops do not sit on one point
                const double kick = static_cast<double>((x * 7 + z * 13 + y * 3) & 7) / 7.0 - 0.5;
                SpawnItem(drop, x + 0.5, y + 0.25, z + 0.5, kick * 0.1, 0.2, -kick * 0.1, 10);
            }
        return true;
    }

    bool Server::PlayerPlace(Session& by, int x, int y, int z, int face) {
        const auto reject = [&](int tx, int ty, int tz) {
            by.CorrectBlock(x, y, z);
            by.CorrectBlock(tx, ty, tz);
            by.SendSlot(by.Inventory().selected);
            return false;
        };
        if (face < 0 || face > 5)
            return false;
        const int tx = x + kFaceOffset[face][0], ty = y + kFaceOffset[face][1], tz = z + kFaceOffset[face][2];
        const ItemStack& held = by.Inventory().Held();
        const CatalogItem* item = held.Empty() ? nullptr : BlockCatalog::Instance().ItemById(held.networkId);
        if (!item || !item->block || ty < kMinY || ty >= kMinY + kHeight || !by.InReach(tx, ty, tz) ||
            !chunks->Peek({tx >> 4, tz >> 4}) || chunks->GetBlock(tx, ty, tz) != BlockRegistry::kAir)
            return reject(tx, ty, tz);
        if (plugins->AnyoneListens(abi::Event::BlockPlace) &&
            plugins->Dispatch<abi::Event::BlockPlace>({by.Id(), tx, ty, tz, item->name}))
            return reject(tx, ty, tz);
        ApplyBlock(tx, ty, tz, BlockCatalog::Instance().RegistryId(item->blockHash));
        if (!by.Creative()) {
            by.Inventory().TakeHeld(1);
            by.SendSlot(by.Inventory().selected);
        }
        return true;
    }

    std::expected<int, abi::Status> Server::GiveItem(uint64_t player, const std::string& itemName,
                                                     int count) {
        Session* s = FindSession(player);
        if (!s || !s->InGame())
            return std::unexpected(abi::Status::NotFound);
        const CatalogItem* item = BlockCatalog::Instance().ItemByName(FullBlockName(itemName));
        if (!item)
            return std::unexpected(abi::Status::BadArguments);
        const int left = s->Inventory().Give(item->networkId, count);
        s->SendInventory();
        return left;
    }

    namespace {
        struct GroupDef {
            proto2225::CreativeContentPacketGroupsEntryCategory category;
            const char* name;
            // block name fragments that put an item in this group; the first group to match wins
            std::vector<const char*> words;
        };

        const std::vector<GroupDef>& Groups() {
            using C = proto2225::CreativeContentPacketGroupsEntryCategory;
            static const std::vector<GroupDef> groups = {
                {C::Construction, "itemGroup.name.planks", {"_planks"}},
                {C::Construction, "itemGroup.name.stairs", {"_stairs"}},
                {C::Construction, "itemGroup.name.slab", {"_slab"}},
                {C::Construction, "itemGroup.name.walls", {"_wall"}},
                {C::Construction, "itemGroup.name.fence", {"_fence"}},
                {C::Construction, "itemGroup.name.fenceGate", {"_fence_gate"}},
                {C::Construction, "itemGroup.name.door", {"_door"}},
                {C::Construction, "itemGroup.name.trapdoor", {"_trapdoor"}},
                {C::Construction, "itemGroup.name.glass", {"glass"}},
                {C::Construction, "itemGroup.name.wool", {"_wool"}},
                {C::Construction, "itemGroup.name.concrete", {"concrete"}},
                {C::Construction, "itemGroup.name.glazedTerracotta", {"glazed_terracotta"}},
                {C::Construction, "itemGroup.name.stainedClay", {"terracotta"}},
                {C::Construction, "itemGroup.name.copper", {"copper"}},
                {C::Construction,
                 "itemGroup.name.stoneBrick",
                 {"brick", "stone", "cobble", "deepslate", "tuff", "blackstone", "basalt", "andesite",
                  "diorite", "granite", "prismarine", "purpur", "quartz", "sandstone"}},
                {C::Nature, "itemGroup.name.log", {"_log", "_wood", "_stem", "_hyphae"}},
                {C::Nature, "itemGroup.name.leaves", {"_leaves", "azalea_leaves"}},
                {C::Nature, "itemGroup.name.sapling", {"sapling", "propagule"}},
                {C::Nature,
                 "itemGroup.name.flower",
                 {"flower", "tulip", "orchid", "allium", "azure", "daisy", "cornflower", "lily", "rose",
                  "peony", "lilac", "dandelion", "poppy", "sunflower"}},
                {C::Nature, "itemGroup.name.ore", {"_ore", "raw_", "ancient_debris"}},
                {C::Nature,
                 "itemGroup.name.mineral",
                 {"coal_block", "iron_block", "gold_block", "diamond_block", "emerald_block", "lapis_block",
                  "netherite_block", "redstone_block"}},
                {C::Nature, "itemGroup.name.sand", {"sand", "gravel"}},
                {C::Nature,
                 "itemGroup.name.dirt",
                 {"dirt", "grass", "mycelium", "podzol", "farmland", "mud", "clay", "moss", "soul_soil",
                  "path"}},
                {C::Nature, "itemGroup.name.ice", {"ice", "snow"}},
                {C::Nature, "itemGroup.name.mushroom", {"mushroom"}},
                {C::Nature, "itemGroup.name.coral", {"coral"}},
                {C::Nature,
                 "itemGroup.name.seed",
                 {"vine", "kelp", "seagrass", "fern", "bush", "cactus", "bamboo", "sugar_cane", "pumpkin",
                  "melon", "hay", "crop", "wheat", "carrots", "potatoes", "beetroot"}},
                {C::Nature,
                 "itemGroup.name.netherWartBlock",
                 {"nether", "warped", "crimson", "shroomlight", "glowstone", "soul_"}},
                {C::Items, "itemGroup.name.rail", {"rail"}},
                {C::Items, "itemGroup.name.chest", {"chest", "barrel", "shulker"}},
                {C::Items,
                 "itemGroup.name.lamp",
                 {"lamp", "lantern", "torch", "candle", "campfire", "froglight"}},
                {C::Items, "itemGroup.name.sign", {"_sign"}},
                {C::Items, "itemGroup.name.bed", {"_bed"}},
                {C::Items, "itemGroup.name.banner", {"banner", "carpet"}},
                {C::Items,
                 "itemGroup.name.redstone",
                 {"redstone", "piston", "dispenser", "dropper", "observer", "repeater", "comparator", "lever",
                  "button", "pressure_plate", "hopper", "tnt", "target", "sculk"}},
            };
            return groups;
        }
    }

    void Server::BuildItemPackets() {
        const BlockCatalog& catalog = BlockCatalog::Instance();
        proto2225::ItemRegistryPacket registry;
        proto2225::CreativeContentPacket creative;
        const std::vector<GroupDef>& defs = Groups();
        // one extra catch-all group at the end for blocks no fragment matched
        std::vector<proto2225::CreativeContentPacketGroupsEntry> groups(defs.size() + 1);
        for (size_t i = 0; i < defs.size(); i++) {
            groups[i].category = defs[i].category;
            groups[i].name = defs[i].name;
        }
        groups.back().category = proto2225::CreativeContentPacketGroupsEntryCategory::Items;
        groups.back().name = "itemGroup.name.miscFood";

        struct Placed {
            proto2225::ItemLegacy legacy;
            size_t group;
        };
        std::vector<Placed> placed;
        // two more groups after the catch-all: gear and everything else that is not a block
        groups.resize(defs.size() + 3);
        const size_t equipmentGroup = defs.size() + 1, itemsGroup = defs.size() + 2;
        groups[equipmentGroup].category = proto2225::CreativeContentPacketGroupsEntryCategory::Equipment;
        groups[equipmentGroup].name = "itemGroup.name.sword";
        groups[itemsGroup].category = proto2225::CreativeContentPacketGroupsEntryCategory::Items;
        groups[itemsGroup].name = "itemGroup.name.miscFood";
        for (const CatalogItem& item : catalog.Items()) {
            proto2225::ItemstatesEntry entry;
            entry.name = item.name;
            entry.runtimeId = item.networkId;
            entry.componentBased = false;
            entry.version = proto2225::ItemstatesEntryVersion::Legacy;
            registry.itemstates.push_back(std::move(entry));

            proto2225::ItemLegacy legacy;
            legacy.networkId = item.networkId;
            legacy.count = 1;
            legacy.blockRuntimeId = item.block ? item.blockHash : 0;
            // the wire always carries the extra-data frame; leaving the variant empty writes none and the client cannot decode the packet
            legacy.extra = proto2225::ItemExtraDataWithoutBlockingTick{};
            size_t group = defs.size();
            const std::string shortName = item.name.substr(item.name.find(':') + 1);
            if (!item.block) {
                const auto has = [&](const char* w) { return shortName.contains(w); };
                const bool gear = has("_sword") || has("_pickaxe") || has("_axe") || has("_shovel") ||
                                  has("_hoe") || has("_helmet") || has("_chestplate") || has("_leggings") ||
                                  has("_boots") || shortName == "bow" || shortName == "crossbow" ||
                                  shortName == "shield" || shortName == "trident" || shortName == "mace" ||
                                  has("_horse_armor") || shortName == "elytra" ||
                                  shortName == "turtle_helmet" || has("wolf_armor");
                placed.push_back({legacy, gear ? equipmentGroup : itemsGroup});
                continue;
            }
            for (size_t g = 0; g < defs.size() && group == defs.size(); g++)
                for (const char* w : defs[g].words)
                    if (shortName.contains(w)) {
                        group = g;
                        break;
                    }
            placed.push_back({legacy, group});
        }
        // groups with nothing in them are left out; the client lists a tab per group
        std::vector<int> remap(groups.size(), -1);
        for (const Placed& p : placed)
            if (remap[p.group] < 0) {
                remap[p.group] = static_cast<int>(creative.groups.size());
                groups[p.group].iconItem = p.legacy;
                creative.groups.push_back(groups[p.group]);
            }
        // creative order: by group, the way the vanilla menu lists them
        std::stable_sort(placed.begin(), placed.end(),
                         [&](const Placed& a, const Placed& b) { return remap[a.group] < remap[b.group]; });
        for (const Placed& p : placed) {
            proto2225::CreativeContentPacketItemsEntry e;
            e.entryId = static_cast<uint32_t>(p.legacy.networkId);
            e.item = p.legacy;
            e.groupIndex = static_cast<uint32_t>(remap[p.group]);
            creative.items.push_back(std::move(e));
        }
        net::EncodeChecked(registry, itemRegistryBody);
        net::EncodeChecked(creative, creativeBody);
    }

    abi::Status Server::GetBlock(int x, int y, int z, std::string& block) {
        if (!chunks->Peek({x >> 4, z >> 4}))
            return abi::Status::Unavailable;
        block = BlockRegistry::Instance().Name(chunks->GetBlock(x, y, z));
        return abi::Status::Ok;
    }

    PluginApi::Status Server::GetStatus() {
        Status s;
        s.name = config.name;
        s.players = static_cast<uint32_t>(InGamePlayers().size());
        s.maxPlayers = config.maxPlayers;
        s.tick = tick;
        s.backend = chunks ? chunks->BackendName() : "";
        return s;
    }

    std::string Server::Describe() const {
        return config.name + " (" + std::to_string(sessions.size()) + " connections, " +
               std::to_string(chunks ? chunks->Loaded() : 0) + " chunks)";
    }
}

namespace perlin::server {
    void Server::InitGameRules() {
        struct Def {
            const char* display;
            bool boolean;
            int value;
        };
        static const Def defs[] = {
            {"commandBlockOutput", true, 1},
            {"commandBlocksEnabled", true, 1},
            {"doDaylightCycle", true, 1},
            {"doEntityDrops", true, 1},
            {"doFireTick", true, 1},
            {"doImmediateRespawn", true, 0},
            {"doInsomnia", true, 1},
            {"doLimitedCrafting", true, 0},
            {"doMobLoot", true, 1},
            {"doMobSpawning", true, 1},
            {"doTileDrops", true, 1},
            {"doWeatherCycle", true, 1},
            {"drowningDamage", true, 1},
            {"fallDamage", true, 1},
            {"fireDamage", true, 1},
            {"freezeDamage", true, 1},
            {"functionCommandLimit", false, 10000},
            {"keepInventory", true, 0},
            {"maxCommandChainLength", false, 65535},
            {"mobGriefing", true, 1},
            {"naturalRegeneration", true, 1},
            {"playersSleepingPercentage", false, 100},
            {"projectilesCanBreakBlocks", true, 1},
            {"pvp", true, 1},
            {"randomTickSpeed", false, 1},
            {"recipesUnlock", true, 1},
            {"respawnBlocksExplode", true, 1},
            {"sendCommandFeedback", true, 1},
            {"showBorderEffect", true, 1},
            {"showCoordinates", true, 0},
            {"showDaysPlayed", true, 0},
            {"showDeathMessages", true, 1},
            {"showTags", true, 1},
            {"spawnRadius", false, 10},
            {"tntExplodes", true, 1},
            {"tntExplosionDropDecay", true, 0},
        };
        for (const Def& d : defs)
            gameRules[str::Lower(d.display)] = {d.display, d.boolean, d.value};
    }

    int Server::RuleValue(const std::string& lowerName) const {
        const auto it = gameRules.find(lowerName);
        return it == gameRules.end() ? 0 : it->second.value;
    }

    bool Server::SetRule(const std::string& lowerName, int value) {
        const auto it = gameRules.find(lowerName);
        if (it == gameRules.end())
            return false;
        it->second.value = it->second.boolean ? (value != 0 ? 1 : 0) : value;
        for (Session* s : InGamePlayers())
            s->SendGameRule(lowerName, it->second.boolean, it->second.value);
        return true;
    }

    void Server::SetDayTime(int64_t ticks) {
        dayTime = std::max<int64_t>(0, ticks);
        for (Session* s : InGamePlayers())
            s->SendTime(static_cast<int32_t>(dayTime % 2147483647));
    }

    void Server::SetWeather(const std::string& kind, int durationTicks) {
        raining = kind != "clear";
        thundering = kind == "thunder";
        weatherTicks = durationTicks;
        for (Session* s : InGamePlayers())
            s->SendWeather(raining, thundering);
    }

    void Server::SetWorldSpawn(int x, int y, int z) {
        spawnX = x;
        spawnY = y;
        spawnZ = z;
        for (Session* s : InGamePlayers())
            s->SendSpawnPosition(x, y, z, true);
    }

    void Server::SendWorldState(Session& to) {
        to.SendWeather(raining, thundering);
        for (const auto& [key, rule] : gameRules)
            to.SendGameRule(key, rule.boolean, rule.value);
    }

    void Server::DrainLocate() {
        std::vector<std::pair<uint64_t, std::string>> done;
        {
            std::lock_guard<std::mutex> lock(locateMutex);
            done.swap(locateDone);
        }
        for (auto& [id, text] : done) {
            locating.erase(id);
            if (id == 0) {
                LogInfo(text);
            } else if (Session* s = FindSession(id); s && s->InGame()) {
                s->SendRaw(text);
            }
        }
    }
}
