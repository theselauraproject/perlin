#include "util/Strings.hpp"
#include "client/services/discord/DiscordService.hpp"
#include "util/ServerAddress.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

#define PERLIN_VERSION "1.0.0"

namespace perlin::origd {
    struct Clock {
        using time_point = std::chrono::steady_clock::time_point;
        static inline time_point current{};
        static time_point now() { return current; }
    };
    using TimePoint = Clock::time_point;
    template <class... A> void Log(const char*, const char*, A&&...) {}

    namespace detail {
    inline bool ParseServer(const std::string& raw, std::string& host, uint16_t& port) {
        const std::string_view text = str::TrimView(raw);
        host = text;
        port = 19132;
        std::string_view portText;
        if (text.starts_with('[')) {
            const size_t close = text.find(']');
            if (close == std::string_view::npos)
                return false;
            host = text.substr(1, close - 1);
            if (close + 1 == text.size())
                return !host.empty();
            if (text[close + 1] != ':')
                return false;
            portText = text.substr(close + 2);
        } else {
            const size_t colon = text.rfind(':');

            if (colon == std::string_view::npos || text.find(':') != colon)
                return !host.empty();
            host = text.substr(0, colon);
            portText = text.substr(colon + 1);
        }
        portText = str::TrimView(portText);
        int value = 0;
        const auto [end, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), value);
        if (ec != std::errc() || end == portText.data() || value <= 0 || value > 65535)
            return false;
        port = static_cast<uint16_t>(value);
        return !host.empty();
    }

    }

    struct Events {
        std::vector<discord::Presence> presences;
        std::vector<std::string> joins;
        std::vector<std::string> toasts;
        std::vector<bool> autoAccept;
        int links = 0;
        std::deque<std::string> pendingJoins;
        std::deque<discord::JoinRequest> pendingAccepted;
    };

    struct FakeClient {
        explicit FakeClient(Events* e = nullptr) : events(e) { if (e) e->links++; }
        void Set(std::optional<discord::Presence> p) { if (p) events->presences.push_back(*p); }
        void SetAutoAccept(bool a) { events->autoAccept.push_back(a); }
        std::optional<std::string> TakeJoin() {
            if (events->pendingJoins.empty()) return std::nullopt;
            auto s = events->pendingJoins.front(); events->pendingJoins.pop_front(); return s;
        }
        std::optional<discord::JoinRequest> TakeAccepted() {
            if (events->pendingAccepted.empty()) return std::nullopt;
            auto s = events->pendingAccepted.front(); events->pendingAccepted.pop_front(); return s;
        }
        Events* events;
    };

    struct FakeSession {
        std::string world, player;
        int players = 0;
        std::string WorldName() const { return world; }
        std::string PlayerName() const { return player; }
        std::vector<int> PlayerList() const { return std::vector<int>(static_cast<size_t>(players)); }
    };
    struct Lease { std::string worldName; };
    struct PingEntry { int maxPlayers = 0; };

    struct Orig {
        struct { std::string server; bool discordPresence = false, discordJoin = false, discordShowServer = false; } options;
        struct { bool active = true; } titleScreen;
        struct { bool done = false; } loading;
        struct { std::string title, message, text; TimePoint at; } toast;
        struct { template <class F> void Run(F&& f) { f(); } } tasks;
        std::shared_ptr<FakeSession> session;
        std::shared_ptr<Lease> friendLease;
        bool inSingleplayer = false;
        std::mutex tabMutex, pingMutex;
        std::string realmJoinAddress;
        std::map<std::string, PingEntry> pingResults;
        std::atomic<uint64_t> connectGeneration{0};
        std::unique_ptr<FakeClient> discordRpc;
        TimePoint discordNextUpdate{};
        uint64_t discordGeneration = 0;
        int64_t discordSince = 0;
        Events* events = nullptr;
        void JoinServer(const std::string& a) { events->joins.push_back(a); }
        void UpdateDiscord();
        void HandleDiscordJoin(const std::string& secret);
    };

    namespace {
        constexpr const char* kDiscordApp = "1568359857915039784";
        constexpr const char* kDiscordInvite = "https://discord.gg/pVE3qvXaWb";
        constexpr const char* kJoinPrefix = "perlin1:";

        std::string Plain(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                if (static_cast<unsigned char>(text[i]) == 0xC2 && i + 1 < text.size() &&
                    static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                    i += 2;
                    continue;
                }
                out.push_back(text[i]);
            }
            const size_t a = out.find_first_not_of(" \t\r\n");
            const size_t b = out.find_last_not_of(" \t\r\n");
            return a == std::string::npos ? std::string() : out.substr(a, b - a + 1);
        }

        bool PrivateHost(const std::string& host) {
            std::string h = host;
            std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
            if (h.empty() || h == "localhost" || h.ends_with(".local") || h.ends_with(".lan"))
                return true;
            if (h.find(':') != std::string::npos)
                return h == "::1" || h == "::" || h.starts_with("fe80:") || h.starts_with("fc") || h.starts_with("fd");
            unsigned a = 0, b = 0, c = 0, d = 0;
            char tail = 0;
            if (std::sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4) {
                return a == 10 || a == 127 || a == 0 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
                       (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
            }
            return h.find('.') == std::string::npos && h.find(':') == std::string::npos;
        }

        bool SafeAddress(const std::string& s) {
            return !s.empty() && s.size() <= 260 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']' || c == '_' ||
                       c == '/';
            });
        }

        uint32_t Fnv(const std::string& s) {
            uint32_t h = 2166136261u;
            for (unsigned char c : s)
                h = (h ^ c) * 16777619u;
            return h;
        }
    }

    void Orig::HandleDiscordJoin(const std::string& secret) {
        if (!secret.starts_with(kJoinPrefix))
            return;
        const std::string address = secret.substr(std::char_traits<char>::length(kJoinPrefix));
        std::string host;
        uint16_t port = 0;
        std::string bare = address;
        if (bare.starts_with("https://"))
            bare.erase(0, 8);
        else if (bare.starts_with("http://"))
            bare.erase(0, 7);
        while (!bare.empty() && bare.back() == '/')
            bare.pop_back();
        if (!SafeAddress(address) || !detail::ParseServer(bare, host, port) || PrivateHost(host)) {
            Log("discord", "ignored a Discord join with an unusable address");
            return;
        }
        if (session && !friendLease && !inSingleplayer && options.server == address)
            return;
        Log("discord", "joining {} from Discord", address);
        JoinServer(address);
    }

    void Orig::UpdateDiscord() {
        if (!true)
            return;
        const auto now = Clock::now();
        if (now < discordNextUpdate)
            return;
        discordNextUpdate = now + std::chrono::seconds(1);
        if (!options.discordPresence) {
            discordRpc.reset();
            return;
        }
        if (!discordRpc) {
            discordRpc = std::make_unique<FakeClient>(events);
            (void)0;
        }
        discordRpc->SetAutoAccept(options.discordJoin);
        while (auto secret = discordRpc->TakeJoin())
            HandleDiscordJoin(*secret);
        while (auto request = discordRpc->TakeAccepted()) {
            toast.title = "Discord";
            toast.message = request->username + " is joining you";
            toast.text = toast.title + "\n" + toast.message;
            toast.at = now;
        }

        discord::Presence p;
        p.largeImage = "perlin";
        p.largeText = std::string("Perlin ") + PERLIN_VERSION;
        const uint64_t generation = connectGeneration.load();
        if (generation != discordGeneration) {
            discordGeneration = generation;
            discordSince = static_cast<int64_t>(std::time(nullptr));
        }
        if (discordSince == 0)
            discordSince = static_cast<int64_t>(std::time(nullptr));
        p.startUnix = discordSince;

        const bool menus = !session || titleScreen.active;
        if (menus) {
            p.details = "In the menus";
            p.buttons.emplace_back("Discord", kDiscordInvite);
            discordRpc->Set(p);
            return;
        }

        std::string realmAddress;
        {
            std::lock_guard lock(tabMutex);
            realmAddress = realmJoinAddress;
        }
        const bool friendWorld = friendLease != nullptr;
        const bool realm = !friendWorld && !inSingleplayer && !realmAddress.empty() && options.server == realmAddress;
        const bool direct = !friendWorld && !inSingleplayer && !realm;
        std::string host;
        uint16_t port = 0;
        std::string bare = options.server;
        if (bare.starts_with("https://"))
            bare.erase(0, 8);
        else if (bare.starts_with("http://"))
            bare.erase(0, 7);
        while (!bare.empty() && bare.back() == '/')
            bare.pop_back();
        const bool parsed = direct && detail::ParseServer(bare, host, port);
        const bool publicServer = parsed && !PrivateHost(host) && SafeAddress(options.server);
        const bool showServer = publicServer && options.discordShowServer;

        std::string world = Plain(session->WorldName());
        if (world.empty() && friendWorld)
            world = Plain(friendLease->worldName);
        std::string where;
        if (!world.empty())
            where = world;
        else if (showServer)
            where = "on " + bare;
        else if (inSingleplayer)
            where = "In singleplayer";
        else if (friendWorld)
            where = "In a friend's world";
        else if (realm)
            where = "On a Realm";
        else
            where = "On a server";

        const std::string name = Plain(session->PlayerName());
        if (!loading.done) {
            p.details = "Joining a world";
            p.state = where;
        } else {
            p.details = name.empty() ? std::string("Playing") : "Playing as " + name;
            p.state = where;
        }
        if (showServer && !world.empty())
            p.largeText = bare + " - Perlin " + PERLIN_VERSION;

        if (showServer && options.discordJoin && loading.done) {
            const auto players = session->PlayerList();
            int maxPlayers = 0;
            {
                std::lock_guard lock(pingMutex);
                if (const auto it = pingResults.find(options.server); it != pingResults.end())
                    maxPlayers = it->second.maxPlayers;
            }
            p.partyId = "perlin-" + std::to_string(Fnv(options.server));
            p.partySize = std::max<int>(1, static_cast<int>(players.size()));
            p.partyMax = std::max(p.partySize, maxPlayers > 0 ? maxPlayers : p.partySize + 1);
            p.joinSecret = kJoinPrefix + options.server;
        } else {
            p.buttons.emplace_back("Discord", kDiscordInvite);
        }
        discordRpc->Set(p);
    }
}

