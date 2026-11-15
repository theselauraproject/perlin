#pragma once
#include "client/ui/Hud.hpp"
#include "client/ui/TitleModel.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::screen {
    class TitlePorts {
    public:
        struct AccountInfo {
            std::string id, name;
            bool microsoft = false, active = false, skinKnown = false;
            std::string skin;
        };
        struct AccountSnapshot {
            std::vector<AccountInfo> entries;
            bool hasActive = false;
            std::string activeName;
        };
        struct SignInInfo {
            std::string url, code, status;
        };
        struct UpdateInfo {
            bool action = false;
            std::string text;
        };

        virtual ~TitlePorts() = default;

        virtual bool PollAccounts() = 0;
        virtual SignInInfo SignInProgress() = 0;
        virtual AccountSnapshot Accounts() = 0;
        virtual bool AccountsEmpty() = 0;
        virtual void AddPreviewAccounts() = 0;
        virtual void BackfillGamertags(std::chrono::steady_clock::time_point now) = 0;
        virtual void BeginSignIn() = 0;
        virtual void CancelSignIn() = 0;
        virtual void RemoveAccount(const std::string& id) = 0;
        virtual bool SetActiveAccount(const std::string& id) = 0;
        virtual void AddOfflineAccount(const std::string& name) = 0;
        virtual void ApplyActiveAccount() = 0;

        virtual void JoinServer(const std::string& address) = 0;
        virtual void AddRealm(const std::string& code) = 0;
        virtual void JoinRealm(long long realmId) = 0;
        virtual void JoinFriend(const ui::ServerEntry& entry) = 0;
        virtual void JoinLanGame(const ui::ServerEntry& entry) = 0;
        virtual void ForgetPing(const std::string& address) = 0;
        virtual void Ping(const std::vector<std::string>& addresses) = 0;
        virtual void RefreshFeatured(bool force) = 0;
        virtual void RefreshTab(ui::ServerTab tab) = 0;
        virtual void PollServers(ui::TitleModel& title) = 0;
        virtual bool TakeFeaturedPing() = 0;
        virtual ui::ServerEntry PingResult(const std::string& address) = 0;
        virtual void SetJoinMessage(const std::string& message) = 0;

        virtual void JoinSingleplayer() = 0;
        virtual void ReleaseIdleServerPacks() = 0;
        virtual bool UpdateAssetScreen(ui::Hud& hud, int fbW, int fbH) = 0;
        virtual std::vector<std::string> ServerPackDirectories() = 0;
        virtual std::string BuildLabel() = 0;
        virtual UpdateInfo UpdateStatus() = 0;
        virtual bool ActOnUpdate() = 0;
        virtual void Quit() = 0;
    };
}
