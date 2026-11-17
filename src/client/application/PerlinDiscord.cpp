#include "client/application/Perlin.hpp"
#include "util/Version.hpp"
#include "client/application/PerlinInternal.hpp"

namespace perlin {
    void Perlin::BuildDiscordService() {
        services::DiscordService::Hooks hooks;
        hooks.joinServer = [this](const std::string& address) { serverBrowser.JoinServer(address); };
        hooks.notify = [this](const std::string& title, const std::string& message) {
            toast.title = title;
            toast.message = message;
            toast.text = title + "\n" + message;
            toast.at = Clock::now();
        };
        hooks.background = [this](std::function<void()> job) { tasks.Run(std::move(job)); };
        appServices.discord =
            std::make_unique<services::DiscordService>(services::DiscordService::RealConfig(kVersion), std::move(hooks));
    }

    void Perlin::UpdateDiscord() {
        services::DiscordService* service = appServices.discord.get();
        const auto now = Clock::now();
        if (!service || !service->Due(now))
            return;
        services::DiscordService::Settings settings;
        settings.presence = options.discordPresence;
        settings.join = options.discordJoin;
        settings.showServer = options.discordShowServer;

        services::DiscordService::Snapshot s;
        s.hasSession = session != nullptr;
        s.inMenus = !session || titleScreen.active;
        s.singleplayer = inSingleplayer;
        s.friendWorld = serverBrowser.FriendLease() != nullptr;
        s.loadingDone = loading.done;
        s.server = options.server;
        s.connectGeneration = connectGeneration.load();
        {
            s.realmAddress = serverBrowser.RealmAddress();
        }
        if (session) {
            s.worldName = session->WorldName();
            s.playerName = session->PlayerName();
            s.playerCount = static_cast<int>(session->PlayerList().size());
        }
        if (const auto& lease = serverBrowser.FriendLease())
            s.friendWorldName = lease->worldName;
        if (const ui::ServerEntry ping = serverBrowser.PingResult(options.server); ping.maxPlayers > 0)
            s.maxPlayers = ping.maxPlayers;
        service->Tick(now, settings, s);
    }
}
