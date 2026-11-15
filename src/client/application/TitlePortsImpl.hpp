#pragma once
#include "client/application/AppServices.hpp"
#include "client/features/packs/PackManager.hpp"
#include "client/features/servers/ServerBrowser.hpp"
#include "client/screen/screens/TitlePorts.hpp"
#include "client/systems/resources/ServerPacks.hpp"

#include <atomic>
#include <functional>

namespace perlin {
    class TitlePortsImpl final : public screen::TitlePorts {
    public:
        struct Deps {
            servers::ServerBrowser& serverBrowser;
            AppServices& appServices;
            packs::PackManager& packManager;
            std::atomic<int>& connectsInFlight;
            ServerPacks& serverPacks;
            std::function<void()> applyActiveAccount;
            std::function<void()> joinSingleplayer;
            std::function<bool(ui::Hud&, int, int)> updateAssetScreen;
            std::function<void()> quit;
        };

        explicit TitlePortsImpl(Deps deps);

        bool PollAccounts() override;
        SignInInfo SignInProgress() override;
        AccountSnapshot Accounts() override;
        bool AccountsEmpty() override;
        void AddPreviewAccounts() override;
        void BackfillGamertags(std::chrono::steady_clock::time_point now) override;
        void BeginSignIn() override;
        void CancelSignIn() override;
        void RemoveAccount(const std::string& id) override;
        bool SetActiveAccount(const std::string& id) override;
        void AddOfflineAccount(const std::string& name) override;
        void ApplyActiveAccount() override;

        void JoinServer(const std::string& address) override;
        void AddRealm(const std::string& code) override;
        void JoinRealm(long long realmId) override;
        void JoinFriend(const ui::ServerEntry& entry) override;
        void JoinLanGame(const ui::ServerEntry& entry) override;
        void ForgetPing(const std::string& address) override;
        void Ping(const std::vector<std::string>& addresses) override;
        void RefreshFeatured(bool force) override;
        void RefreshTab(ui::ServerTab tab) override;
        void PollServers(ui::TitleModel& title) override;
        bool TakeFeaturedPing() override;
        ui::ServerEntry PingResult(const std::string& address) override;
        void SetJoinMessage(const std::string& message) override;

        void JoinSingleplayer() override;
        void ReleaseIdleServerPacks() override;
        bool UpdateAssetScreen(ui::Hud& hud, int fbW, int fbH) override;
        std::vector<std::string> ServerPackDirectories() override;
        std::string BuildLabel() override;
        UpdateInfo UpdateStatus() override;
        bool ActOnUpdate() override;
        void Quit() override;

    private:
        services::AccountService& Service() { return *appServices.accounts; }

        servers::ServerBrowser& serverBrowser;
        AppServices& appServices;
        packs::PackManager& packManager;
        std::atomic<int>& connectsInFlight;
        ServerPacks& serverPacks;
        std::function<void()> applyActiveAccount;
        std::function<void()> joinSingleplayer;
        std::function<bool(ui::Hud&, int, int)> updateAssetScreen;
        std::function<void()> quit;
    };
}