namespace {
    using namespace perlin;
    int failures = 0;
    void Fail(const char* what, int seed, int step) {
        if (failures++ < 12)
            std::fprintf(stderr, "MISMATCH %s (seed %d step %d)\n", what, seed, step);
    }
    std::string Pick(std::mt19937& g, std::initializer_list<const char*> options) {
        return *(options.begin() + g() % options.size());
    }
    const std::initializer_list<const char*> kServers = {
        "play.example.com", "play.example.com:19133", "https://play.example.com/", "http://x.org//", "localhost",
        "192.168.1.5:19132", "10.0.0.1", "[::1]:19132", "[2001:db8::1]:1234", "bad host", "", "172.20.0.4",
        "a.b.lan", "server_name", "1.2.3.4:99999", "mc.hypixel.net", "100.64.0.1", "172.31.0.1", "172.32.0.1", "172.15.0.1", "100.127.0.1", "100.128.0.1", "169.254.1.1", "0.0.0.0", "fe80::1", "fd00::1", "2606:4700::1", "172.16.0.1", "169.1.1.1", "192.169.1.1", "100.63.0.1"};
    const std::initializer_list<const char*> kWorlds = {"", "My World", "\xc2\xa7" "aGreen \xc2\xa7r world", "  padded  ", "W"};

