#pragma once
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/resources/AssetJobs.hpp"
#include "client/ui/Hud.hpp"

#include <array>
#include <string>

namespace perlin::screen {
    class ConnectionPorts {
    public:
        struct VersionOverride {
            std::string server;
            int protocol = 0;
            std::string version;
            bool dontAsk = false;
        };

        virtual ~ConnectionPorts() = default;

        virtual BedrockSession* Session() = 0;
        virtual LevelRenderer* Level() = 0;
        virtual bool SessionReady() = 0;
        virtual void SetSessionReady(bool ready) = 0;
        virtual std::string StatusHint() = 0;
        virtual bool OnlineThisRun() = 0;
        virtual bool InSingleplayer() = 0;
        struct SingleplayerSetup {
            bool starting = false;
            std::string text;
            float progress = -1.0f;
        };
        virtual SingleplayerSetup SingleplayerStatus() { return {}; }
        virtual bool ReplayPlaying() = 0;
        virtual bool ModHidesLoading() = 0;
        virtual bool TakeLeaseJoinFailed() = 0;
        virtual void PollLeaseJoin() = 0;
        virtual VersionOverride& Override() = 0;
        virtual void Connect() = 0;
        virtual void LeaveToServerList(ui::Hud& hud) = 0;
        virtual void StartAssetDownloads() = 0;
        virtual std::array<assets::Job*, 2> AssetJobs() = 0;
        virtual void CloseGameplayScreens() = 0;
    };
}
