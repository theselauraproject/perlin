#include "client/services/account/AccountService.hpp"
#include "util/FileIo.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <thread>

using namespace perlin::services;
namespace fs = std::filesystem;
using Clock = AccountService::Clock;

namespace {
    int failures = 0;
    int checks = 0;

    void Check(bool ok, const char* what, int line) {
        checks++;
        if (!ok) {
            failures++;
            std::printf("FAIL line %d: %s\n", line, what);
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    struct Script {
        bool signInOk = true;
        std::string signInError = "denied";
        std::string gamertag = "Tag";
        bool fetchOk = true;
        bool hasSaved = true;
        bool savedOk = true;
        std::string url = "https://login", code = "ABCD";
        std::atomic<int> cancels{0};
        std::atomic<int> opened{0};
        std::map<std::string, std::string> tagByPath;
        std::function<void()> duringSignIn;
        std::atomic<bool> blockUntilCancel{false};
    };

    class FakeLogin : public AccountService::Login {
    public:
        FakeLogin(Script& s, std::string p) : script(s), path(std::move(p)) { script.opened++; }
        bool HasSavedLogin() const override { return script.hasSaved; }
        void Cancel() override {
            script.cancels++;
            cancelled = true;
        }
        bool SignIn(const AccountService::Prompt& prompt, std::string& error) override {
            prompt(script.url, script.code);
            if (script.duringSignIn)
                script.duringSignIn();
            while (script.blockUntilCancel && !cancelled)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (cancelled) {
                error = "cancelled";
                return false;
            }
            if (!script.signInOk) {
                error = script.signInError;
                return false;
            }
            std::ofstream(path) << "{}";
            return true;
        }
        bool SignInSaved(std::string& error) override {
            if (!script.savedOk)
                error = "expired";
            return script.savedOk;
        }
        std::shared_ptr<perlin::xbox::Account> Native() const override { return nullptr; }
        bool FetchGamertag(std::string& tag, std::string& error) override {
            if (!script.fetchOk) {
                error = "no tag";
                return false;
            }
            auto it = script.tagByPath.find(path);
            tag = it != script.tagByPath.end() ? it->second : script.gamertag;
            return true;
        }

    private:
        Script& script;
        std::string path;
        std::atomic<bool> cancelled{false};
    };

    struct Rig {
        fs::path dir;
        Script script;
        std::deque<std::function<void()>> queue;
        std::vector<std::string> logs;
        std::vector<std::pair<std::string, std::string>> shown;
        std::unique_ptr<AccountService> service;

        explicit Rig(bool online = false, const std::string& player = {}, const std::string& tag = "t") {
            static std::atomic<int> n{0};
            dir = fs::temp_directory_path() / ("perlin_acct_" + tag + std::to_string(n++) + "_" +
                                               std::to_string(std::random_device{}()));
            fs::remove_all(dir);
            fs::create_directories(dir);
            Make(online, player);
        }
        ~Rig() {
            service.reset();
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
        void Make(bool online, const std::string& player, bool inlineExec = false) {
            service.reset();
            AccountService::Config c;
            c.dataDir = dir.string();
            c.onlineLogin = online;
            c.playerName = player;
            c.openLogin = [this](const std::string& p) -> std::shared_ptr<AccountService::Login> {
                return std::make_shared<FakeLogin>(script, p);
            };
            c.submit = [this, inlineExec](std::function<void()> job) {
                if (inlineExec)
                    job();
                else
                    queue.push_back(std::move(job));
                return true;
            };
            AccountService::Hooks h;
            h.showSignInCode = [this](const std::string& u, const std::string& co) { shown.emplace_back(u, co); };
            h.log = [this](const std::string& m) { logs.push_back(m); };
            service = std::make_unique<AccountService>(std::move(c), std::move(h));
        }
        void Drain() {
            while (!queue.empty()) {
                auto job = std::move(queue.front());
                queue.pop_front();
                job();
            }
        }
        bool Exists(const std::string& rel) const { return fs::exists(dir / rel); }
    };

    std::string Slurp(const fs::path& p) {
        const std::string contents = perlin::file::Read(p);
        return contents;
    }

    void SignInSuccess() {
        Rig r;
        CHECK(r.service->Entries().empty());
        r.service->BeginSignIn();
        CHECK(r.service->SignInProgress().state == AccountService::SignInState::Waiting);
        CHECK(r.queue.size() == 1);
        r.script.gamertag = "Steve";
        r.Drain();
        const auto view = r.service->SignInProgress();
        CHECK(view.state == AccountService::SignInState::Done);
        CHECK(view.url == "https://login" && view.code == "ABCD");
        CHECK(r.shown.size() == 1 && r.shown[0].second == "ABCD");
        CHECK(r.service->Poll());
        CHECK(!r.service->Poll());
        CHECK(r.service->Entries().size() == 1);
        const AccountEntry* a = r.service->Active();
        CHECK(a && a->microsoft && a->name == "Steve");
        CHECK(a && r.Exists(a->authFile));
        CHECK(r.service->ActiveIsMicrosoft());
        CHECK(r.service->ActiveAuthPath() == r.service->AuthPath(*a));
        CHECK(r.service->SignInProgress().state == AccountService::SignInState::Idle);
        CHECK(Slurp(r.dir / "accounts.json").find("Steve") != std::string::npos);
    }

    void SignInFailure() {
        Rig r;
        r.script.signInOk = false;
        r.service->BeginSignIn();
        r.Drain();
        const auto view = r.service->SignInProgress();
        CHECK(view.state == AccountService::SignInState::Failed);
        CHECK(view.status == "Sign-in failed: denied");
        CHECK(!r.service->Poll());
        CHECK(r.service->Entries().empty());
        r.service->CancelSignIn();
        CHECK(r.service->SignInProgress().state == AccountService::SignInState::Idle);
        CHECK(r.script.cancels == 1);
    }

    void SignInUnnamed() {
        Rig r;
        r.script.fetchOk = false;
        r.service->BeginSignIn();
        r.Drain();
        CHECK(r.service->Poll());
        CHECK(r.service->Active() && r.service->Active()->name.empty());
    }

    void CancelRemovesAuthFile() {
        Rig r;
        r.service->BeginSignIn();
        std::string authRel;
        r.script.duringSignIn = [&] {};
        r.Drain();
        CHECK(fs::exists(r.dir / "accounts"));
        std::size_t files = 0;
        for (auto& e : fs::directory_iterator(r.dir / "accounts"))
            files += e.is_regular_file();
        CHECK(files == 1);
        r.service->CancelSignIn();
        files = 0;
        for (auto& e : fs::directory_iterator(r.dir / "accounts"))
            files += e.is_regular_file();
        CHECK(files == 0);
        CHECK(!r.service->Poll());
        CHECK(r.service->Entries().empty());
    }

    void SupersededFlowIgnored() {
        Rig r;
        r.service->BeginSignIn();
        r.service->BeginSignIn();
        CHECK(r.queue.size() == 2);
        CHECK(r.script.cancels == 1);
        r.script.gamertag = "First";
        auto stale = std::move(r.queue.front());
        r.queue.pop_front();
        stale();
        CHECK(r.service->SignInProgress().state == AccountService::SignInState::Waiting);
        CHECK(!r.service->Poll());
        r.script.gamertag = "Second";
        r.Drain();
        CHECK(r.service->Poll());
        CHECK(r.service->Entries().size() == 1);
        CHECK(r.service->Active()->name == "Second");
        std::size_t files = 0;
        for (auto& e : fs::directory_iterator(r.dir / "accounts"))
            files += e.is_regular_file();
        CHECK(files == 1);
    }

    void CancelAfterDoneDropsResult() {
        Rig r;
        r.service->BeginSignIn();
        r.Drain();
        r.service->CancelSignIn();
        CHECK(!r.service->Poll());
        CHECK(r.service->Entries().empty());
    }

    void Unavailable() {
        Rig r;
        Rig r2;
        r2.Make(false, {});
        AccountService::Config c;
        c.dataDir = r.dir.string();
        c.openLogin = [&](const std::string& p) -> std::shared_ptr<AccountService::Login> {
            return std::make_shared<FakeLogin>(r.script, p);
        };
        c.submit = [&](std::function<void()> j) {
            r.queue.push_back(std::move(j));
            return true;
        };
        c.signInUnavailable = [] { return std::string("no relay"); };
        AccountService svc(std::move(c), {});
        svc.BeginSignIn();
        const auto view = svc.SignInProgress();
        CHECK(view.state == AccountService::SignInState::Failed);
        CHECK(view.status == "no relay");
        CHECK(r.queue.empty());
        svc.Stop();
    }

    void BackfillAndThrottle() {
        Rig r;
        r.service->AddMicrosoft("", "accounts/a.json");
        r.service->AddMicrosoft("Named", "accounts/b.json");
        r.service->AddMicrosoft("", "accounts/c.json");
        r.script.tagByPath[(r.dir / "accounts/a.json").string()] = "Alpha";
        r.script.tagByPath[(r.dir / "accounts/c.json").string()] = "Gamma";
        const auto t0 = Clock::now();
        r.service->BackfillGamertags(t0);
        CHECK(r.queue.size() == 2);
        r.service->BackfillGamertags(t0 + std::chrono::seconds(5));
        CHECK(r.queue.size() == 2);
        r.Drain();
        CHECK(!r.service->Poll());
        std::map<std::string, std::string> byAuth;
        for (auto& e : r.service->Entries())
            byAuth[e.authFile] = e.name;
        CHECK(byAuth["accounts/a.json"] == "Alpha");
        CHECK(byAuth["accounts/b.json"] == "Named");
        CHECK(byAuth["accounts/c.json"] == "Gamma");
        r.service->BackfillGamertags(t0 + std::chrono::seconds(31));
        CHECK(r.queue.empty());
    }

    void BackfillFailureLogged() {
        Rig r;
        r.service->AddMicrosoft("", "accounts/a.json");
        r.script.savedOk = false;
        r.service->BackfillGamertags(Clock::now());
        r.Drain();
        CHECK(!r.service->Poll());
        CHECK(r.service->Entries().front().name.empty());
        CHECK(r.logs.size() == 1 && r.logs[0].find("expired") != std::string::npos);
        Rig q;
        q.service->AddMicrosoft("", "accounts/a.json");
        q.script.hasSaved = false;
        q.service->BackfillGamertags(Clock::now());
        q.Drain();
        CHECK(q.logs.empty());
        CHECK(!q.service->Poll());
    }

    void NoteGamertagApplied() {
        Rig r;
        r.service->AddMicrosoft("", "accounts/a.json");
        r.service->NoteGamertag(r.service->ActiveId(), "Later");
        CHECK(r.service->Entries().front().name.empty());
        r.service->Poll();
        CHECK(r.service->Entries().front().name == "Later");
    }

    void ActiveAndPersistence() {
        std::string firstId;
        fs::path dir;
        {
            Rig r(false, "Alex");
            dir = r.dir;
            CHECK(r.service->Entries().size() == 1 && r.service->Active()->name == "Alex");
            CHECK(!r.service->ActiveIsMicrosoft());
            CHECK(r.service->ActiveAuthPath() == r.dir.string() + "/auth.json");
            r.service->AddOffline("Bob");
            CHECK(r.service->Active()->name == "Bob");
            r.service->AddOffline("Alex");
            CHECK(r.service->Entries().size() == 2 && r.service->Active()->name == "Alex");
            r.service->SetSkin(r.service->ActiveId(), "s.png", "c.png");
            firstId = r.service->ActiveId();
            CHECK(r.service->SetActive("nope") == false);
            r.Make(false, "ignored");
            CHECK(r.service->Entries().size() == 2);
            CHECK(r.service->ActiveId() == firstId);
            CHECK(r.service->Active()->skinKnown && r.service->Active()->skin == "s.png" &&
                  r.service->Active()->cape == "c.png");
            const std::string other = r.service->Entries()[0].id == firstId ? r.service->Entries()[1].id
                                                                            : r.service->Entries()[0].id;
            CHECK(r.service->SetActive(other));
            r.service->Remove(other);
            CHECK(r.service->Entries().size() == 1 && r.service->ActiveId() == firstId);
            r.service->Remove(firstId);
            CHECK(r.service->Entries().empty() && r.service->Active() == nullptr);
            CHECK(!r.service->ActiveIsMicrosoft());
        }
    }

    void LegacyMigration() {
        Rig r(false, {}, "legacy");
        r.service.reset();
        fs::remove(r.dir / "accounts.json");
        std::ofstream(r.dir / "auth.json") << "{}";
        r.Make(true, "Offy");
        CHECK(r.service->Entries().size() == 2);
        CHECK(r.service->Active()->microsoft && r.service->Active()->authFile == "auth.json");
        CHECK(r.service->ActiveAuthPath() == r.dir.string() + "/auth.json");
        r.Make(false, "Offy");
        CHECK(r.service->Entries().size() == 2);
        CHECK(r.service->Active()->microsoft);
    }

    void StopWaitsForRunningWork() {
        Rig r;
        r.script.blockUntilCancel = true;
        std::vector<std::thread> threads;
        std::atomic<int> finished{0};
        AccountService::Config c;
        c.dataDir = r.dir.string();
        c.openLogin = [&](const std::string& p) -> std::shared_ptr<AccountService::Login> {
            return std::make_shared<FakeLogin>(r.script, p);
        };
        c.submit = [&](std::function<void()> job) {
            threads.emplace_back([&finished, job = std::move(job)] {
                job();
                finished++;
            });
            return true;
        };
        std::atomic<bool> prompted{false};
        AccountService::Hooks h;
        h.showSignInCode = [&](const std::string&, const std::string&) { prompted = true; };
        {
            AccountService svc(std::move(c), std::move(h));
            svc.BeginSignIn();
            while (!prompted)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            svc.Stop();
            CHECK(r.script.cancels >= 1);
            CHECK(finished.load() == 1);
            svc.BeginSignIn();
            CHECK(threads.size() == 1);
            CHECK(!svc.Poll());
        }
        for (auto& t : threads)
            t.join();
    }

    void RejectedSubmitDoesNotWedgeStop() {
        Rig r;
        AccountService::Config c;
        c.dataDir = r.dir.string();
        c.openLogin = [&](const std::string& p) -> std::shared_ptr<AccountService::Login> {
            return std::make_shared<FakeLogin>(r.script, p);
        };
        c.submit = [](std::function<void()>) { return false; };
        AccountService svc(std::move(c), {});
        svc.BeginSignIn();
        const auto start = Clock::now();
        svc.Stop();
        CHECK(Clock::now() - start < std::chrono::seconds(2));
    }

    void ConcurrentPollAndNotes() {
        Rig r;
        r.service->AddMicrosoft("", "accounts/a.json");
        const std::string id = r.service->ActiveId();
        std::atomic<bool> stop{false};
        std::thread writer([&] {
            for (int i = 0; i < 2000; i++)
                r.service->NoteGamertag(id, "n" + std::to_string(i));
            stop = true;
        });
        while (!stop)
            r.service->Poll();
        writer.join();
        r.service->Poll();
        CHECK(r.service->Entries().front().name == "n1999");
    }

    void ConnectSignInNamesUnnamedAccount() {
        Rig r;
        r.script.fetchOk = false;
        r.service->BeginSignIn();
        r.Drain();
        CHECK(r.service->Poll());
        CHECK(r.service->Active()->name.empty());
        r.script.fetchOk = true;
        r.script.gamertag = "Alex";
        const int opened = r.script.opened;
        auto connect = r.service->OpenConnectLogin();
        CHECK(r.script.opened == opened + 1);
        CHECK(connect.login && connect.authPath == r.service->ActiveAuthPath());
        CHECK(connect.nameAccountId == r.service->ActiveId());
        std::vector<std::string> prompts;
        const auto outcome = r.service->SignInForConnect(
            connect, [&](const std::string&, const std::string& code) { prompts.push_back(code); });
        CHECK(outcome.signedIn && outcome.error.empty());
        CHECK(prompts.size() == 1 && prompts[0] == "ABCD");
        r.service->Poll();
        CHECK(r.service->Active()->name == "Alex");
    }

    void ConnectSignInKeepsNamedAccount() {
        Rig r;
        r.script.gamertag = "Steve";
        r.service->BeginSignIn();
        r.Drain();
        CHECK(r.service->Poll());
        r.script.gamertag = "Other";
        auto connect = r.service->OpenConnectLogin();
        CHECK(connect.nameAccountId.empty());
        CHECK(r.service->SignInForConnect(connect, [](const std::string&, const std::string&) {}).signedIn);
        CHECK(!r.service->Poll());
        CHECK(r.service->Active()->name == "Steve");
    }

    void ConnectSignInFailureReportsError() {
        Rig r;
        r.script.fetchOk = false;
        r.service->BeginSignIn();
        r.Drain();
        r.service->Poll();
        r.script.signInOk = false;
        r.script.fetchOk = true;
        auto connect = r.service->OpenConnectLogin();
        const auto outcome = r.service->SignInForConnect(connect, [](const std::string&, const std::string&) {});
        CHECK(!outcome.signedIn && outcome.error == "denied");
        CHECK(!r.service->Poll());
        CHECK(r.service->Active()->name.empty());
        AccountService::ConnectLogin none;
        CHECK(!r.service->SignInForConnect(none, [](const std::string&, const std::string&) {}).signedIn);
    }

    void ConnectCancelAndStopCancelLogin() {
        Rig r;
        r.script.blockUntilCancel = true;
        auto connect = r.service->OpenConnectLogin();
        AccountService::ConnectOutcome outcome;
        std::thread worker([&] {
            outcome = r.service->SignInForConnect(connect, [](const std::string&, const std::string&) {});
        });
        while (r.script.cancels == 0) {
            r.service->CancelConnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        worker.join();
        CHECK(!outcome.signedIn && outcome.error == "cancelled");

        Rig stopRig;
        stopRig.script.blockUntilCancel = true;
        auto second = stopRig.service->OpenConnectLogin();
        std::thread blocked([&] {
            stopRig.service->SignInForConnect(second, [](const std::string&, const std::string&) {});
        });
        stopRig.service->Stop();
        blocked.join();
        CHECK(stopRig.script.cancels >= 1);
    }
}

int main() {
    SignInSuccess();
    SignInFailure();
    SignInUnnamed();
    CancelRemovesAuthFile();
    SupersededFlowIgnored();
    CancelAfterDoneDropsResult();
    Unavailable();
    BackfillAndThrottle();
    BackfillFailureLogged();
    NoteGamertagApplied();
    ActiveAndPersistence();
    LegacyMigration();
    StopWaitsForRunningWork();
    RejectedSubmitDoesNotWedgeStop();
    ConcurrentPollAndNotes();
    ConnectSignInNamesUnnamedAccount();
    ConnectSignInKeepsNamedAccount();
    ConnectSignInFailureReportsError();
    ConnectCancelAndStopCancelLogin();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