    struct NewLink final : services::DiscordService::Link {
        explicit NewLink(origd::Events* e) : e(e) {}
        void SetAutoAccept(bool a) override { e->autoAccept.push_back(a); }
        std::optional<std::string> TakeJoin() override {
            if (e->pendingJoins.empty()) return std::nullopt;
            auto s = e->pendingJoins.front(); e->pendingJoins.pop_front(); return s;
        }
        std::optional<discord::JoinRequest> TakeAccepted() override {
            if (e->pendingAccepted.empty()) return std::nullopt;
            auto s = e->pendingAccepted.front(); e->pendingAccepted.pop_front(); return s;
        }
        void Set(const discord::Presence& p) override { e->presences.push_back(p); }
        origd::Events* e;
    };

    bool SamePresence(discord::Presence a, discord::Presence b) {
        a.startUnix = b.startUnix = 0;
        return a == b;
    }
}

int main() {
    const services::DiscordService::Config base = services::DiscordService::RealConfig("1.0.0");
    long steps = 0, presences = 0, joins = 0, toasts = 0, menus = 0, parties = 0;
    for (int seed = 1; seed <= 600; seed++) {
        std::mt19937 g(static_cast<uint32_t>(seed));
        origd::Events oe, ne;
        origd::Orig orig;
        orig.events = &oe;
        auto session = std::make_shared<origd::FakeSession>();
        services::DiscordService::Config cfg = base;
        cfg.supported = [] { return true; };
        cfg.connect = [&] { ne.links++; return std::unique_ptr<services::DiscordService::Link>(new NewLink(&ne)); };
        cfg.registerLaunch = [](const std::string&, const std::string&) {};
        services::DiscordService::Hooks hooks;
        hooks.joinServer = [&](const std::string& a) { ne.joins.push_back(a); };
        hooks.notify = [&](const std::string& t, const std::string& m) { ne.toasts.push_back(t + "\n" + m); };
        hooks.background = [](std::function<void()> job) { job(); };
        services::DiscordService service(cfg, hooks);

        origd::Clock::current = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
        uint64_t generation = 0;
        for (int step = 0; step < 40; step++) {
            steps++;
            origd::Clock::current += std::chrono::milliseconds(300 + static_cast<int>(g() % 900));
            const auto t = origd::Clock::current;
            const bool presence = (g() % 8) != 0, join = g() & 1, showServer = g() & 1;
            const bool hasSession = (g() % 5) != 0, title = (g() % 4) == 0, single = (g() % 4) == 0;
            const bool lease = (g() % 6) == 0, loadDone = g() & 1;
            const std::string server = Pick(g, kServers);
            const std::string realm = (g() % 4) == 0 ? server : Pick(g, kServers);
            const std::string world = Pick(g, kWorlds), player = Pick(g, kWorlds), leaseWorld = Pick(g, kWorlds);
            const int players = static_cast<int>(g() % 12), maxPlayers = (g() % 3) ? static_cast<int>(g() % 20) : 0;
            if (g() % 10 == 0) generation++;

            orig.options.discordPresence = presence;
            orig.options.discordJoin = join;
            orig.options.discordShowServer = showServer;
            orig.options.server = server;
            orig.titleScreen.active = title;
            orig.inSingleplayer = single;
            orig.loading.done = loadDone;
            orig.realmJoinAddress = realm;
            orig.connectGeneration = generation;
            orig.pingResults.clear();
            if (maxPlayers || (g() & 1)) orig.pingResults[server] = {maxPlayers};
            session->world = world; session->player = player; session->players = players;
            orig.session = hasSession ? session : nullptr;
            orig.friendLease = lease ? std::make_shared<origd::Lease>(origd::Lease{leaseWorld}) : nullptr;

            std::vector<std::string> pending;
            const int nj = static_cast<int>(g() % 3);
            for (int i = 0; i < nj; i++)
                pending.push_back((g() % 6 == 0) ? "garbage" : "perlin1:" + Pick(g, kServers));
            std::vector<discord::JoinRequest> accepted;
            if (g() % 5 == 0) accepted.push_back({"1", "User" + std::to_string(g() % 100)});
            for (auto* e : {&oe, &ne}) {
                e->pendingJoins.assign(pending.begin(), pending.end());
                e->pendingAccepted.assign(accepted.begin(), accepted.end());
            }

            orig.toast.text.clear();
            const size_t joinsBefore = ne.joins.size(), presencesBefore = ne.presences.size();
            orig.UpdateDiscord();
            if (!orig.toast.text.empty())
                oe.toasts.push_back(orig.toast.text);

            services::DiscordService::Settings st{presence, join, showServer};
            services::DiscordService::Snapshot s;
            s.hasSession = hasSession;
            s.inMenus = !hasSession || title;
            s.singleplayer = single;
            s.friendWorld = lease;
            s.loadingDone = loadDone;
            s.server = server;
            s.realmAddress = realm;
            s.connectGeneration = generation;
            if (hasSession) { s.worldName = world; s.playerName = player; s.playerCount = players; }
            if (lease) s.friendWorldName = leaseWorld;
            if (const auto it = orig.pingResults.find(server); it != orig.pingResults.end()) s.maxPlayers = it->second.maxPlayers;
            if (service.Due(t)) service.Tick(t, st, s);

            if (oe.presences.size() != ne.presences.size()) { Fail("presence count", seed, step); continue; }
            if (!oe.presences.empty() && !SamePresence(oe.presences.back(), ne.presences.back())) Fail("presence", seed, step);
            if (oe.joins != ne.joins) Fail("joins", seed, step);
            if (oe.toasts.size() != ne.toasts.size()) Fail("toast count", seed, step);
            if (oe.autoAccept != ne.autoAccept) Fail("auto accept", seed, step);
            if (oe.links != ne.links) Fail("link creation", seed, step);
            if ((orig.discordRpc != nullptr) != service.Connected()) Fail("link alive", seed, step);
            if (oe.pendingJoins.size() != ne.pendingJoins.size() || oe.pendingAccepted.size() != ne.pendingAccepted.size()) Fail("drain", seed, step);
            if (!orig.toast.title.empty() && !oe.toasts.empty()) {
                if (oe.toasts.back() != ne.toasts.back()) Fail("toast text", seed, step);
            }
            presences += static_cast<long>(ne.presences.size() - presencesBefore);
            joins += static_cast<long>(ne.joins.size() - joinsBefore);
            toasts += !ne.toasts.empty() && oe.toasts.size() == ne.toasts.size() ? 0 : 0;
            if (!ne.presences.empty()) {
                menus += ne.presences.back().details == "In the menus";
                parties += !ne.presences.back().joinSecret.empty();
            }
        }
    }
    if (str::Trim(str::StripFormatting("\xc2\xa7" "aHi \xc2\xa7r ")) != "Hi") Fail("strip", 0, 0);
    if (!services::IsPrivateHost("192.168.0.1") || services::IsPrivateHost("8.8.8.8") || !services::IsPrivateHost("::1")) Fail("private", 0, 0);
    if (services::JoinTarget("perlin1:play.example.com:19132") != std::optional<std::string>("play.example.com:19132")) Fail("join target", 0, 0);
    if (services::JoinTarget("perlin1:localhost") || services::JoinTarget("nope") || services::JoinTarget("perlin1:bad host")) Fail("join reject", 0, 0);

    {
        origd::Events e;
        int made = 0;
        services::DiscordService::Config cfg = base;
        cfg.supported = [] { return true; };
        cfg.connect = [&] { made++; return std::unique_ptr<services::DiscordService::Link>(new NewLink(&e)); };
        cfg.registerLaunch = [](const std::string&, const std::string&) {};
        int registered = 0;
        services::DiscordService::Hooks hooks;
        hooks.background = [&](std::function<void()> job) { registered++; job(); };
        services::DiscordService service(cfg, hooks);
        auto t = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
        services::DiscordService::Snapshot snap;
        if (service.Connected()) Fail("connected before use", 0, 0);
        service.Tick(t, {true, false, false}, snap);
        if (!service.Connected() || made != 1 || registered != 1) Fail("link not created once", 0, 0);
        service.Tick(t + std::chrono::milliseconds(500), {false, false, false}, snap);
        if (!service.Connected()) Fail("throttle ignored", 0, 0);
        service.Tick(t + std::chrono::seconds(2), {false, false, false}, snap);
        if (service.Connected()) Fail("presence off must drop link", 0, 0);
        service.Tick(t + std::chrono::seconds(4), {true, false, false}, snap);
        if (!service.Connected() || made != 2) Fail("reconnect", 0, 0);
        service.Stop();
        service.Stop();
        if (service.Connected()) Fail("stop", 0, 0);
        cfg.supported = [] { return false; };
        services::DiscordService off(cfg, hooks);
        if (off.Due(t + std::chrono::hours(1))) Fail("unsupported must never be due", 0, 0);
    }
    std::printf("discord_service_difftest: %ld steps, presences=%ld joins=%ld menus=%ld party=%ld, failures=%d\n",
                steps, presences, joins, menus, parties, failures);
    if (joins == 0 || menus == 0 || parties == 0 || presences == 0) { std::fprintf(stderr, "test is vacuous\n"); return 1; }
    return failures ? 1 : 0;
}
