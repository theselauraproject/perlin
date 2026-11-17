#pragma once
#include "client/services/discord/DiscordRpc.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace perlin::services {
    class DiscordService {
    public:
        using Clock = std::chrono::steady_clock;

        class Link {
        public:
            virtual ~Link() = default;
            virtual void SetAutoAccept(bool accept) = 0;
            virtual std::optional<std::string> TakeJoin() = 0;
            virtual std::optional<discord::JoinRequest> TakeAccepted() = 0;
            virtual void Set(const discord::Presence& presence) = 0;
        };

        struct Settings {
            bool presence = false;
            bool join = false;
            bool showServer = false;
        };

        struct Snapshot {
            bool hasSession = false;
            bool inMenus = true;
            bool singleplayer = false;
            bool friendWorld = false;
            bool loadingDone = false;
            std::string server;
            std::string realmAddress;
            std::string friendWorldName;
            std::string worldName;
            std::string playerName;
            int playerCount = 0;
            int maxPlayers = 0;
            uint64_t connectGeneration = 0;
        };

        struct Hooks {
            std::function<void(const std::string& address)> joinServer;
            std::function<void(const std::string& title, const std::string& message)> notify;
            std::function<void(std::function<void()>)> background;
        };

        struct Config {
            std::string applicationId;
            std::string inviteUrl;
            std::string version;
            std::string executablePath;
            std::function<bool()> supported;
            std::function<std::unique_ptr<Link>()> connect;
            std::function<void(const std::string& applicationId, const std::string& executable)> registerLaunch;
        };

        DiscordService(Config config, Hooks hooks);

        bool Due(Clock::time_point now) const { return config.supported && config.supported() && now >= nextUpdate; }
        void Tick(Clock::time_point now, const Settings& settings, const Snapshot& snapshot);
        void Stop();
        bool Connected() const { return link != nullptr; }

        static Config RealConfig(std::string version);

    private:
        void HandleJoin(const std::string& secret, const Snapshot& snapshot);

        Config config;
        Hooks hooks;
        std::unique_ptr<Link> link;
        Clock::time_point nextUpdate{};
        uint64_t generation = 0;
        int64_t since = 0;
    };

    bool IsPrivateHost(const std::string& host);
    bool IsSafeAddress(const std::string& address);
    uint32_t PartyHash(const std::string& text);
    std::string BareAddress(const std::string& address);
    std::optional<std::string> JoinTarget(const std::string& secret);
    discord::Presence BuildPresence(const DiscordService::Config& config, const DiscordService::Settings& settings,
                                    const DiscordService::Snapshot& snapshot, int64_t sinceUnix);
}
