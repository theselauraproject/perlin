#include "client/features/servers/ServerBrowser.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"

#include <chrono>
#include <thread>

namespace {
    using namespace perlin;

    struct Rig {
        Options options;
        std::unique_ptr<ThreadPool> workers = std::make_unique<ThreadPool>(2);
        TaskGroup tasks;
        bool microsoft = false;
        std::string authPath = "/nonexistent/perlin_selftest_auth.json";
        std::vector<std::string> hints;
        int hidden = 0, connected = 0;
        servers::ServerBrowser browser{servers::ServerBrowser::Deps{
            options, selftest::G().uiAssets, workers, tasks, [this] { return microsoft; },
            [this] { return authPath; }, [this](const std::string& h) { hints.push_back(h); },
            [this] { hidden++; }, [this] { connected++; }}};

        template <class Pred> bool WaitFor(Pred done, int timeoutMs = 8000) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < end) {
                if (done())
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return done();
        }
    };
}

namespace perlin::selftest {
    int ServerBrowser() {
        Checker t("server_browser_test");
        TempDataDir data;

        {
            Rig r;
            r.browser.JoinServer("");
            t.Check(r.connected == 0 && r.hidden == 0, "empty address is ignored");
            r.browser.JoinServer("play.example.net:19132");
            t.Equal(r.options.server, std::string("play.example.net:19132"), "join records the address");
            t.Equal(r.hidden, 1, "join hides the title screen");
            t.Equal(r.connected, 1, "join starts the connection");
        }
        {
            Rig r;
            ui::TitleModel title;
            t.Check(!r.browser.LeaseJoining() && !r.browser.TakeLeaseJoinFailed(), "no lease join initially");
            r.browser.SetJoinMessage("hello");
            r.browser.Poll(title);
            t.Equal(title.joinMessage, std::string("hello"), "join message reaches the title model");
            r.browser.SetJoinMessage({});
            r.browser.Poll(title);
            t.Check(title.joinMessage.empty(), "cleared join message reaches the title model");
        }
        {
            Rig r;
            ui::TitleModel title;
            r.browser.AddRealm("CODE");
            r.browser.JoinRealm(5);
            ui::ServerEntry friendEntry{.name = "Pal", .address = "", .hosting = true};
            r.browser.JoinLanGame(friendEntry);
            r.browser.Poll(title);
            t.Check(title.joinMessage.find("Microsoft") != std::string::npos,
                    "signed-out join attempts explain the Microsoft requirement");
            t.Check(!r.browser.LeaseJoining(), "signed-out join does not start a lease join");
            t.Equal(r.connected, 0, "signed-out join never connects");
        }
        {
            Rig r;
            ui::TitleModel title;
            title.tab = ui::ServerTab::Realms;
            r.browser.RefreshTab(ui::ServerTab::Realms);
            r.browser.Poll(title);
            t.Check(title.realms.empty(), "signed-out realms list is empty");
            t.Check(title.tabMessage.find("Realms") != std::string::npos, "realms tab explains sign-in");
            title.tab = ui::ServerTab::Friends;
            r.browser.RefreshTab(ui::ServerTab::Friends);
            r.browser.Poll(title);
            t.Check(title.tabMessage.find("friends") != std::string::npos, "friends tab explains sign-in");
            t.Check(title.friends.empty(), "signed-out friends list stays empty");
        }
        {
            Rig r;
            ui::TitleModel title;
            ui::ServerEntry offlineFriend{.name = "Off", .address = "", .offline = true};
            r.browser.JoinFriend(offlineFriend);
            r.browser.Poll(title);
            t.Check(title.joinMessage == "Off is offline", "offline friend cannot be joined");
            ui::ServerEntry idleFriend{.name = "Idle", .address = ""};
            r.browser.JoinFriend(idleFriend);
            r.browser.Poll(title);
            t.Check(title.joinMessage.find("isn't hosting") != std::string::npos,
                    "idle friend explains there is nothing to join");
            t.Check(!r.browser.LeaseJoining(), "unjoinable friends never start a lease join");
        }
        {
            Rig r;
            r.microsoft = true;
            ui::TitleModel title;
            ui::ServerEntry host{.name = "Host", .address = "", .hosting = true};
            host.xuid = "123";
            r.browser.JoinFriend(host);
            t.Check(!r.hints.empty() && r.hints.front() == "Joining Host...", "lease join announces progress");
            t.Equal(r.hidden, 1, "lease join hides the title screen");
            const bool failed = r.WaitFor([&] { return r.browser.TakeLeaseJoinFailed(); });
            t.Check(failed, "a failing sign-in reports the lease join as failed");
            t.Check(!r.browser.LeaseJoining(), "failed lease join stops joining");
            t.Check(!r.hints.empty() && r.hints.back().empty(), "failed lease join clears the status hint");
            t.Check(!r.browser.TakeLeaseJoinFailed(), "failure flag is consumed once");
            r.browser.Poll(title);
            t.Check(!title.joinMessage.empty(), "failure reason is shown on the title screen");
            t.Equal(r.connected, 0, "failed lease join never connects");
            t.Check(!r.browser.FriendLease(), "failed lease join holds no lease");
            r.browser.PollLeaseJoin();
            t.Equal(r.connected, 0, "polling after failure does not connect");
        }
        {
            Rig r;
            r.microsoft = true;
            ui::TitleModel title;
            r.browser.SetJoinMessage("x");
            r.browser.JoinRealm(7);
            t.Check(r.WaitFor([&] { return r.browser.TakeLeaseJoinFailed(); }), "realm join failure is reported");
            t.Equal(r.connected, 0, "realm join failure never connects");
        }
        {
            Rig r;
            const std::string address = "not a valid address";
            r.browser.Ping({address});
            const ui::ServerEntry started = r.browser.PingResult(address);
            t.Check(started.status == ui::ServerEntry::Status::Pinging ||
                        started.status == ui::ServerEntry::Status::Offline,
                    "pinging marks the entry as in flight");
            t.Check(r.WaitFor([&] {
                return r.browser.PingResult(address).status == ui::ServerEntry::Status::Offline;
            }), "an unparseable address ends offline");
            r.browser.ForgetPing(address);
            t.Check(r.browser.PingResult(address).status == ui::ServerEntry::Status::Unknown,
                    "forgetting a ping resets it");
            t.Check(r.browser.PingResult("never.pinged").status == ui::ServerEntry::Status::Unknown,
                    "unknown address has no result");
            r.browser.Ping({address, address});
            t.Check(r.WaitFor([&] {
                return r.browser.PingResult(address).status == ui::ServerEntry::Status::Offline;
            }), "duplicate addresses in one ping settle");
        }
        {
            Rig r;
            ui::TitleModel title;
            title.tab = ui::ServerTab::Lan;
            r.browser.RefreshTab(ui::ServerTab::Lan);
            r.browser.Poll(title);
            t.Check(title.tabMessage.find("Scanning") != std::string::npos, "lan refresh announces scanning");
            const bool settled = r.WaitFor(
                [&] {
                    r.browser.Poll(title);
                    return title.tabMessage.find("Scanning") == std::string::npos;
                },
                30000);
            t.Check(settled, "lan scan completes and replaces the scanning message");
        }
        return t.Finish();
    }
}
