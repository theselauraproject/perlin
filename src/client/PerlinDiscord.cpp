#include "client/Perlin.hpp"
#include "client/PerlinInternal.hpp"

#include "util/Log.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <thread>

namespace perlin {
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

    void Perlin::HandleDiscordJoin(const std::string& secret) {
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

    void Perlin::UpdateDiscord() {
        if (!discord::Supported())
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
            discordRpc = std::make_unique<discord::Client>(kDiscordApp);
            std::thread([exe = paths::ExePath()] { discord::RegisterLaunch(kDiscordApp, exe); }).detach();
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
