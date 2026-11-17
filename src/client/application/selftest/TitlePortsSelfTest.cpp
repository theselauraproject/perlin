#include "client/application/TitlePortsImpl.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"

#include <algorithm>
#include <deque>
#include <memory>

namespace {
    using namespace perlin;

    class NoLogin final : public services::AccountService::Login {
    public:
        bool HasSavedLogin() const override { return false; }
        void Cancel() override {}
        bool SignIn(const services::AccountService::Prompt&, std::string& error) override {
            error = "unavailable";
            return false;
        }
        bool SignInSaved(std::string& error) override {
            error = "unavailable";
            return false;
        }
        bool FetchGamertag(std::string&, std::string& error) override {
            error = "unavailable";
            return false;
        }
        std::shared_ptr<xbox::Account> Native() const override { return nullptr; }
    };

    struct Rig {
        Options options;
        std::unique_ptr<ThreadPool> workers = std::make_unique<ThreadPool>(2);
        TaskGroup tasks;
        bool microsoft = false;
        int hidden = 0, connected = 0;
        servers::ServerBrowser browser{servers::ServerBrowser::Deps{
            options, selftest::G().uiAssets, workers, tasks, [this] { return microsoft; },
            [] { return std::string("/nonexistent/perlin_selftest_auth.json"); }, [](const std::string&) {},
            [this] { hidden++; }, [this] { connected++; }}};

        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<ui::Hud> hud;
        ui::HudState hudState;
        HandRenderer handRenderer;
        entity::EntityRenderer entityRenderer;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        ui::ChatLog chat;
        uint64_t ticks = 0;
        ShaderPackCache shaderPackCache;
        packs::PackManager manager{packs::PackManager::Deps{
            options, selftest::G().uiAssets, selftest::G().texturePack, selftest::G().serverPacks,
            selftest::G().serverBlocks, selftest::G().jsonScreens, selftest::G().mcFont, selftest::G().shaderPack,
            selftest::G().blockPalette, selftest::G().lightRegistry, renderer, levelRenderer, hud, entityRenderer,
            handRenderer, hudState, audioEngine, sounds, chat, ticks, shaderPackCache, [] {},
            [] { return std::vector<std::string>{}; }, [] {}, [] {}}};

        AppServices services;
        std::deque<std::function<void()>> queue;
        std::atomic<int> connectsInFlight{0};
        int applied = 0, singleplayer = 0, quit = 0;
        std::pair<int, int> assetSize{0, 0};
        bool assetResult = false;
        std::unique_ptr<TitlePortsImpl> ports;

        Rig() {
            services::AccountService::Config c;
            c.dataDir = std::filesystem::temp_directory_path().string() + "/perlin_titleports_" +
                        std::to_string(std::random_device{}());
            std::filesystem::create_directories(c.dataDir);
            dataDir = c.dataDir;
            c.openLogin = [](const std::string&) -> std::shared_ptr<services::AccountService::Login> {
                return std::make_shared<NoLogin>();
            };
            c.submit = [this](std::function<void()> job) {
                queue.push_back(std::move(job));
                return true;
            };
            services.accounts = std::make_unique<services::AccountService>(std::move(c), services::AccountService::Hooks{});
            ports = std::make_unique<TitlePortsImpl>(TitlePortsImpl::Deps{
                browser, services, manager, connectsInFlight, selftest::G().serverPacks, [this] { applied++; },
                [this] { singleplayer++; },
                [this](ui::Hud&, int w, int h) {
                    assetSize = {w, h};
                    return assetResult;
                },
                [this] { quit++; }});
        }
        ~Rig() {
            services.Stop();
            ports.reset();
            services.accounts.reset();
            std::error_code ec;
            std::filesystem::remove_all(dataDir, ec);
        }
        std::string dataDir;
    };
}

