#include "network/TokenStore.hpp"

#include "network/AtomicFile.hpp"
#include "network/SecretVault.hpp"
#include "util/FileIo.hpp"
#include "util/Json.hpp"
#include "util/Log.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace perlin::auth {
    namespace {
        class RealClock final : public Clock {
        public:
            int64_t NowNanos() const override {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                    .count();
            }
        };
    }

    std::shared_ptr<const Clock> SystemClock() {
        static const std::shared_ptr<const Clock> clock = std::make_shared<RealClock>();
        return clock;
    }

    namespace policy {
        bool TokenUsable(const Token& t, int64_t serverNow) {
            return !t.token.empty() && t.notAfter > 0 && serverNow < t.notAfter - kMarginSec;
        }

        bool DeviceUsable(const Persisted& p, int64_t serverNow) {
            return !p.deviceToken.empty() && p.deviceNotAfter > 0 && serverNow < p.deviceNotAfter - kMarginSec;
        }

        std::string Fingerprint(const std::string& text) {
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int len = 0;
            EVP_Digest(text.data(), text.size(), digest, &len, EVP_sha256(), nullptr);
            static const char* hex = "0123456789abcdef";
            std::string out;
            for (unsigned int i = 0; i < len; i++) {
                out += hex[digest[i] >> 4];
                out += hex[digest[i] & 15];
            }
            return out;
        }

        bool SisuUsable(const Persisted& p, int64_t localNow, int64_t serverNow) {
            const SisuState& s = p.sisu;
            if (!TokenUsable(s.title, serverNow) || !TokenUsable(s.user, serverNow))
                return false;
            if (p.deviceToken.empty() || s.deviceBinding != Fingerprint(p.deviceToken))
                return false;
            if (s.acquiredAt <= 0 || s.acquiredAt > localNow + kClockSkewToleranceSec)
                return false;
            return localNow < s.acquiredAt + kSisuMaxAgeSec;
        }

        bool XstsUsable(const Persisted& p, const std::string& relyingParty, int64_t serverNow) {
            const auto it = p.xsts.find(relyingParty);
            return it != p.xsts.end() && TokenUsable(it->second, serverNow) && !it->second.uhs.empty();
        }

        bool MsaUsable(const State& s, int64_t localNow) {
            return !s.msaAccess.empty() && localNow < s.msaExpiry - kMarginSec;
        }
    }

    class TokenStore::FileLock {
    public:
        std::mutex m;
        std::condition_variable cv;
        std::thread::id owner;
        int depth = 0;
        std::string path;
#ifdef _WIN32
        void* handle = INVALID_HANDLE_VALUE;
#else
        int fd = -1;
#endif
        bool held = false;
        bool openFailed = false;
    };

    namespace {
        constexpr const char* kLegacySealedPrefix = "PRLNSEAL1:";

        bool UnsealLegacy(const std::string& stored, std::string& plain) {
#ifdef _WIN32
            const std::string_view body = std::string_view(stored).substr(std::string_view(kLegacySealedPrefix).size());
            DATA_BLOB in{static_cast<DWORD>(body.size()), reinterpret_cast<BYTE*>(const_cast<char*>(body.data()))};
            DATA_BLOB out{};
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
                return false;
            plain.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
            LocalFree(out.pbData);
            return true;
#else
            (void)stored;
            (void)plain;
            return false;
#endif
        }

        std::mutex g_registryMutex;
        std::map<std::string, std::weak_ptr<TokenStore>>& Registry() {
            static std::map<std::string, std::weak_ptr<TokenStore>> registry;
            return registry;
        }

        std::string CanonicalPath(const std::string& path) {
            std::error_code ec;
            std::string key = std::filesystem::weakly_canonical(path, ec).string();
            return ec || key.empty() ? path : key;
        }
    }

    std::shared_ptr<TokenStore> TokenStore::Open(const std::string& path) {
        const std::string key = CanonicalPath(path);
        std::lock_guard<std::mutex> lock(g_registryMutex);
        auto& slot = Registry()[key];
        if (auto existing = slot.lock())
            return existing;
        Options options;
        options.vault = DefaultVault();
        auto created = std::make_shared<TokenStore>(key, options);
        slot = created;
        return created;
    }

    void TokenStore::Forget(const std::string& path) {
        const std::string key = CanonicalPath(path);
        std::shared_ptr<TokenStore> store;
        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            auto& registry = Registry();
            const auto it = registry.find(key);
            if (it != registry.end()) {
                store = it->second.lock();
                registry.erase(it);
            }
        }
        if (!store) {
            Options options;
            options.vault = DefaultVault();
            store = std::make_shared<TokenStore>(key, options);
        }
        store->Clear();
    }

    TokenStore::TokenStore(std::string p, Options options)
        : path(std::move(p)), clock(options.clock ? std::move(options.clock) : SystemClock()),
          lockTimeoutMs(options.lockTimeoutMs), vault(std::move(options.vault)) {
        auto empty = std::make_shared<State>();
        current = LoadFromDisk(empty);
        if (plainOnDisk && vault && vault->Available())
            MigrateToVault();
    }

    TokenStore::~TokenStore() {
        Shutdown();
        for (auto& [name, lock] : locks) {
#ifdef _WIN32
            if (lock->handle != INVALID_HANDLE_VALUE)
                CloseHandle(lock->handle);
#else
            if (lock->fd >= 0)
                close(lock->fd);
#endif
        }
    }

    std::string TokenStore::VaultName() const {
        return vault && vault->Available() ? vault->Name() : "private file";
    }

    Snapshot TokenStore::Current() const {
        std::lock_guard<std::mutex> lock(mu);
        return current;
    }

    bool TokenStore::DataKey(bool create, std::string& key) {
        if (!vault)
            return false;
        std::lock_guard<std::mutex> keyLock(keyMutex);
        if (dataKey.empty()) {
            switch (vault->Load(path, dataKey)) {
            case SecretVault::Result::Ok:
                break;
            case SecretVault::Result::Unavailable:
                dataKey.clear();
                return false;
            case SecretVault::Result::Missing:
                dataKey.clear();
                if (!create || !vault->Available())
                    return false;
                dataKey = NewDataKey();
                if (dataKey.empty() || !vault->Store(path, dataKey)) {
                    dataKey.clear();
                    return false;
                }
                break;
            }
        }
        key = dataKey;
        return true;
    }

    Snapshot TokenStore::LoadFromDisk(const Snapshot& memory) {
        std::string stored;
        if (!file::Read(path, stored))
            return memory;
        std::string text;
        bool readable = true;
        bool wasPlain = false;
        if (IsEncrypted(stored)) {
            std::string key;
            const bool haveKey = DataKey(false, key);
            if (!haveKey) {
                const bool vaultDown = !vault || vault->Load(path, key) == SecretVault::Result::Unavailable;
                if (vaultDown) {
                    if (!locked.exchange(true))
                        Log("auth", "token cache {} is encrypted and no secure storage is available; leaving it untouched", path);
                    return memory;
                }
                readable = false;
            } else if (!Decrypt(key, stored, text)) {
                {
                    std::lock_guard<std::mutex> keyLock(keyMutex);
                    dataKey.clear();
                }
                std::string retry;
                readable = DataKey(false, retry) && Decrypt(retry, stored, text);
            }
        } else if (stored.starts_with(kLegacySealedPrefix)) {
            readable = UnsealLegacy(stored, text);
            wasPlain = true;
        } else {
            text = stored;
            wasPlain = true;
        }
        std::optional<Persisted> parsed;
        if (readable)
            parsed = json::Read<Persisted>(text);
        if (!parsed) {
            std::error_code ec;
            std::filesystem::rename(path, path + ".corrupt", ec);
            Log("auth", "token cache {} was unreadable and has been set aside", path);
            return memory;
        }
        if (parsed->version > policy::kSchema) {
            Log("auth", "token cache {} uses a newer format; ignoring it", path);
            return memory;
        }
        locked = false;
        plainOnDisk = wasPlain;
        const bool legacyFirstLoad = memory->data.revision == 0 && memory->data.refreshToken.empty() &&
                                     memory->data.deviceToken.empty() && memory->data.proofKeyPem.empty();
        if (parsed->revision <= memory->data.revision && !legacyFirstLoad)
            return memory;
        auto next = std::make_shared<State>(*memory);
        next->data = std::move(*parsed);
        return next;
    }

    Snapshot TokenStore::Refresh() {
        std::lock_guard<std::mutex> lock(mu);
        current = LoadFromDisk(current);
        return current;
    }

    bool TokenStore::WriteToDisk(const Persisted& data) {
        const std::string text = json::Write(data);
        if (text.empty())
            return false;
        if (locked.exchange(false)) {
            std::error_code ec;
            if (!std::filesystem::exists(path + ".locked", ec))
                std::filesystem::rename(path, path + ".locked", ec);
        }
        std::string bytes = text;
        std::string key;
        if (vault && vault->Available() && DataKey(true, key)) {
            std::string sealed = Encrypt(key, text);
            if (!sealed.empty())
                bytes = std::move(sealed);
        }
        const bool ok = WriteFileAtomic(path, bytes);
        if (ok) {
            writes++;
            plainOnDisk = !IsEncrypted(bytes);
        } else {
            Log("auth", "could not save token cache {}", path);
        }
        return ok;
    }

    void TokenStore::MigrateToVault() {
        FileLock& account = LockFor("");
        if (Take(account, nullptr, 1500) != LockResult::Held)
            return;
        {
            std::lock_guard<std::mutex> lock(mu);
            current = LoadFromDisk(current);
            if (plainOnDisk) {
                if (WriteToDisk(current->data) && !plainOnDisk)
                    Log("auth", "migrated token cache {} to {}", path, vault->Name());
            }
        }
        Release(account);
    }

    TokenStore::FileLock& TokenStore::LockFor(const std::string& suffix) {
        std::lock_guard<std::mutex> lock(locksMutex);
        auto& slot = locks[suffix];
        if (!slot) {
            slot = std::make_unique<FileLock>();
            slot->path = path + ".lock" + suffix;
        }
        return *slot;
    }

    TokenStore::LockResult TokenStore::Take(FileLock& lock, const std::atomic<bool>* cancel, int timeoutMs) {
        const auto self = std::this_thread::get_id();
        auto stop = [&] { return shutdown.load() || (cancel && cancel->load()); };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        {
            std::unique_lock<std::mutex> l(lock.m);
            if (lock.depth > 0 && lock.owner == self) {
                lock.depth++;
                return LockResult::Held;
            }
            while (lock.depth > 0) {
                if (stop())
                    return LockResult::Cancelled;
                if (std::chrono::steady_clock::now() >= deadline) {
                    lockTimeouts++;
                    return LockResult::TimedOut;
                }
                lock.cv.wait_for(l, std::chrono::milliseconds(20));
            }
            if (stop())
                return LockResult::Cancelled;
            lock.owner = self;
            lock.depth = 1;
            lock.held = false;
        }
        auto giveUp = [&](LockResult why) {
            std::lock_guard<std::mutex> l(lock.m);
            lock.depth = 0;
            lock.owner = std::thread::id();
            lock.cv.notify_all();
            return why;
        };
        EnsurePrivateDirectory(std::filesystem::path(lock.path).parent_path());
#ifdef _WIN32
        if (lock.handle == INVALID_HANDLE_VALUE && !lock.openFailed) {
            lock.handle = CreateFileA(lock.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            lock.openFailed = lock.handle == INVALID_HANDLE_VALUE;
        }
        const bool usable = lock.handle != INVALID_HANDLE_VALUE;
#else
        if (lock.fd < 0 && !lock.openFailed) {
            lock.fd = open(lock.path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            lock.openFailed = lock.fd < 0;
        }
        const bool usable = lock.fd >= 0;
#endif
        if (!usable) {
            Log("auth", "cannot open lock file {}; coordinating inside this process only", lock.path);
            return LockResult::Degraded;
        }
        bool got = false;
        while (true) {
#ifdef _WIN32
            OVERLAPPED ov{};
            got = LockFileEx(lock.handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov) != 0;
#else
            got = flock(lock.fd, LOCK_EX | LOCK_NB) == 0;
#endif
            if (got)
                break;
            if (stop())
                return giveUp(LockResult::Cancelled);
            if (std::chrono::steady_clock::now() >= deadline) {
                lockTimeouts++;
                Log("auth", "timed out waiting for another Perlin instance to release {}", lock.path);
                return giveUp(LockResult::TimedOut);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        std::lock_guard<std::mutex> l(lock.m);
        lock.held = true;
        return LockResult::Held;
    }

    void TokenStore::Release(FileLock& lock) {
        std::lock_guard<std::mutex> l(lock.m);
        if (--lock.depth > 0)
            return;
        if (lock.held) {
#ifdef _WIN32
            OVERLAPPED ov{};
            UnlockFileEx(lock.handle, 0, 1, 0, &ov);
#else
            flock(lock.fd, LOCK_UN);
#endif
        }
        lock.held = false;
        lock.owner = std::thread::id();
        lock.cv.notify_all();
    }

    struct TokenStore::Guard {
        TokenStore& store;
        FileLock* lock = nullptr;
        LockResult result = LockResult::Cancelled;
        Guard(TokenStore& s, FileLock& l, const std::atomic<bool>* cancel) : store(s) {
            result = store.Take(l, cancel, store.lockTimeoutMs);
            if (result == LockResult::Held || result == LockResult::Degraded)
                lock = &l;
        }
        ~Guard() {
            if (lock)
                store.Release(*lock);
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        bool Owned() const { return lock != nullptr; }
        std::string Failure() const {
            return result == LockResult::TimedOut
                       ? "timed out waiting for another Perlin instance that is signing in with this account"
                       : "cancelled";
        }
    };

    void TokenStore::PublishLocked(const Mutator& mutate) {
        Snapshot base;
        {
            std::lock_guard<std::mutex> lock(mu);
            current = LoadFromDisk(current);
            base = current;
        }
        auto next = std::make_shared<State>(*base);
        mutate(*next);
        next->data.version = policy::kSchema;
        next->data.revision = base->data.revision + 1;
        const Persisted toWrite = next->data;
        {
            std::lock_guard<std::mutex> lock(mu);
            current = next;
        }
        WriteToDisk(toWrite);
    }

    void TokenStore::PublishTransient(const Mutator& mutate) {
        std::lock_guard<std::mutex> lock(mu);
        auto next = std::make_shared<State>(*current);
        mutate(*next);
        current = next;
    }

    void TokenStore::Publish(const Mutator& mutate) {
        if (retired.load())
            return;
        Guard g(*this, LockFor(""), nullptr);
        if (!g.Owned()) {
            Log("auth", "could not lock {} to save tokens; keeping them in memory only", path);
            PublishTransient(mutate);
            return;
        }
        if (retired.load())
            return;
        PublishLocked(mutate);
    }

    void TokenStore::Clear() {
        retired = true;
        Guard g(*this, LockFor(""), nullptr);
        {
            std::lock_guard<std::mutex> lock(mu);
            auto empty = std::make_shared<State>();
            empty->data.revision = current->data.revision + 1;
            current = empty;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path + ".corrupt", ec);
        std::filesystem::remove(path + ".locked", ec);
        if (vault)
            vault->Erase(path);
        {
            std::lock_guard<std::mutex> keyLock(keyMutex);
            dataKey.clear();
        }
        std::lock_guard<std::mutex> l(locksMutex);
        for (const auto& [suffix, lock] : locks)
            std::filesystem::remove(lock->path, ec);
    }

    void TokenStore::Shutdown() {
        shutdown.store(true);
        {
            std::lock_guard<std::mutex> lock(mu);
        }
        cv.notify_all();
        std::lock_guard<std::mutex> l(locksMutex);
        for (const auto& [suffix, lock] : locks) {
            std::lock_guard<std::mutex> inner(lock->m);
            lock->cv.notify_all();
        }
    }

    bool TokenStore::Ensure(const std::string& key, const EnsureOptions& options, const Freshness& fresh,
                            const Producer& produce, std::string& error) {
        const std::atomic<bool>* cancel = options.cancel;
        auto cancelled = [&] { return shutdown.load() || (cancel && cancel->load()); };
        auto commitWith = [&](const Mutator& commit, bool locked) {
            if (!commit)
                return;
            if (!options.persist)
                PublishTransient(commit);
            else if (locked)
                PublishLocked(commit);
            else
                Publish(commit);
        };
        if (retired.load()) {
            error = "this account was signed out";
            return false;
        }
        if (fresh(*Current()))
            return true;
        if (cancelled()) {
            error = "cancelled";
            return false;
        }

        if (options.scope == Scope::Account) {
            Guard g(*this, LockFor(""), cancel);
            if (!g.Owned()) {
                error = g.Failure();
                return false;
            }
            Snapshot snap = Refresh();
            if (fresh(*snap))
                return true;
            Mutator commit;
            if (!produce(*snap, commit, error))
                return false;
            if (retired.load()) {
                error = "this account was signed out";
                return false;
            }
            commitWith(commit, true);
            return true;
        }

        for (int attempt = 0; attempt < 3; attempt++) {
            std::shared_ptr<Flight> flight;
            bool leader = false;
            {
                std::unique_lock<std::mutex> lock(mu);
                auto it = flights.find(key);
                if (it == flights.end()) {
                    flight = std::make_shared<Flight>();
                    flights[key] = flight;
                    leader = true;
                } else {
                    flight = it->second;
                    while (!flight->done) {
                        if (cancelled()) {
                            error = "cancelled";
                            return false;
                        }
                        cv.wait_for(lock, std::chrono::milliseconds(20));
                    }
                }
            }
            if (!leader) {
                if (!flight->ok) {
                    error = flight->error;
                    return false;
                }
                if (fresh(*Current()))
                    return true;
                continue;
            }
            bool ok = false;
            std::string failure;
            {
                std::unique_ptr<Guard> keyLock;
                bool coordinated = true;
                if (options.scope == Scope::Key) {
                    keyLock = std::make_unique<Guard>(*this, LockFor(".x" + policy::Fingerprint(key).substr(0, 16)), cancel);
                    if (!keyLock->Owned()) {
                        failure = keyLock->Failure();
                        coordinated = false;
                    }
                }
                if (coordinated) {
                    Snapshot snap = Refresh();
                    if (fresh(*snap)) {
                        ok = true;
                    } else {
                        Mutator commit;
                        ok = produce(*snap, commit, failure);
                        if (ok && retired.load()) {
                            ok = false;
                            failure = "this account was signed out";
                        }
                        if (ok)
                            commitWith(commit, false);
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(mu);
                flight->done = true;
                flight->ok = ok;
                flight->error = failure;
                flights.erase(key);
            }
            cv.notify_all();
            if (!ok)
                error = failure;
            return ok;
        }
        error = "the token changed repeatedly while refreshing";
        return false;
    }
}
