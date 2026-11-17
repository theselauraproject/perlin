#include "client/services/discord/DiscordService.hpp"

#include "util/Log.hpp"
#include "util/Paths.hpp"
#include "util/ServerAddress.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace perlin::services {
    namespace {
        constexpr const char* kApplicationId = "1568359857915039784";
        constexpr const char* kInviteUrl = "https://discord.gg/pVE3qvXaWb";
        constexpr const char* kJoinPrefix = "perlin1:";

        class RpcLink final : public DiscordService::Link {
        public:
            explicit RpcLink(const std::string& applicationId) : client(applicationId) {}
            void SetAutoAccept(bool accept) override { client.SetAutoAccept(accept); }
            std::optional<std::string> TakeJoin() override { return client.TakeJoin(); }
            std::optional<discord::JoinRequest> TakeAccepted() override { return client.TakeAccepted(); }
            void Set(const discord::Presence& presence) override { client.Set(presence); }

        private:
            discord::Client client;
        };
    }
    bool IsPrivateHost(const std::string& host) {
        std::string h = host;
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
        if (h.empty() || h == "localhost" || h.ends_with(".local") || h.ends_with(".lan"))
            return true;
        if (h.contains(':'))
            return h == "::1" || h == "::" || h.starts_with("fe80:") || h.starts_with("fc") || h.starts_with("fd");
        unsigned a = 0, b = 0, c = 0, d = 0;
        char tail = 0;
        if (std::sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4) {
            return a == 10 || a == 127 || a == 0 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
                   (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
        }
        return !h.contains('.') && !h.contains(':');
    }

    bool IsSafeAddress(const std::string& s) {
        return !s.empty() && s.size() <= 260 && std::ranges::all_of(s, [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']' || c == '_' ||
                   c == '/';
        });
    }

    uint32_t PartyHash(const std::string& s) {
        uint32_t h = 2166136261u;
        for (unsigned char c : s)
            h = (h ^ c) * 16777619u;
        return h;
    }

    std::string BareAddress(const std::string& address) {
        std::string bare = address;
        if (bare.starts_with("https://"))
            bare.erase(0, 8);
        else if (bare.starts_with("http://"))
            bare.erase(0, 7);
        while (!bare.empty() && bare.back() == '/')
            bare.pop_back();
        return bare;
    }

    std::optional<std::string> JoinTarget(const std::string& secret) {
        if (!secret.starts_with(kJoinPrefix))
            return std::nullopt;
        const std::string address = secret.substr(std::char_traits<char>::length(kJoinPrefix));
        std::string host;
        uint16_t port = 0;
        if (!IsSafeAddress(address) || !ParseServerAddress(BareAddress(address), host, port) || IsPrivateHost(host))
            return std::nullopt;
        return address;
    }

    discord::Presence BuildPresence(const DiscordService::Config& config, const DiscordService::Settings& settings,
                                    const DiscordService::Snapshot& s, int64_t sinceUnix) {
        discord::Presence p;
        p.largeImage = "perlin";
        p.largeText = std::string("Perlin ") + config.version;
        p.startUnix = sinceUnix;

        if (s.inMenus) {
            p.details = "In the menus";
            p.buttons.emplace_back("Discord", config.inviteUrl);
            return p;
        }

        const bool realm = !s.friendWorld && !s.singleplayer && !s.realmAddress.empty() && s.server == s.realmAddress;
        const bool direct = !s.friendWorld && !s.singleplayer && !realm;
        std::string host;
        uint16_t port = 0;
        const std::string bare = BareAddress(s.server);
        const bool parsed = direct && ParseServerAddress(bare, host, port);
        const bool publicServer = parsed && !IsPrivateHost(host) && IsSafeAddress(s.server);
        const bool showServer = publicServer && settings.showServer;

        std::string world = str::Trim(str::StripFormatting(s.worldName));
        if (world.empty() && s.friendWorld)
            world = str::Trim(str::StripFormatting(s.friendWorldName));
        std::string where;
        if (!world.empty())
            where = world;
        else if (showServer)
            where = "on " + bare;
        else if (s.singleplayer)
            where = "In singleplayer";
        else if (s.friendWorld)
            where = "In a friend's world";
        else if (realm)
            where = "On a Realm";
        else
            where = "On a server";

        const std::string name = str::Trim(str::StripFormatting(s.playerName));
        if (!s.loadingDone) {
            p.details = "Joining a world";
            p.state = where;
        } else {
            p.details = name.empty() ? std::string("Playing") : "Playing as " + name;
            p.state = where;
        }
        if (showServer && !world.empty())
            p.largeText = bare + " - Perlin " + config.version;

        if (showServer && settings.join && s.loadingDone) {
            p.partyId = "perlin-" + std::to_string(PartyHash(s.server));
            p.partySize = std::max<int>(1, s.playerCount);
            p.partyMax = std::max(p.partySize, s.maxPlayers > 0 ? s.maxPlayers : p.partySize + 1);
            p.joinSecret = kJoinPrefix + s.server;
        } else {
            p.buttons.emplace_back("Discord", config.inviteUrl);
        }
        return p;
    }

    DiscordService::Config DiscordService::RealConfig(std::string version) {
        Config c;
        c.applicationId = kApplicationId;
        c.inviteUrl = kInviteUrl;
        c.version = std::move(version);
        c.executablePath = paths::ExePath();
        c.supported = [] { return discord::Supported(); };
        c.connect = [] { return std::unique_ptr<Link>(new RpcLink(kApplicationId)); };
        c.registerLaunch = [](const std::string& id, const std::string& exe) { discord::RegisterLaunch(id, exe); };
        return c;
    }

    DiscordService::DiscordService(Config cfg, Hooks h) : config(std::move(cfg)), hooks(std::move(h)) {}

    void DiscordService::Stop() { link.reset(); }

    void DiscordService::HandleJoin(const std::string& secret, const Snapshot& s) {
        if (!secret.starts_with(kJoinPrefix))
            return;
        const std::optional<std::string> address = JoinTarget(secret);
        if (!address) {
            Log("discord", "ignored a Discord join with an unusable address");
            return;
        }
        if (s.hasSession && !s.friendWorld && !s.singleplayer && s.server == *address)
            return;
        Log("discord", "joining {} from Discord", *address);
        if (hooks.joinServer)
            hooks.joinServer(*address);
    }

    void DiscordService::Tick(Clock::time_point now, const Settings& settings, const Snapshot& s) {
        if (!config.supported || !config.supported())
            return;
        if (now < nextUpdate)
            return;
        nextUpdate = now + std::chrono::seconds(1);
        if (!settings.presence) {
            link.reset();
            return;
        }
        if (!link) {
            link = config.connect();
            if (hooks.background && config.registerLaunch)
                hooks.background([register_ = config.registerLaunch, id = config.applicationId,
                                  exe = config.executablePath] { register_(id, exe); });
        }
        link->SetAutoAccept(settings.join);
        while (auto secret = link->TakeJoin())
            HandleJoin(*secret, s);
        while (auto request = link->TakeAccepted())
            if (hooks.notify)
                hooks.notify("Discord", request->username + " is joining you");

        if (s.connectGeneration != generation) {
            generation = s.connectGeneration;
            since = static_cast<int64_t>(std::time(nullptr));
        }
        if (since == 0)
            since = static_cast<int64_t>(std::time(nullptr));
        link->Set(BuildPresence(config, settings, s, since));
    }
}
