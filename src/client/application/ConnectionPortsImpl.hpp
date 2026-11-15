#pragma once
#include "client/features/servers/ServerBrowser.hpp"
#include "client/screen/screens/ConnectionPorts.hpp"
#include "client/systems/replay/ReplayController.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace perlin {
    class ConnectionPortsImpl final : public screen::ConnectionPorts {
    public:
        struct Deps {
            std::unique_ptr<BedrockSession>& session;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            std::atomic<bool>& sessionReady;
            std::mutex& statusMutex;
            std::string& statusHint;
            bool& onlineThisRun;
            bool& inSingleplayer;
            replay::ReplayController& replayCtl;
            servers::ServerBrowser& serverBrowser;
            screen::ConnectionPorts::VersionOverride& versionOverride;
            std::function<bool()> modHidesLoading;
            std::function<void()> connect;
            std::function<void(ui::Hud&)> leaveToServerList;
            std::function<void()> startAssetDownloads;
            std::function<void()> closeGameplayScreens;
        };

        explicit ConnectionPortsImpl(Deps deps);

        BedrockSession* Session() override;
        LevelRenderer* Level() override;
        bool SessionReady() override;
        void SetSessionReady(bool ready) override;
        std::string StatusHint() override;
        bool OnlineThisRun() override;
        bool InSingleplayer() override;
        bool ReplayPlaying() override;
        bool ModHidesLoading() override;
        bool TakeLeaseJoinFailed() override;
        void PollLeaseJoin() override;
        VersionOverride& Override() override;
        void Connect() override;
        void LeaveToServerList(ui::Hud& hud) override;
        void StartAssetDownloads() override;
        std::array<assets::Job*, 2> AssetJobs() override;
        void CloseGameplayScreens() override;

    private:
        std::unique_ptr<BedrockSession>& session;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        std::atomic<bool>& sessionReady;
        std::mutex& statusMutex;
        std::string& statusHint;
        bool& onlineThisRun;
        bool& inSingleplayer;
        replay::ReplayController& replayCtl;
        servers::ServerBrowser& serverBrowser;
        screen::ConnectionPorts::VersionOverride& versionOverride;
        std::function<bool()> modHidesLoading;
        std::function<void()> connect;
        std::function<void(ui::Hud&)> leaveToServerList;
        std::function<void()> startAssetDownloads;
        std::function<void()> closeGameplayScreens;
    };
}
