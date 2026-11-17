#include "client/application/ConnectionPortsImpl.hpp"

namespace perlin {
    ConnectionPortsImpl::ConnectionPortsImpl(Deps d)
        : session(d.session), levelRenderer(d.levelRenderer), sessionReady(d.sessionReady),
          statusMutex(d.statusMutex), statusHint(d.statusHint), onlineThisRun(d.onlineThisRun),
          inSingleplayer(d.inSingleplayer), replayCtl(d.replayCtl), serverBrowser(d.serverBrowser),
          versionOverride(d.versionOverride), modHidesLoading(std::move(d.modHidesLoading)),
          connect(std::move(d.connect)), leaveToServerList(std::move(d.leaveToServerList)),
          startAssetDownloads(std::move(d.startAssetDownloads)),
          closeGameplayScreens(std::move(d.closeGameplayScreens)) {}

    BedrockSession* ConnectionPortsImpl::Session() {
        return session.get();
    }

    LevelRenderer* ConnectionPortsImpl::Level() {
        return levelRenderer.get();
    }

    bool ConnectionPortsImpl::SessionReady() {
        return sessionReady.load();
    }

    void ConnectionPortsImpl::SetSessionReady(bool ready) {
        sessionReady = ready;
    }

    std::string ConnectionPortsImpl::StatusHint() {
        std::lock_guard lock(statusMutex);
        return statusHint;
    }

    bool ConnectionPortsImpl::OnlineThisRun() {
        return onlineThisRun;
    }

    bool ConnectionPortsImpl::InSingleplayer() {
        return inSingleplayer;
    }

    bool ConnectionPortsImpl::ReplayPlaying() {
        return replayCtl.Playing();
    }

    bool ConnectionPortsImpl::ModHidesLoading() {
        return modHidesLoading();
    }

    bool ConnectionPortsImpl::TakeLeaseJoinFailed() {
        return serverBrowser.TakeLeaseJoinFailed();
    }

    void ConnectionPortsImpl::PollLeaseJoin() {
        serverBrowser.PollLeaseJoin();
    }

    screen::ConnectionPorts::VersionOverride& ConnectionPortsImpl::Override() {
        return versionOverride;
    }

    void ConnectionPortsImpl::Connect() {
        connect();
    }

    void ConnectionPortsImpl::LeaveToServerList(ui::Hud& hud) {
        leaveToServerList(hud);
    }

    void ConnectionPortsImpl::StartAssetDownloads() {
        startAssetDownloads();
    }

    std::array<assets::Job*, 2> ConnectionPortsImpl::AssetJobs() {
        return {&assets::Samples(), &assets::Client()};
    }

    void ConnectionPortsImpl::CloseGameplayScreens() {
        closeGameplayScreens();
    }
}
