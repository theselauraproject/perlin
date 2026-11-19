#include "client/services/account/AccountService.hpp"

#include <filesystem>

namespace perlin::services {
    AccountService::AccountService(Config cfg, Hooks h) : config(std::move(cfg)), hooks(std::move(h)) {
        store.Load(config.dataDir, config.onlineLogin, config.playerName, config.forgetLogin);
    }

    AccountService::~AccountService() { Stop(); }

    void AccountService::Log(const std::string& message) const {
        if (hooks.log)
            hooks.log(message);
    }

    void AccountService::Stop() {
        std::shared_ptr<Login> cancelling, cancellingConnect;
        {
            std::lock_guard lock(mutex);
            stopped = true;
            cancelling = login;
            cancellingConnect = connecting;
        }
        if (cancelling)
            cancelling->Cancel();
        if (cancellingConnect)
            cancellingConnect->Cancel();
        std::unique_lock lock(mutex);
        if (!idle.wait_for(lock, std::chrono::seconds(10), [&] { return inFlight == 0; }))
            Log("account tasks did not finish before shutdown");
    }

    void AccountService::Spawn(std::function<void()> work) {
        {
            std::lock_guard lock(mutex);
            if (stopped || !config.submit)
                return;
            inFlight++;
        }
        const bool accepted = config.submit([this, work = std::move(work)] {
            struct Done {
                AccountService& self;
                ~Done() {
                    std::lock_guard lock(self.mutex);
                    self.inFlight--;
                    self.idle.notify_all();
                }
            } done{*this};
            work();
        });
        if (!accepted) {
            std::lock_guard lock(mutex);
            inFlight--;
            idle.notify_all();
        }
    }

    bool AccountService::ActiveIsMicrosoft() const {
        const AccountEntry* a = store.Active();
        return a && a->microsoft;
    }

    std::string AccountService::ActiveAuthPath() const {
        const AccountEntry* a = store.Active();
        return a && a->microsoft ? store.AuthPath(*a) : config.dataDir + "/auth.json";
    }

    AccountService::ConnectLogin AccountService::OpenConnectLogin() {
        ConnectLogin connect;
        connect.authPath = ActiveAuthPath();
        connect.login = OpenLogin(connect.authPath);
        if (const AccountEntry* a = store.Active(); a && a->microsoft && a->name.empty())
            connect.nameAccountId = a->id;
        {
            std::lock_guard lock(mutex);
            connecting = connect.login;
        }
        return connect;
    }

    AccountService::ConnectOutcome AccountService::SignInForConnect(const ConnectLogin& connect, const Prompt& prompt) {
        ConnectOutcome outcome;
        if (!connect.login)
            return outcome;
        outcome.signedIn = connect.login->SignIn(prompt, outcome.error);
        if (hooks.signInEnded)
            hooks.signInEnded();
        if (outcome.signedIn && !connect.nameAccountId.empty()) {
            std::string tag, why;
            if (connect.login->FetchGamertag(tag, why))
                NoteGamertag(connect.nameAccountId, tag);
        }
        return outcome;
    }

    void AccountService::CancelConnect() {
        std::shared_ptr<Login> cancelling;
        {
            std::lock_guard lock(mutex);
            cancelling = connecting;
        }
        if (cancelling)
            cancelling->Cancel();
    }

    void AccountService::BeginSignIn() {
        CancelSignIn();
        std::shared_ptr<Login> flow;
        {
            std::lock_guard lock(mutex);
            authFile = store.NewAuthFile();
            flow = config.openLogin(config.dataDir + "/" + authFile);
            login = flow;
            url.clear();
            code.clear();
            status.clear();
            gamertag.clear();
            if (config.signInUnavailable) {
                std::string reason = config.signInUnavailable();
                if (!reason.empty()) {
                    status = std::move(reason);
                    state = SignInState::Failed;
                    return;
                }
            }
            state = SignInState::Waiting;
        }
        Spawn([this, flow] {
            std::string error, tag;
            const bool ok = flow->SignIn(
                [this, flow](const std::string& u, const std::string& c) {
                    {
                        std::lock_guard lock(mutex);
                        if (login != flow)
                            return;
                        url = u;
                        code = c;
                        status = "Waiting for you to sign in...";
                    }
                    if (hooks.showSignInCode)
                        hooks.showSignInCode(u, c);
                },
                error);
            if (hooks.signInEnded)
                hooks.signInEnded();
            const bool named = ok && flow->FetchGamertag(tag, error);
            std::lock_guard lock(mutex);
            if (login != flow)
                return;
            if (ok) {
                gamertag = named ? tag : std::string();
                state = SignInState::Done;
            } else {
                status = "Sign-in failed: " + error;
                state = SignInState::Failed;
            }
        });
    }

    void AccountService::CancelSignIn() {
        std::lock_guard lock(mutex);
        if (!login)
            return;
        login->Cancel();
        login.reset();
        if (hooks.signInEnded)
            hooks.signInEnded();
        if (!authFile.empty()) {
            if (config.forgetLogin) {
                config.forgetLogin(config.dataDir + "/" + authFile);
            } else {
                std::error_code ec;
                std::filesystem::remove(config.dataDir + "/" + authFile, ec);
            }
        }
        state = SignInState::Idle;
    }

    AccountService::SignInView AccountService::SignInProgress() const {
        std::lock_guard lock(mutex);
        return {state, url, code, status};
    }

    bool AccountService::Poll() {
        std::vector<std::pair<std::string, std::string>> renames;
        std::string finishedAuth, finishedTag;
        bool finished = false;
        {
            std::lock_guard lock(mutex);
            renames.swap(pendingRenames);
            if (state == SignInState::Done) {
                finished = true;
                finishedAuth = authFile;
                finishedTag = gamertag;
                login.reset();
                state = SignInState::Idle;
            }
        }
        for (const auto& [id, tag] : renames)
            store.Rename(id, tag);
        if (finished)
            store.AddMicrosoft(finishedTag, finishedAuth);
        return finished;
    }

    void AccountService::NoteGamertag(const std::string& id, const std::string& tag) {
        std::lock_guard lock(mutex);
        pendingRenames.emplace_back(id, tag);
    }

    void AccountService::BackfillGamertags(Clock::time_point now) {
        {
            std::lock_guard lock(mutex);
            if (backfilled && now - lastBackfill <= config.backfillInterval)
                return;
            backfilled = true;
            lastBackfill = now;
        }
        for (const AccountEntry& e : store.Entries()) {
            if (!e.microsoft || !e.name.empty())
                continue;
            Spawn([this, id = e.id, path = store.AuthPath(e)] {
                std::shared_ptr<Login> probe = config.openLogin(path);
                std::string error, tag;
                if (!probe->HasSavedLogin() || !probe->SignInSaved(error) || !probe->FetchGamertag(tag, error)) {
                    if (!error.empty())
                        Log("couldn't learn an account's gamertag: " + error);
                    return;
                }
                NoteGamertag(id, tag);
            });
        }
    }
}
