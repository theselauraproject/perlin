#pragma once
#include "client/services/account/AccountStore.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace perlin::xbox {
    class Account;
}

namespace perlin::services {
    class AccountService {
    public:
        using Clock = std::chrono::steady_clock;
        using Prompt = std::function<void(const std::string& url, const std::string& code)>;

        class Login {
        public:
            virtual ~Login() = default;
            virtual bool HasSavedLogin() const = 0;
            virtual void Cancel() = 0;
            virtual bool SignIn(const Prompt& prompt, std::string& error) = 0;
            virtual bool SignInSaved(std::string& error) = 0;
            virtual bool FetchGamertag(std::string& gamertag, std::string& error) = 0;
            virtual std::shared_ptr<xbox::Account> Native() const = 0;
        };

        struct Config {
            std::string dataDir;
            bool onlineLogin = false;
            std::string playerName;
            std::function<std::shared_ptr<Login>(const std::string& authPath)> openLogin;
            std::function<void(const std::string& authPath)> forgetLogin;
            std::function<bool(std::function<void()>)> submit;
            std::function<std::string()> signInUnavailable;
            std::chrono::seconds backfillInterval{30};
        };

        struct Hooks {
            std::function<void(const std::string& url, const std::string& code)> showSignInCode;
            std::function<void(const std::string& message)> log;
        };

        enum class SignInState { Idle, Waiting, Done, Failed };

        struct SignInView {
            SignInState state = SignInState::Idle;
            std::string url, code, status;
        };

        AccountService(Config config, Hooks hooks);
        ~AccountService();
        AccountService(const AccountService&) = delete;
        AccountService& operator=(const AccountService&) = delete;

        void Stop();

        const std::vector<AccountEntry>& Entries() const { return store.Entries(); }
        const AccountEntry* Active() const { return store.Active(); }
        const std::string& ActiveId() const { return store.ActiveId(); }
        bool ActiveIsMicrosoft() const;
        std::string ActiveAuthPath() const;
        std::string AuthPath(const AccountEntry& entry) const { return store.AuthPath(entry); }

        bool SetActive(const std::string& id) { return store.SetActive(id); }
        void AddOffline(const std::string& name) { store.AddOffline(name); }
        void AddMicrosoft(const std::string& gamertag, const std::string& authFile) {
            store.AddMicrosoft(gamertag, authFile);
        }
        void Remove(const std::string& id) { store.Remove(id); }
        void SetSkin(const std::string& id, const std::string& skin, const std::string& cape) {
            store.SetSkin(id, skin, cape);
        }

        struct ConnectLogin {
            std::shared_ptr<Login> login;
            std::string authPath;
            std::string nameAccountId;
        };

        struct ConnectOutcome {
            bool signedIn = false;
            std::string error;
        };

        std::shared_ptr<Login> OpenLogin(const std::string& authPath) const { return config.openLogin(authPath); }
        ConnectLogin OpenConnectLogin();
        ConnectOutcome SignInForConnect(const ConnectLogin& connect, const Prompt& prompt);
        void CancelConnect();

        void BeginSignIn();
        void CancelSignIn();
        SignInView SignInProgress() const;
        bool Poll();

        void NoteGamertag(const std::string& id, const std::string& gamertag);
        void BackfillGamertags(Clock::time_point now);

    private:
        void Spawn(std::function<void()> work);
        void Log(const std::string& message) const;

        Config config;
        Hooks hooks;
        AccountStore store;

        mutable std::mutex mutex;
        std::condition_variable idle;
        bool stopped = false;
        int inFlight = 0;
        std::shared_ptr<Login> login;
        std::shared_ptr<Login> connecting;
        std::string authFile, url, code, status, gamertag;
        SignInState state = SignInState::Idle;
        std::vector<std::pair<std::string, std::string>> pendingRenames;
        Clock::time_point lastBackfill{};
        bool backfilled = false;
    };
}
