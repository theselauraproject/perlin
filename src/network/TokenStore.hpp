#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin::auth {

    class Clock {
    public:
        virtual ~Clock() = default;
        virtual int64_t NowNanos() const = 0;
        int64_t NowSec() const { return NowNanos() / 1000000000LL; }
    };

    std::shared_ptr<const Clock> SystemClock();

    struct Token {
        std::string token;
        std::string uhs;
        std::string gtg;
        std::string xid;
        int64_t notAfter = 0;
    };

    struct SisuState {
        Token title;
        Token user;
        int64_t acquiredAt = 0;
        std::string deviceBinding;
    };

    struct Persisted {
        int version = 0;
        int64_t revision = 0;
        std::string refreshToken;
        std::string proofKeyPem;
        std::string deviceId;
        std::string deviceToken;
        int64_t deviceNotAfter = 0;
        std::string minecraftDeviceId;
        SisuState sisu;
        std::map<std::string, Token> xsts;
    };

    struct SignaturePolicy {
        uint32_t version = 1;
        size_t maxBody = 0;
        std::vector<std::string> extraHeaders;
    };

    struct NsalEndpoint {
        std::string protocol, host, hostType, path, relyingParty;
        int port = 0;
        int policy = -1;
    };

    struct Nsal {
        std::vector<NsalEndpoint> endpoints;
        std::vector<SignaturePolicy> policies;
    };

    struct State {
        Persisted data;
        std::string msaAccess;
        int64_t msaExpiry = 0;
        std::shared_ptr<const Nsal> nsal;
        int64_t nsalAt = 0;
    };

    using Snapshot = std::shared_ptr<const State>;

    namespace policy {
        constexpr int kSchema = 2;
        constexpr int64_t kSisuMaxAgeSec = 14LL * 24 * 3600;
        constexpr int64_t kMarginSec = 60;
        constexpr int64_t kClockSkewToleranceSec = 300;
        constexpr int64_t kNsalTtlSec = 3600;
        constexpr int64_t kNsalRetrySec = 300;

        bool TokenUsable(const Token& t, int64_t serverNow);
        bool DeviceUsable(const Persisted& p, int64_t serverNow);
        bool SisuUsable(const Persisted& p, int64_t localNow, int64_t serverNow);
        bool XstsUsable(const Persisted& p, const std::string& relyingParty, int64_t serverNow);
        bool MsaUsable(const State& s, int64_t localNow);
        std::string Fingerprint(const std::string& text);
    }

    using Mutator = std::function<void(State&)>;
    using Freshness = std::function<bool(const State&)>;
    using Producer = std::function<bool(const State&, Mutator& commit, std::string& error)>;

    class SecretVault;

    class TokenStore {
    public:
        struct Options {
            std::shared_ptr<const Clock> clock;
            int lockTimeoutMs = 90000;
            std::shared_ptr<SecretVault> vault;
        };

        enum class Scope { Process, Key, Account };

        static std::shared_ptr<TokenStore> Open(const std::string& path);
        static void Forget(const std::string& path);
        explicit TokenStore(std::string path, Options options);
        explicit TokenStore(std::string path) : TokenStore(std::move(path), Options{}) {}
        ~TokenStore();
        TokenStore(const TokenStore&) = delete;
        TokenStore& operator=(const TokenStore&) = delete;

        const std::string& Path() const { return path; }
        const Clock& GetClock() const { return *clock; }
        int64_t LocalNow() const { return clock->NowSec(); }
        int64_t ServerNow() const { return clock->NowSec() + skew.load(); }
        int64_t ServerNanos() const { return clock->NowNanos() + skew.load() * 1000000000LL; }
        void SetSkew(int64_t seconds) { skew.store(seconds); }

        Snapshot Current() const;
        Snapshot Refresh();

        struct EnsureOptions {
            Scope scope = Scope::Process;
            bool persist = true;
            const std::atomic<bool>* cancel = nullptr;
        };

        bool Ensure(const std::string& key, const EnsureOptions& options, const Freshness& fresh,
                    const Producer& produce, std::string& error);

        void Publish(const Mutator& mutate);
        void PublishTransient(const Mutator& mutate);
        void Clear();
        void Shutdown();
        bool IsShutdown() const { return shutdown.load(); }

        bool Retired() const { return retired.load(); }
        bool Locked() const { return locked.load(); }
        int LockedWrites() const { return writes.load(); }
        int LockTimeouts() const { return lockTimeouts.load(); }
        std::string VaultName() const;

    private:
        struct Flight {
            bool done = false;
            bool ok = false;
            std::string error;
        };

        class FileLock;
        enum class LockResult { Held, Degraded, Cancelled, TimedOut };
        struct Guard;

        FileLock& LockFor(const std::string& suffix);
        LockResult Take(FileLock& lock, const std::atomic<bool>* cancel, int timeoutMs);
        void Release(FileLock& lock);
        Snapshot LoadFromDisk(const Snapshot& memory);
        bool WriteToDisk(const Persisted& data);
        bool DataKey(bool create, std::string& key);
        void MigrateToVault();
        void PublishLocked(const Mutator& mutate);

        std::string path;
        std::shared_ptr<const Clock> clock;
        int lockTimeoutMs;
        std::shared_ptr<SecretVault> vault;
        std::atomic<int64_t> skew{0};
        std::atomic<bool> shutdown{false};
        std::atomic<bool> retired{false};
        std::atomic<bool> locked{false};
        std::atomic<int> writes{0};
        std::atomic<int> lockTimeouts{0};
        std::atomic<bool> plainOnDisk{false};
        std::mutex keyMutex;
        std::string dataKey;

        mutable std::mutex mu;
        std::condition_variable cv;
        Snapshot current;
        std::map<std::string, std::shared_ptr<Flight>> flights;

        std::mutex locksMutex;
        std::map<std::string, std::unique_ptr<FileLock>> locks;
    };
}
