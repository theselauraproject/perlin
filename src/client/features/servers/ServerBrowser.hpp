#pragma once
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/config/Options.hpp"
#include "client/ui/TitleModel.hpp"
#include "network/FriendWorld.hpp"
#include "network/LanDiscovery.hpp"
#include "util/TaskGroup.hpp"
#include "util/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace perlin::servers {
    class ServerBrowser {
    public:
        struct Deps {
            Options& options;
            ui::UiAssets& uiAssets;
            std::unique_ptr<ThreadPool>& workers;
            TaskGroup& tasks;
            std::function<bool()> activeIsMicrosoft;
            std::function<std::string()> activeAuthPath;
            std::function<void(const std::string&)> setStatusHint;
            std::function<void()> hideTitle;
            std::function<void()> connect;
        };

        explicit ServerBrowser(Deps deps);
        ServerBrowser(const ServerBrowser&) = delete;
        ServerBrowser& operator=(const ServerBrowser&) = delete;

        void Ping(const std::vector<std::string>& addresses);
        void ForgetPing(const std::string& address);
        ui::ServerEntry PingResult(const std::string& address) const;

        void RefreshTab(ui::ServerTab tab);
        void RefreshFeatured(bool force);
        bool TakeFeaturedPing() { return featuredPing.exchange(false); }

        void Poll(ui::TitleModel& title);
        void PollLeaseJoin();
        bool TakeLeaseJoinFailed() { return leaseJoinFailed.exchange(false); }
        bool LeaseJoining() const { return leaseJoining; }

        void JoinServer(const std::string& address);
        void JoinRealm(long long id);
        void JoinFriend(const ui::ServerEntry& entry);
        void JoinLanGame(const ui::ServerEntry& entry);
        void AddRealm(const std::string& code);
        void SetJoinMessage(const std::string& text);
        void ReleaseFriendLease();

        const std::shared_ptr<friendworld::Lease>& FriendLease() const { return friendLease; }
        std::string RealmAddress() const;

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        void BeginLeaseJoin(const std::string& text);
        void FailLeaseJoin(const std::string& text);
        void RebuildFriendSections(ui::TitleModel& title);

        Options& options;
        ui::UiAssets& uiAssets;
        std::unique_ptr<ThreadPool>& workers;
        TaskGroup& tasks;
        std::function<bool()> activeIsMicrosoft;
        std::function<std::string()> activeAuthPath;
        std::function<void(const std::string&)> setStatusHint;
        std::function<void()> hideTitle;
        std::function<void()> connect;

        mutable std::mutex pingMutex;
        std::map<std::string, ui::ServerEntry> pingResults;
        mutable std::mutex tabMutex;
        std::vector<ui::ServerEntry> tabLan, tabRealms, tabFriends, tabFeatured;
        std::string tabLanMsg, tabRealmsMsg, tabFriendsMsg, tabJoinMsg, realmJoinAddress;
        bool tabDirty = false, realmJoinReady = false;
        std::atomic<bool> lanBusy{false}, realmsBusy{false}, friendsBusy{false}, realmJoinBusy{false};
        std::atomic<bool> featuredBusy{false}, featuredPing{false}, featuredCacheLoaded{false};
        std::atomic<bool> realmAddBusy{false}, realmAddedFlag{false};
        TimePoint realmAddedAt;
        std::atomic<bool> leaseJoining{false}, leaseJoinFailed{false};
        std::shared_ptr<lan::Session> lanSession;
        std::mutex lanSessionMutex;
        std::shared_ptr<friendworld::Lease> friendLease;
        std::shared_ptr<friendworld::Lease> friendLeaseReady;
        std::atomic<bool> friendJoinBusy{false};
    };
}
