#include "network/TokenStore.hpp"
#include "network/SecretVault.hpp"
#include "util/FileIo.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace perlin;

static std::atomic<int> failures{0};
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

namespace {
    namespace fs = std::filesystem;

    struct Dir {
        fs::path path;
        explicit Dir(const char* name) {
            path = fs::temp_directory_path() / (std::string("perlin_store_") + name);
            fs::remove_all(path);
            fs::create_directories(path);
        }
        ~Dir() { fs::remove_all(path); }
        std::string File() const { return (path / "auth.json").string(); }
    };

    struct FakeClock final : auth::Clock {
        std::atomic<int64_t> nanos{1790000000LL * 1000000000LL};
        int64_t NowNanos() const override { return nanos.load(); }
    };

    auth::TokenStore::Options Opts(std::shared_ptr<const auth::Clock> clock = nullptr, int lockMs = 5000) {
        auth::TokenStore::Options o;
        o.clock = std::move(clock);
        o.lockTimeoutMs = lockMs;
        return o;
    }

    bool HasToken(const auth::State& s) {
        return !s.data.xsts.empty();
    }
}

static void TestSingleFlight() {
    Dir dir("flight");
    auth::TokenStore store(dir.File(), Opts());
    std::atomic<int> produced{0};
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 24; i++)
        threads.emplace_back([&] {
            std::string error;
            const bool r = store.Ensure(
                "xsts:a", {auth::TokenStore::Scope::Process, true, nullptr}, HasToken,
                [&](const auth::State&, auth::Mutator& commit, std::string&) {
                    produced++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    commit = [](auth::State& s) { s.data.xsts["a"] = {"tok", "uhs", "", "", 4000000000}; };
                    return true;
                },
                error);
            if (r)
                ok++;
        });
    for (auto& t : threads)
        t.join();
    CHECK(produced == 1);
    CHECK(ok == 24);
    CHECK(store.Current()->data.xsts.at("a").token == "tok");
    CHECK(store.Current()->data.revision == 1);
    CHECK(json::Read<auth::Persisted>(file::Read(dir.File()))->xsts.at("a").uhs == "uhs");
}

static void TestFailureIsSharedAndPublishesNothing() {
    Dir dir("failure");
    auth::TokenStore store(dir.File(), Opts());
    store.Publish([](auth::State& s) { s.data.xsts["keep"] = {"old", "u", "", "", 4000000000}; });
    const std::string before = file::Read(dir.File());
    std::atomic<int> produced{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++)
        threads.emplace_back([&] {
            std::string error;
            const bool r = store.Ensure(
                "xsts:b", {auth::TokenStore::Scope::Process, true, nullptr}, [](const auth::State& s) { return s.data.xsts.count("b") > 0; },
                [&](const auth::State&, auth::Mutator& commit, std::string& why) {
                    produced++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    commit = [](auth::State& s) {
                        s.data.xsts["b"] = {"half", "", "", "", 1};
                        s.data.xsts.erase("keep");
                    };
                    why = "boom";
                    return false;
                },
                error);
            if (!r && error == "boom")
                failed++;
        });
    for (auto& t : threads)
        t.join();
    CHECK(produced == 1 && failed == 8);
    CHECK(file::Read(dir.File()) == before);
    CHECK(store.Current()->data.xsts.count("keep") == 1 && store.Current()->data.xsts.count("b") == 0);
    std::string error;
    CHECK(store.Ensure(
        "xsts:b", {auth::TokenStore::Scope::Process, true, nullptr}, [](const auth::State& s) { return s.data.xsts.count("b") > 0; },
        [&](const auth::State&, auth::Mutator& commit, std::string&) {
            produced++;
            commit = [](auth::State& s) { s.data.xsts["b"] = {"ok", "u", "", "", 4000000000}; };
            return true;
        },
        error));
    CHECK(produced == 2);
}