namespace perlin::selftest {
    int TitlePorts() {
        Checker t("title_ports_test");
        TempDataDir data;

        {
            Rig r;
            t.Check(r.ports->AccountsEmpty(), "fresh service has no accounts");
            t.Check(!r.ports->Accounts().hasActive, "empty snapshot has no active account");
            r.ports->AddOfflineAccount("Alex");
            r.ports->AddOfflineAccount("Bob");
            const auto snapshot = r.ports->Accounts();
            t.Equal(snapshot.entries.size(), static_cast<size_t>(2), "both offline accounts are listed");
            t.Check(!r.ports->AccountsEmpty(), "accounts no longer empty");
            t.Check(snapshot.hasActive && !snapshot.activeName.empty(), "one account is active");
            const auto bob = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                          [](const auto& e) { return e.name == "Bob"; });
            t.Check(bob != snapshot.entries.end() && !bob->microsoft, "offline entry is not marked microsoft");
            if (bob != snapshot.entries.end()) {
                t.Check(r.ports->SetActiveAccount(bob->id), "switching to a known account succeeds");
                const auto after = r.ports->Accounts();
                t.Equal(after.activeName, std::string("Bob"), "snapshot follows the switch");
                t.Check(std::count_if(after.entries.begin(), after.entries.end(), [](const auto& e) { return e.active; }) == 1,
                        "exactly one entry is flagged active");
                r.ports->RemoveAccount(bob->id);
                t.Equal(r.ports->Accounts().entries.size(), static_cast<size_t>(1), "removal drops the entry");
            }
            t.Check(!r.ports->SetActiveAccount("no-such-id"), "unknown account id is rejected");
        }
        {
            Rig r;
            r.ports->AddPreviewAccounts();
            const auto snapshot = r.ports->Accounts();
            t.Equal(snapshot.entries.size(), static_cast<size_t>(3), "preview adds three accounts");
            t.Check(std::any_of(snapshot.entries.begin(), snapshot.entries.end(), [](const auto& e) { return e.microsoft; }),
                    "preview includes a microsoft account");
        }
        {
            Rig r;
            r.ports->ApplyActiveAccount();
            r.ports->ApplyActiveAccount();
            t.Equal(r.applied, 2, "apply callback runs per call");
            r.ports->JoinSingleplayer();
            t.Equal(r.singleplayer, 1, "singleplayer callback forwarded");
            r.ports->Quit();
            t.Equal(r.quit, 1, "quit callback forwarded");
            r.assetResult = true;
            ui::Hud hud;
            t.Check(r.ports->UpdateAssetScreen(hud, 640, 480), "asset screen result is forwarded");
            t.Check(r.assetSize == std::make_pair(640, 480), "asset screen receives the framebuffer size");
            r.assetResult = false;
            t.Check(!r.ports->UpdateAssetScreen(hud, 1, 2), "false asset screen result is forwarded");
        }
        {
            Rig r;
            r.ports->JoinServer("play.example.net:19132");
            t.Equal(r.options.server, std::string("play.example.net:19132"), "join forwards the address");
            t.Equal(r.connected, 1, "join connects once");
            ui::TitleModel title;
            r.ports->SetJoinMessage("note");
            r.ports->PollServers(title);
            t.Equal(title.joinMessage, std::string("note"), "join message forwarded to the title model");
            r.ports->SetJoinMessage({});
            r.ports->PollServers(title);
            t.Check(title.joinMessage.empty(), "cleared join message forwarded");
            t.Check(!r.ports->TakeFeaturedPing(), "no featured ping pending");
            t.Check(r.ports->PingResult("unknown:1").address.empty() || r.ports->PingResult("unknown:1").address == "unknown:1",
                    "unknown ping result is empty");
        }
        {
            Rig r;
            t.Check(!r.ports->BuildLabel().empty(), "build label is not empty");
            t.Check(r.ports->ServerPackDirectories().empty(), "no server pack directories without server packs");
            const auto update = r.ports->UpdateStatus();
            t.Check(!update.action || !update.text.empty(), "an actionable update always carries text");
            r.connectsInFlight = 1;
            r.ports->ReleaseIdleServerPacks();
            r.connectsInFlight = 0;
            r.ports->ReleaseIdleServerPacks();
            t.Check(true, "releasing idle server packs is safe with and without connections");
        }
        return t.Finish();
    }
}
