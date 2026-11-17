#include "client/application/TitlePortsImpl.hpp"

#include "client/services/update/Updater.hpp"

namespace perlin {
    TitlePortsImpl::TitlePortsImpl(Deps d)
        : serverBrowser(d.serverBrowser), appServices(d.appServices), packManager(d.packManager),
          connectsInFlight(d.connectsInFlight), serverPacks(d.serverPacks),
          applyActiveAccount(std::move(d.applyActiveAccount)), joinSingleplayer(std::move(d.joinSingleplayer)),
          updateAssetScreen(std::move(d.updateAssetScreen)), quit(std::move(d.quit)) {}

    bool TitlePortsImpl::PollAccounts() {
        return Service().Poll();
    }

    TitlePortsImpl::SignInInfo TitlePortsImpl::SignInProgress() {
        const services::AccountService::SignInView view = Service().SignInProgress();
        return {view.url, view.code, view.status};
    }

    TitlePortsImpl::AccountSnapshot TitlePortsImpl::Accounts() {
        AccountSnapshot snapshot;
        services::AccountService& accounts = Service();
        const std::string activeId = accounts.ActiveId();
        for (const services::AccountEntry& e : accounts.Entries())
            snapshot.entries.push_back({e.id, e.name, e.microsoft, e.id == activeId, e.skinKnown, e.skin});
        if (const services::AccountEntry* active = accounts.Active()) {
            snapshot.hasActive = true;
            snapshot.activeName = active->name;
        }
        return snapshot;
    }

    bool TitlePortsImpl::AccountsEmpty() {
        return Service().Entries().empty();
    }

    void TitlePortsImpl::AddPreviewAccounts() {
        Service().AddMicrosoft("Steve4821", "accounts/preview.json");
        Service().AddOffline("Alex");
        Service().AddOffline("BuilderBob");
    }

    void TitlePortsImpl::BackfillGamertags(std::chrono::steady_clock::time_point now) {
        Service().BackfillGamertags(now);
    }

    void TitlePortsImpl::BeginSignIn() {
        Service().BeginSignIn();
    }

    void TitlePortsImpl::CancelSignIn() {
        Service().CancelSignIn();
    }

    void TitlePortsImpl::RemoveAccount(const std::string& id) {
        Service().Remove(id);
    }

    bool TitlePortsImpl::SetActiveAccount(const std::string& id) {
        return Service().SetActive(id);
    }

    void TitlePortsImpl::AddOfflineAccount(const std::string& name) {
        Service().AddOffline(name);
    }

    void TitlePortsImpl::ApplyActiveAccount() {
        applyActiveAccount();
    }

    void TitlePortsImpl::JoinServer(const std::string& address) {
        serverBrowser.JoinServer(address);
    }

    void TitlePortsImpl::AddRealm(const std::string& code) {
        serverBrowser.AddRealm(code);
    }

    void TitlePortsImpl::JoinRealm(long long realmId) {
        serverBrowser.JoinRealm(realmId);
    }

    void TitlePortsImpl::JoinFriend(const ui::ServerEntry& entry) {
        serverBrowser.JoinFriend(entry);
    }

    void TitlePortsImpl::JoinLanGame(const ui::ServerEntry& entry) {
        serverBrowser.JoinLanGame(entry);
    }

    void TitlePortsImpl::ForgetPing(const std::string& address) {
        serverBrowser.ForgetPing(address);
    }

    void TitlePortsImpl::Ping(const std::vector<std::string>& addresses) {
        serverBrowser.Ping(addresses);
    }

    void TitlePortsImpl::RefreshFeatured(bool force) {
        serverBrowser.RefreshFeatured(force);
    }

    void TitlePortsImpl::RefreshTab(ui::ServerTab tab) {
        serverBrowser.RefreshTab(tab);
    }

    void TitlePortsImpl::PollServers(ui::TitleModel& title) {
        serverBrowser.Poll(title);
    }

    bool TitlePortsImpl::TakeFeaturedPing() {
        return serverBrowser.TakeFeaturedPing();
    }

    ui::ServerEntry TitlePortsImpl::PingResult(const std::string& address) {
        return serverBrowser.PingResult(address);
    }

    void TitlePortsImpl::SetJoinMessage(const std::string& message) {
        serverBrowser.SetJoinMessage(message);
    }

    void TitlePortsImpl::JoinSingleplayer() {
        joinSingleplayer();
    }

    void TitlePortsImpl::ReleaseIdleServerPacks() {
        if (connectsInFlight.load() == 0)
            packManager.UnloadServerPacks();
    }

    bool TitlePortsImpl::UpdateAssetScreen(ui::Hud& hud, int fbW, int fbH) {
        return updateAssetScreen(hud, fbW, fbH);
    }

    std::vector<std::string> TitlePortsImpl::ServerPackDirectories() {
        return serverPacks.Directories();
    }

    std::string TitlePortsImpl::BuildLabel() {
        return update::BuildLabel();
    }

    TitlePortsImpl::UpdateInfo TitlePortsImpl::UpdateStatus() {
        using update::State;
        const update::Status u = update::Current();
        UpdateInfo info;
        info.action = u.state == State::Ready || u.state == State::Available;
        info.text = u.state == State::Downloading
                        ? u.message + " " + std::to_string(static_cast<int>(u.progress * 100.0f)) + "%"
                    : info.action || u.state == State::Failed ? u.message
                                                              : std::string();
        return info;
    }

    bool TitlePortsImpl::ActOnUpdate() {
        return update::Act();
    }

    void TitlePortsImpl::Quit() {
        quit();
    }
}