static void TestCrossProcessSerialisation() {
    Dir dir("cross");
    auth::TokenStore one(dir.File(), Opts());
    auth::TokenStore two(dir.File(), Opts());
    std::atomic<int> produced{0};
    auto run = [&](auth::TokenStore& store) {
        std::string error;
        store.Ensure(
            "sisu", {auth::TokenStore::Scope::Account, true, nullptr}, [](const auth::State& s) { return !s.data.sisu.title.token.empty(); },
            [&](const auth::State&, auth::Mutator& commit, std::string&) {
                produced++;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                commit = [](auth::State& s) { s.data.sisu.title.token = "title"; };
                return true;
            },
            error);
    };
    std::thread a(run, std::ref(one));
    std::thread b(run, std::ref(two));
    a.join();
    b.join();
    CHECK(produced == 1);
    CHECK(one.Current()->data.sisu.title.token == "title");
    CHECK(two.Current()->data.sisu.title.token == "title");
    CHECK(one.LockTimeouts() == 0 && two.LockTimeouts() == 0);
}

static void TestNestedCrossKeys() {
    Dir dir("nested");
    auth::TokenStore store(dir.File(), Opts());
    std::string error;
    const bool ok = store.Ensure(
        "outer", {auth::TokenStore::Scope::Account, true, nullptr}, [](const auth::State& s) { return !s.data.sisu.user.token.empty(); },
        [&](const auth::State&, auth::Mutator& commit, std::string& why) {
            if (!store.Ensure(
                    "inner", {auth::TokenStore::Scope::Account, true, nullptr},
                    [](const auth::State& s) { return !s.data.deviceToken.empty(); },
                    [](const auth::State&, auth::Mutator& c, std::string&) {
                        c = [](auth::State& s) { s.data.deviceToken = "dev"; };
                        return true;
                    },
                    why))
                return false;
            commit = [](auth::State& s) { s.data.sisu.user.token = "user"; };
            return true;
        },
        error);
    CHECK(ok);
    CHECK(store.Current()->data.deviceToken == "dev" && store.Current()->data.sisu.user.token == "user");
    CHECK(store.Current()->data.revision == 2);
}

static void TestConsistentSnapshotsUnderStress() {
    Dir dir("stress");
    auth::TokenStore store(dir.File(), Opts());
    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::atomic<int> reads{0};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++)
        readers.emplace_back([&] {
            while (!stop) {
                const auto snap = store.Current();
                reads++;
                const auto& s = snap->data.sisu;
                if (s.title.token.empty())
                    continue;
                if (s.title.token.substr(1) != s.user.token.substr(1) ||
                    std::to_string(s.acquiredAt) != s.title.token.substr(1))
                    torn++;
            }
        });
    std::vector<std::thread> writers;
    std::atomic<int> sequence{1};
    for (int w = 0; w < 4; w++)
        writers.emplace_back([&] {
            for (int i = 0; i < 60; i++) {
                const int n = sequence++;
                store.Publish([n](auth::State& s) {
                    s.data.sisu.title.token = "T" + std::to_string(n);
                    s.data.sisu.user.token = "U" + std::to_string(n);
                    s.data.sisu.acquiredAt = n;
                });
                std::string error;
                store.Ensure(
                    "k" + std::to_string(n % 5), {auth::TokenStore::Scope::Process, false, nullptr},
                    [](const auth::State&) { return false; },
                    [&](const auth::State&, auth::Mutator& commit, std::string&) {
                        commit = [](auth::State& s) { s.nsalAt++; };
                        return true;
                    },
                    error);
            }
        });
    for (auto& t : writers)
        t.join();
    stop = true;
    for (auto& t : readers)
        t.join();
    CHECK(torn == 0);
    CHECK(reads > 0);
    CHECK(store.Current()->data.revision == 240);
    auto disk = json::Read<auth::Persisted>(file::Read(dir.File()));
    CHECK(disk && disk->revision == 240);
}

