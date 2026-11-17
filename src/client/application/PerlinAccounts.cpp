#include "client/application/LaunchOverrides.hpp"
#include "util/Log.hpp"
#include "client/application/Perlin.hpp"
#include "client/application/PerlinInternal.hpp"
#include "client/services/account/XboxLogin.hpp"

namespace perlin {
    void Perlin::LoadAccounts() {
        if (ApplyLaunchOverrides(options, args).signOut) {
            xbox::Account::SignOut(paths::DataDir() + "/auth.json");
            Log("auth", "saved Microsoft login removed");
        }
        services::AccountService::Config config;
        config.dataDir = paths::DataDir();
        config.onlineLogin = options.onlineLogin;
        config.playerName = options.playerName;
        config.openLogin = [](const std::string& path) -> std::shared_ptr<services::AccountService::Login> {
            return std::make_shared<services::XboxLogin>(path);
        };
        config.forgetLogin = [](const std::string& path) { xbox::Account::SignOut(path); };
        config.submit = [this](std::function<void()> job) {
            if (!workers)
                return false;
            workers->Submit(std::move(job));
            return true;
        };
#ifdef __EMSCRIPTEN__
        config.signInUnavailable = [] {
            return http::WebRelayReady()
                       ? std::string()
                       : std::string("Microsoft sign-in in the browser needs the site's sign-in relay (see HOSTING.txt)");
        };
#endif
        services::AccountService::Hooks hooks;
        hooks.showSignInCode = [this](const std::string& url, const std::string& code) {
            platform->ShowSignInCode(url, code);
        };
        hooks.log = [](const std::string& message) { Log("auth", "{}", message); };
        appServices.accounts = std::make_unique<services::AccountService>(std::move(config), std::move(hooks));
        if (args.playerName.empty() && !args.login && !args.logout)
            ApplyActiveAccount();
        onlineThisRun = options.onlineLogin && !args.offline;
    }

    void Perlin::ApplyActiveAccount() {
        const services::AccountEntry* a = Accounts().Active();
        if (!a)
            return;
        options.onlineLogin = a->microsoft;
        if (!a->microsoft)
            options.playerName = a->name;
        onlineThisRun = options.onlineLogin && !args.offline;
        if (a->skinKnown && (a->skin != options.skin || a->cape != options.cape)) {
            options.skin = a->skin;
            options.cape = a->cape;
            SkinConfig cfg = MakeSkinConfig(options.skin, options.cape, options.skinSlim);
            LoadedSkin loaded;
            std::string err;
            if (LoadSkinConfig(cfg, loaded, err))
                options.slimArms = loaded.slimArms;
            else
                Log("accounts", "skin: {}", err);
            pause.wardrobe.init = false;
        } else if (!a->skinKnown) {
            Accounts().SetSkin(a->id, options.skin, options.cape);
        }
        options.Save();
        PrewarmSignIn();
    }

    void Perlin::PrewarmSignIn() {
        if (!onlineThisRun || PERLIN_ENV("PERLIN_PREVIEW"))
            return;
        const std::string path = Accounts().ActiveAuthPath();
        const std::shared_ptr<services::AccountService::Login> login = Accounts().OpenLogin(path);
        if (login->HasSavedLogin())
            BedrockSession::PrewarmLogin(login->Native(), path);
    }
}
