#include "client/screen/screens/DisconnectController.hpp"
#include "client/screen/screens/LoadingController.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/TexturePack.hpp"

#include <algorithm>
#include <memory>

namespace {
    using namespace perlin;

    class FakeConnection final : public screen::ConnectionPorts {
    public:
        std::vector<std::string> calls;
        screen::ConnectionPorts::VersionOverride override;
        std::string hint;
        bool modHides = false;
        bool ready = false;

        int Count(const std::string& s) const { return static_cast<int>(std::count(calls.begin(), calls.end(), s)); }

        BedrockSession* Session() override { return nullptr; }
        LevelRenderer* Level() override { return nullptr; }
        bool SessionReady() override { return ready; }
        void SetSessionReady(bool value) override {
            ready = value;
            calls.push_back("SetSessionReady");
        }
        std::string StatusHint() override { return hint; }
        bool OnlineThisRun() override { return false; }
        bool InSingleplayer() override { return false; }
        bool ReplayPlaying() override { return false; }
        bool ModHidesLoading() override { return modHides; }
        bool TakeLeaseJoinFailed() override { return false; }
        void PollLeaseJoin() override { calls.push_back("PollLeaseJoin"); }
        VersionOverride& Override() override { return override; }
        void Connect() override { calls.push_back("Connect"); }
        void LeaveToServerList(ui::Hud&) override { calls.push_back("LeaveToServerList"); }
        void StartAssetDownloads() override { calls.push_back("StartAssetDownloads"); }
        std::array<assets::Job*, 2> AssetJobs() override { return {nullptr, nullptr}; }
        void CloseGameplayScreens() override { calls.push_back("CloseGameplayScreens"); }
    };

    struct Rig {
        perlin::Options options;
        perlin::Input input;
        std::unique_ptr<RenderContext> noRenderer;
        ui::PanoramaBackdrop panorama{options, noRenderer, selftest::G().uiAssets, selftest::G().texturePack,
                                      selftest::G().serverPacks};
        screen::Manager screens;
        ui::ChatLog chat;
        screen::TitleScreen& titleScreen = screens.Emplace<screen::TitleScreen>(screen::Id::Title, screen::Traits{true});
        screen::LoadingScreen& loading = screens.Emplace<screen::LoadingScreen>(screen::Id::Loading, screen::Traits{true});
        screen::DisconnectScreen& disconnect =
            screens.Emplace<screen::DisconnectScreen>(screen::Id::Disconnect, screen::Traits{true});
        FakeConnection ports;
        screen::LoadingController loadingController{screen::LoadingController::Deps{
            loading, titleScreen, screens, input, options, chat, panorama, ports}};
        screen::DisconnectController disconnectController{screen::DisconnectController::Deps{
            disconnect, screens, input, options, chat, panorama, ports}};
        ui::Hud hud;
    };
}

namespace perlin::selftest {
    int ConnectionControllers(bool preview) {
        Checker t(preview ? "connection_controllers_preview_test" : "connection_controllers_test");
        TempDataDir data;

        if (!preview) {
            {
                Rig r;
                r.titleScreen.active = true;
                t.Check(!r.loadingController.Update(r.hud, 800, 600), "loading yields to the title screen");
                t.Check(!r.screens.IsOpen(screen::Id::Loading), "loading stays closed behind the title screen");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 0, "no gameplay screens closed while title is up");
            }
            {
                Rig r;
                r.titleScreen.active = false;
                t.Check(r.loadingController.Update(r.hud, 800, 600), "loading takes the frame without a session");
                t.Check(r.screens.IsOpen(screen::Id::Loading), "loading screen opens");
                t.Check(r.loading.textMode && r.input.TextMode(), "loading enters text mode");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 1, "gameplay screens closed once on open");
                t.Check(!r.loading.signature.empty(), "loading builds a signature");
                t.Check(r.loadingController.Update(r.hud, 800, 600), "second frame still loading");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 1, "reopen does not repeat the close");
            }
            {
                Rig r;
                r.titleScreen.active = false;
                r.ports.hint = "hint-for-test";
                r.loadingController.Update(r.hud, 800, 600);
                t.Check(r.loading.signature.contains("hint-for-test"), "status hint reaches the loading text");
                r.ports.hint.clear();
                const std::string before = r.loading.signature;
                r.loadingController.Update(r.hud, 800, 600);
                t.Check(r.loading.signature != before, "changed text changes the signature");
                t.Check(!r.loading.signature.contains("hint-for-test"), "cleared hint disappears");
            }
            {
                Rig r;
                r.titleScreen.active = false;
                r.ports.modHides = true;
                t.Check(r.loadingController.Update(r.hud, 800, 600), "mod-hidden loading still owns the frame");
                t.Check(r.loading.modHidden, "mod hiding is recorded");
                t.Check(r.loading.signature.empty(), "mod-hidden loading keeps no signature");
                r.ports.modHides = false;
                r.loadingController.Update(r.hud, 800, 600);
                t.Check(!r.loading.modHidden && !r.loading.signature.empty(), "loading returns when the mod stops hiding it");
            }
            {
                Rig r;
                t.Check(!r.disconnectController.Update(r.hud, 800, 600), "no failure means no disconnect screen");
                t.Check(!r.screens.IsOpen(screen::Id::Disconnect), "disconnect stays closed");
                t.Check(!r.disconnect.textMode, "disconnect leaves text mode alone");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 0, "no gameplay screens closed without a failure");
            }
        } else {
            {
                Rig r;
                r.titleScreen.active = false;
                t.Check(r.loadingController.Update(r.hud, 800, 600), "preview loading takes the frame");
                t.Check(r.screens.IsOpen(screen::Id::Loading), "preview loading opens");
                t.Check(r.loading.signature.contains("40%"), "preview progress is in the loading text");
                t.Check(r.loading.signature.contains("Downloading packs 3/25"), "preview pack count is in the loading text");
            }
            {
                Rig r;
                t.Check(r.disconnectController.Update(r.hud, 800, 600), "preview disconnect takes the frame");
                t.Check(r.screens.IsOpen(screen::Id::Disconnect), "preview disconnect opens");
                t.Check(r.disconnect.textMode && r.input.TextMode(), "disconnect enters text mode");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 1, "gameplay screens closed once on disconnect");
                t.Check(r.disconnect.signature.contains("banned") || r.disconnect.signature.contains("Banned") ||
                            r.disconnect.signature.contains("You are banned"),
                        "ban message selects the banned presentation");
                t.Check(r.disconnect.signature.contains("55") || r.disconnect.signature.contains("disconnect.code"),
                        "disconnect reason code is shown");
                t.Equal(r.ports.Count("Connect"), 0, "preview never reconnects");
                t.Check(r.disconnectController.Update(r.hud, 800, 600), "second preview frame");
                t.Equal(r.ports.Count("CloseGameplayScreens"), 1, "second frame does not close screens again");
            }
        }
        return t.Finish();
    }
}