static void TestShutdownReleasesWaiters() {
    Dir dir("shutdown");
    auto store = std::make_shared<auth::TokenStore>(dir.File(), Opts());
    std::atomic<bool> release{false};
    std::thread leader([&] {
        std::string error;
        store->Ensure(
            "slow", {auth::TokenStore::Scope::Process, true, nullptr}, [](const auth::State& s) { return s.data.xsts.count("s") > 0; },
            [&](const auth::State&, auth::Mutator&, std::string& why) {
                while (!release)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                why = "late";
                return false;
            },
            error);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<std::thread> waiters;
    std::atomic<int> cancelled{0};
    for (int i = 0; i < 6; i++)
        waiters.emplace_back([&] {
            std::string error;
            const bool ok = store->Ensure(
                "slow", {auth::TokenStore::Scope::Process, true, nullptr}, [](const auth::State& s) { return s.data.xsts.count("s") > 0; },
                [](const auth::State&, auth::Mutator&, std::string&) { return false; }, error);
            if (!ok && error == "cancelled")
                cancelled++;
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto begin = std::chrono::steady_clock::now();
    store->Shutdown();
    for (auto& t : waiters)
        t.join();
    CHECK(cancelled == 6);
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(2));
    release = true;
    leader.join();
}

static void TestClockDrivenPolicy() {
    Dir dir("clock");
    auto clock = std::make_shared<FakeClock>();
    auth::TokenStore store(dir.File(), Opts(clock));
    const int64_t now = store.LocalNow();
    CHECK(now == 1790000000);
    store.SetSkew(30);
    CHECK(store.ServerNow() == now + 30);
    auth::Token t{"x", "u", "", "", now + 100};
    CHECK(auth::policy::TokenUsable(t, now));
    clock->nanos += 39LL * 1000000000LL;
    CHECK(store.LocalNow() == now + 39 && store.ServerNow() == now + 39 + 30);
    CHECK(auth::policy::TokenUsable(t, now + 39));
    CHECK(!auth::policy::TokenUsable(t, now + 40));
    CHECK(!auth::policy::TokenUsable(auth::Token{"", "", "", "", now + 100}, now));
    CHECK(!auth::policy::TokenUsable(auth::Token{"x", "", "", "", 0}, now));
}

namespace {
    using Scope = auth::TokenStore::Scope;
    using Clock = std::chrono::steady_clock;

    long Ms(Clock::time_point since) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
    }

    std::string KeyLockPath(const std::string& file, const std::string& key) {
        return file + ".lock.x" + auth::policy::Fingerprint(key).substr(0, 16);
    }

    bool Ensure(auth::TokenStore& store, Scope scope, const std::string& key, const std::string& token,
                std::atomic<int>& produced, int sleepMs, std::string& error, const std::atomic<bool>* cancel = nullptr) {
        return store.Ensure(
            key, {scope, true, cancel}, [key](const auth::State& s) { return s.data.xsts.count(key) > 0; },
            [&, key, token](const auth::State&, auth::Mutator& commit, std::string&) {
                produced++;
                std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                commit = [key, token](auth::State& s) { s.data.xsts[key].token = token; };
                return true;
            },
            error);
    }

    struct FakeVault final : auth::SecretVault {
        std::map<std::string, std::string> secrets;
        bool available = true;
        bool failStore = false;
        const char* Name() const override { return "fake vault"; }
        bool Available() override { return available; }
        Result Load(const std::string& id, std::string& secret) override {
            if (!available)
                return Result::Unavailable;
            const auto it = secrets.find(id);
            if (it == secrets.end())
                return Result::Missing;
            secret = it->second;
            return Result::Ok;
        }
        bool Store(const std::string& id, const std::string& secret) override {
            if (!available || failStore)
                return false;
            secrets[id] = secret;
            return true;
        }
        void Erase(const std::string& id) override { secrets.erase(id); }
    };

    auth::TokenStore::Options VaultOpts(std::shared_ptr<auth::SecretVault> vault, int lockMs = 3000) {
        auto o = Opts(nullptr, lockMs);
        o.vault = std::move(vault);
        return o;
    }

    std::string Slurp(const std::string& path) {
        return file::Read(path);
    }

    void Seed(const std::string& path, const std::string& token) {
        std::ofstream(path) << "{\"refreshToken\":\"" << token << "\"}";
    }
}

static void TestPerKeyLocksAreIndependent() {
    Dir dir("keylocks");
#ifndef _WIN32
    const int fd = open(KeyLockPath(dir.File(), "xsts:rpX").c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    auth::TokenStore store(dir.File(), Opts(nullptr, 300));
    std::atomic<int> produced{0};
    std::string error;
    auto begin = Clock::now();
    CHECK(!Ensure(store, Scope::Key, "xsts:rpX", "x", produced, 0, error));
    CHECK(Ms(begin) >= 280 && Ms(begin) < 5000);
    CHECK(produced == 0);
    CHECK(!error.empty());
    CHECK(store.LockTimeouts() >= 1);
    begin = Clock::now();
    CHECK(Ensure(store, Scope::Key, "xsts:rpY", "y", produced, 0, error));
    CHECK(Ensure(store, Scope::Account, "acct", "a", produced, 0, error));
    CHECK(Ms(begin) < 250);
    CHECK(produced == 2);
    flock(fd, LOCK_UN);
    CHECK(Ensure(store, Scope::Key, "xsts:rpX", "x", produced, 0, error));
    CHECK(produced == 3);
    close(fd);
#endif
}

static void TestKeyScopeExactlyOnceAcrossStores() {
    Dir dir("keyonce");
    auth::TokenStore one(dir.File(), Opts());
    auth::TokenStore two(dir.File(), Opts());
    std::atomic<int> produced{0};
    std::string e1, e2;
    bool ok1 = false, ok2 = false;
    std::thread a([&] { ok1 = Ensure(one, Scope::Key, "xsts:rp", "t", produced, 200, e1); });
    std::thread b([&] { ok2 = Ensure(two, Scope::Key, "xsts:rp", "t", produced, 200, e2); });
    a.join();
    b.join();
    CHECK(ok1 && ok2);
    CHECK(produced == 1);
    CHECK(one.Current()->data.xsts.count("xsts:rp") == 1);
    CHECK(two.Current()->data.xsts.count("xsts:rp") == 1);
    CHECK(one.LockTimeouts() == 0 && two.LockTimeouts() == 0);

    Dir third("keyonce_late");
    auth::TokenStore early(third.File(), Opts());
    std::string error;
    CHECK(Ensure(early, Scope::Key, "xsts:rp", "t", produced, 0, error));
    const int before = produced;
    auth::TokenStore late(third.File(), Opts());
    CHECK(Ensure(late, Scope::Key, "xsts:rp", "t", produced, 0, error));
    CHECK(produced == before);
}

static void TestDifferentKeysProgressTogether() {
    Dir dir("keysparallel");
    auth::TokenStore one(dir.File(), Opts());
    auth::TokenStore two(dir.File(), Opts());
    std::atomic<int> produced{0};
    std::string e1, e2;
    bool ok1 = false, ok2 = false;
    const auto begin = Clock::now();
    std::thread a([&] { ok1 = Ensure(one, Scope::Key, "xsts:rpA", "a", produced, 400, e1); });
    std::thread b([&] { ok2 = Ensure(two, Scope::Key, "xsts:rpB", "b", produced, 400, e2); });
    a.join();
    b.join();
    CHECK(ok1 && ok2);
    CHECK(produced == 2);
    CHECK(Ms(begin) < 750);
    one.Refresh();
    CHECK(one.Current()->data.xsts.count("xsts:rpA") == 1 && one.Current()->data.xsts.count("xsts:rpB") == 1);
}

static void TestAccountsAreIndependent() {
    Dir dir("accounts");
    const std::string fileA = (dir.path / "a.json").string();
    const std::string fileB = (dir.path / "b.json").string();
    auth::TokenStore a1(fileA, Opts()), a2(fileA, Opts()), b1(fileB, Opts()), b2(fileB, Opts());
    std::atomic<int> producedA{0}, producedB{0};
    std::vector<std::string> errors(4);
    std::vector<int> ok(4, 0);
    const auto begin = Clock::now();
    std::vector<std::thread> threads;
    threads.emplace_back([&] { ok[0] = Ensure(a1, Scope::Account, "sisu", "A", producedA, 400, errors[0]); });
    threads.emplace_back([&] { ok[1] = Ensure(a2, Scope::Account, "sisu", "A", producedA, 400, errors[1]); });
    threads.emplace_back([&] { ok[2] = Ensure(b1, Scope::Account, "sisu", "B", producedB, 400, errors[2]); });
    threads.emplace_back([&] { ok[3] = Ensure(b2, Scope::Account, "sisu", "B", producedB, 400, errors[3]); });
    for (auto& t : threads)
        t.join();
    CHECK(ok[0] && ok[1] && ok[2] && ok[3]);
    CHECK(producedA == 1 && producedB == 1);
    CHECK(Ms(begin) < 750);
    CHECK(a2.Current()->data.xsts.at("sisu").token == "A");
    CHECK(b2.Current()->data.xsts.at("sisu").token == "B");
    CHECK(Slurp(fileA).find("\"B\"") == std::string::npos);
    CHECK(Slurp(fileB).find("\"A\"") == std::string::npos);

#ifndef _WIN32
    const int fd = open((fileA + ".lock").c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    auth::TokenStore blockedA(fileA, Opts(nullptr, 200));
    auth::TokenStore freeB(fileB, Opts(nullptr, 200));
    std::string error;
    std::atomic<int> produced{0};
    CHECK(!Ensure(blockedA, Scope::Account, "other", "x", produced, 0, error));
    const auto start = Clock::now();
    CHECK(Ensure(freeB, Scope::Account, "other", "x", produced, 0, error));
    CHECK(Ms(start) < 150);
    flock(fd, LOCK_UN);
    close(fd);
#endif
}

static void TestSignOutOnlyAffectsOneAccountAndDropsLatePublish() {
    Dir dir("signout");
    const std::string fileA = (dir.path / "a.json").string();
    const std::string fileB = (dir.path / "b.json").string();
    Seed(fileA, "refresh-A");
    Seed(fileB, "refresh-B");
    auth::TokenStore a(fileA, Opts());
    auth::TokenStore b(fileB, Opts());
    std::atomic<bool> started{false}, release{false};
    std::string error;
    bool lateOk = true;
    std::thread late([&] {
        lateOk = a.Ensure(
            "xsts:late", {Scope::Key, true, nullptr}, [](const auth::State& s) { return s.data.xsts.count("xsts:late") > 0; },
            [&](const auth::State&, auth::Mutator& commit, std::string&) {
                started = true;
                while (!release)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                commit = [](auth::State& s) { s.data.xsts["xsts:late"].token = "stale"; };
                return true;
            },
            error);
    });
    while (!started)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::thread out([&] { a.Clear(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    release = true;
    late.join();
    out.join();
    CHECK(!lateOk);
    CHECK(a.Retired());
    CHECK(!fs::exists(fileA));
    CHECK(a.Current()->data.xsts.empty() && a.Current()->data.refreshToken.empty());
    CHECK(b.Current()->data.refreshToken == "refresh-B");
    CHECK(Slurp(fileB).find("refresh-B") != std::string::npos);
    std::atomic<int> produced{0};
    CHECK(!Ensure(a, Scope::Account, "again", "x", produced, 0, error));
    CHECK(produced == 0 && !fs::exists(fileA));
    CHECK(Ensure(b, Scope::Account, "still", "x", produced, 0, error));
}

static void TestCancellationWhileWaitingForCoordination() {
#ifndef _WIN32
    Dir dir("cancelwait");
    const int fd = open((dir.File() + ".lock").c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    auth::TokenStore store(dir.File(), Opts(nullptr, 60000));
    std::atomic<bool> cancel{false};
    std::atomic<int> produced{0};
    std::string error;
    bool ok = true;
    long elapsed = 0;
    std::thread waiter([&] {
        const auto begin = Clock::now();
        ok = Ensure(store, Scope::Account, "k", "t", produced, 0, error, &cancel);
        elapsed = Ms(begin);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    cancel = true;
    waiter.join();
    CHECK(!ok);
    CHECK(error == "cancelled");
    CHECK(elapsed >= 100 && elapsed < 1500);
    CHECK(produced == 0);
    CHECK(store.LockTimeouts() == 0);

    std::atomic<bool> cancelKey{false};
    const int keyFd = open(KeyLockPath(dir.File(), "xsts:k").c_str(), O_RDWR | O_CREAT, 0600);
    CHECK(keyFd >= 0 && flock(keyFd, LOCK_EX | LOCK_NB) == 0);
    flock(fd, LOCK_UN);
    std::thread keyWaiter([&] { ok = Ensure(store, Scope::Key, "xsts:k", "t", produced, 0, error, &cancelKey); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cancelKey = true;
    keyWaiter.join();
    CHECK(!ok && produced == 0);
    flock(keyFd, LOCK_UN);
    close(keyFd);
    close(fd);
#endif
}

static void TestLockHolderTerminationReleasesLock() {
#ifndef _WIN32
    Dir dir("killed");
    const std::string lockFile = dir.File() + ".lock";
    int ready[2];
    CHECK(pipe(ready) == 0);
    const pid_t child = fork();
    if (child == 0) {
        const int fd = open(lockFile.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0)
            _exit(2);
        const char byte = 'x';
        if (write(ready[1], &byte, 1) != 1)
            _exit(3);
        for (;;)
            pause();
    }
    char byte = 0;
    CHECK(read(ready[0], &byte, 1) == 1);
    std::atomic<int> produced{0};
    std::string error;
    {
        auth::TokenStore blocked(dir.File(), Opts(nullptr, 200));
        CHECK(!Ensure(blocked, Scope::Account, "k", "t", produced, 0, error));
        CHECK(produced == 0);
    }
    kill(child, SIGKILL);
    int status = 0;
    waitpid(child, &status, 0);
    CHECK(WIFSIGNALED(status));
    auth::TokenStore after(dir.File(), Opts(nullptr, 2000));
    const auto begin = Clock::now();
    CHECK(Ensure(after, Scope::Account, "k", "t", produced, 0, error));
    CHECK(Ms(begin) < 1500);
    CHECK(after.LockTimeouts() == 0);
    CHECK(produced == 1);
    close(ready[0]);
    close(ready[1]);
#endif
}

static void TestUnopenableLockDegradesToInProcess() {
#ifndef _WIN32
    Dir dir("degraded");
    fs::create_directories(dir.File() + ".lock");
    auth::TokenStore store(dir.File(), Opts(nullptr, 500));
    std::atomic<int> produced{0};
    std::string error;
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int i = 0; i < 4; i++)
        threads.emplace_back([&] {
            std::string e;
            if (Ensure(store, Scope::Key, "xsts:r", "t", produced, 100, e))
                ok++;
        });
    for (auto& t : threads)
        t.join();
    CHECK(ok == 4);
    CHECK(produced == 1);
#endif
}

static void TestMigrationFromPlaintextToVault() {
    Dir dir("migrate");
    Seed(dir.File(), "refresh-secret-value");
    auto vault = std::make_shared<FakeVault>();
    {
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        CHECK(store.Current()->data.refreshToken == "refresh-secret-value");
        CHECK(vault->secrets.size() == 1);
        const std::string disk = Slurp(dir.File());
        CHECK(auth::IsEncrypted(disk));
        CHECK(disk.find("refresh-secret-value") == std::string::npos);
        CHECK(!store.Locked());
    }
    auth::TokenStore reopened(dir.File(), VaultOpts(vault));
    CHECK(reopened.Current()->data.refreshToken == "refresh-secret-value");
    reopened.Publish([](auth::State& s) { s.data.deviceId = "device-1"; });
    CHECK(Slurp(dir.File()).find("device-1") == std::string::npos);
    auth::TokenStore again(dir.File(), VaultOpts(vault));
    CHECK(again.Current()->data.deviceId == "device-1");
    CHECK(again.Current()->data.refreshToken == "refresh-secret-value");
}

static void TestInterruptedMigrationRecovers() {
    Dir dir("interrupted");
    Seed(dir.File(), "refresh-keep");
    std::ofstream(dir.File() + ".tmp.99999.1") << "half written";
    auto vault = std::make_shared<FakeVault>();
    vault->secrets[dir.File()] = auth::NewDataKey();
    auth::TokenStore store(dir.File(), VaultOpts(vault));
    CHECK(store.Current()->data.refreshToken == "refresh-keep");
    CHECK(auth::IsEncrypted(Slurp(dir.File())));
    auth::TokenStore again(dir.File(), VaultOpts(vault));
    CHECK(again.Current()->data.refreshToken == "refresh-keep");

    Dir partial("interrupted_store");
    Seed(partial.File(), "refresh-keep2");
    auto failing = std::make_shared<FakeVault>();
    failing->failStore = true;
    {
        auth::TokenStore s(partial.File(), VaultOpts(failing));
        CHECK(s.Current()->data.refreshToken == "refresh-keep2");
        CHECK(!auth::IsEncrypted(Slurp(partial.File())));
        CHECK(Slurp(partial.File()).find("refresh-keep2") != std::string::npos);
    }
    failing->failStore = false;
    auth::TokenStore healed(partial.File(), VaultOpts(failing));
    CHECK(healed.Current()->data.refreshToken == "refresh-keep2");
    CHECK(auth::IsEncrypted(Slurp(partial.File())));
}

static void TestVaultUnavailableFallsBackToPrivateFile() {
    Dir dir("novault");
    auto vault = std::make_shared<FakeVault>();
    vault->available = false;
    {
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        store.Publish([](auth::State& s) { s.data.refreshToken = "plain-token"; });
        const std::string disk = Slurp(dir.File());
        CHECK(!auth::IsEncrypted(disk));
        CHECK(disk.find("plain-token") != std::string::npos);
        CHECK(store.VaultName() == "private file");
#ifndef _WIN32
        struct stat st{};
        CHECK(stat(dir.File().c_str(), &st) == 0 && (st.st_mode & 077) == 0);
#endif
    }
    vault->available = true;
    auth::TokenStore later(dir.File(), VaultOpts(vault));
    CHECK(later.Current()->data.refreshToken == "plain-token");
    CHECK(auth::IsEncrypted(Slurp(dir.File())));

    auth::TokenStore nullVault(dir.File() + "2", Opts());
    nullVault.Publish([](auth::State& s) { s.data.refreshToken = "p2"; });
    CHECK(Slurp(dir.File() + "2").find("p2") != std::string::npos);
}

static void TestEncryptedCacheWhileVaultLockedIsPreserved() {
    Dir dir("locked");
    auto vault = std::make_shared<FakeVault>();
    {
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        store.Publish([](auth::State& s) { s.data.refreshToken = "encrypted-token"; });
    }
    const std::string original = Slurp(dir.File());
    CHECK(auth::IsEncrypted(original));
    vault->available = false;
    {
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        CHECK(store.Current()->data.refreshToken.empty());
        CHECK(store.Locked());
        CHECK(Slurp(dir.File()) == original);
        store.Publish([](auth::State& s) { s.data.refreshToken = "new-signin"; });
        CHECK(Slurp(dir.File() + ".locked") == original);
        CHECK(Slurp(dir.File()).find("new-signin") != std::string::npos);
    }
}

static void TestCorruptSecureStorageData() {
    {
        Dir dir("gcm");
        auto vault = std::make_shared<FakeVault>();
        {
            auth::TokenStore store(dir.File(), VaultOpts(vault));
            store.Publish([](auth::State& s) { s.data.refreshToken = "t"; });
        }
        std::string disk = Slurp(dir.File());
        disk[disk.size() - 20] = static_cast<char>(disk[disk.size() - 20] ^ 0x5a);
        std::ofstream(dir.File(), std::ios::binary) << disk;
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        CHECK(store.Current()->data.refreshToken.empty());
        CHECK(fs::exists(dir.File() + ".corrupt"));
        CHECK(!store.Locked());
        store.Publish([](auth::State& s) { s.data.refreshToken = "fresh"; });
        auth::TokenStore again(dir.File(), VaultOpts(vault));
        CHECK(again.Current()->data.refreshToken == "fresh");
    }
    {
        Dir dir("keylost");
        auto vault = std::make_shared<FakeVault>();
        {
            auth::TokenStore store(dir.File(), VaultOpts(vault));
            store.Publish([](auth::State& s) { s.data.refreshToken = "t"; });
        }
        vault->secrets.clear();
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        CHECK(store.Current()->data.refreshToken.empty());
        CHECK(fs::exists(dir.File() + ".corrupt"));
        CHECK(!store.Locked());
    }
    {
        Dir dir("truncated");
        auto vault = std::make_shared<FakeVault>();
        {
            auth::TokenStore store(dir.File(), VaultOpts(vault));
            store.Publish([](auth::State& s) { s.data.refreshToken = "t"; });
        }
        std::string disk = Slurp(dir.File());
        std::ofstream(dir.File(), std::ios::binary) << disk.substr(0, 14);
        auth::TokenStore store(dir.File(), VaultOpts(vault));
        CHECK(store.Current()->data.refreshToken.empty());
        CHECK(fs::exists(dir.File() + ".corrupt"));
    }
}

static void TestVaultKeysAreScopedPerAccount() {
    Dir dir("vaultscope");
    auto vault = std::make_shared<FakeVault>();
    const std::string fileA = (dir.path / "a.json").string();
    const std::string fileB = (dir.path / "b.json").string();
    auth::TokenStore a(fileA, VaultOpts(vault));
    auth::TokenStore b(fileB, VaultOpts(vault));
    a.Publish([](auth::State& s) { s.data.refreshToken = "A"; });
    b.Publish([](auth::State& s) { s.data.refreshToken = "B"; });
    CHECK(vault->secrets.size() == 2);
    CHECK(vault->secrets[fileA] != vault->secrets[fileB]);
    a.Clear();
    CHECK(vault->secrets.size() == 1 && vault->secrets.count(fileB) == 1);
    auth::TokenStore bAgain(fileB, VaultOpts(vault));
    CHECK(bAgain.Current()->data.refreshToken == "B");
}

int main() {
    TestSingleFlight();
    TestFailureIsSharedAndPublishesNothing();
    TestCrossProcessSerialisation();
    TestNestedCrossKeys();
    TestConsistentSnapshotsUnderStress();
    TestShutdownReleasesWaiters();
    TestClockDrivenPolicy();
    TestPerKeyLocksAreIndependent();
    TestKeyScopeExactlyOnceAcrossStores();
    TestDifferentKeysProgressTogether();
    TestAccountsAreIndependent();
    TestSignOutOnlyAffectsOneAccountAndDropsLatePublish();
    TestCancellationWhileWaitingForCoordination();
    TestLockHolderTerminationReleasesLock();
    TestUnopenableLockDegradesToInProcess();
    TestMigrationFromPlaintextToVault();
    TestInterruptedMigrationRecovers();
    TestVaultUnavailableFallsBackToPrivateFile();
    TestEncryptedCacheWhileVaultLockedIsPreserved();
    TestCorruptSecureStorageData();
    TestVaultKeysAreScopedPerAccount();
    if (failures) {
        std::printf("%d check(s) failed\n", failures.load());
        return 1;
    }
    std::printf("token_store_selftest: ok\n");
    return 0;
}
