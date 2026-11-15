#include "client/ui/GuiViewport.hpp"
#include "client/application/PerlinInternal.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::RegisterScreens() {
        using screen::Claim;
        using screen::Frame;
        const auto claim = [](bool took) { return took ? Claim::Frame : Claim::None; };
        const auto inGame = [](auto update) {
            return [update](Frame& f) {
                update(f);
                return Claim::None;
            };
        };

        titleScreen.update = [this, claim](Frame& f) { return claim(titleController.Update(f.hud, f.fbW, f.fbH)); };
        disconnect.update = [this, claim](Frame& f) { return claim(disconnectController.Update(f.hud, f.fbW, f.fbH)); };
        loading.update = [this, claim](Frame& f) { return claim(loadingController.Update(f.hud, f.fbW, f.fbH)); };
        replayScreen.update = [this](Frame& f) {
            if (!replayCtl.Playing() || !f.live || !f.live->IsReplay())
                return Claim::None;
            if (replayStudio.Exporting())
                replayStudio.ExportOverlay(f.hud, f.fbW, f.fbH);
            else
                replayStudio.UpdateScreen(f.hud, f.live, f.fbW, f.fbH);
            return Claim::Frame;
        };
        replayScreen.isOpen = [this] { return replayCtl.Playing(); };
        chatScreen.update = inGame([this](Frame& f) { UpdateChatClicks(f.hud, f.live, f.fbW, f.fbH); });
        chatScreen.isOpen = [this] { return chat.IsOpen(); };
    }

    void Perlin::UpdateServerLayer(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto hide = [&] {
            if (!serverLayer.shown)
                return;
            hud.Overlay().ClearServerLayer();
            serverLayer.shown = false;
            serverLayer.signature.clear();
        };
        if (!live || !options.showHud || !hudState.visible || death.IsOpen() || WorldCovered())
            return hide();

        const auto now = Clock::now();
        if (!toast.text.empty() && now - toast.at > std::chrono::seconds(4))
            toast.text.clear();
        ui::ServerLayerData data;
        if (toast.text.empty()) {
            BedrockSession::ServerToast t;
            if (live->TakeToast(t) && !modHost.DispatchModToast(t.title, t.message)) {
                toast.text = t.title + "\n" + t.message;
                toast.at = now;
                toast.title = t.title;
                toast.message = t.message;
            }
        }
        if (!toast.text.empty()) {
            data.toast = true;
            data.toastTitle = toast.title;
            data.toastMessage = toast.message;
        }
        if (!(hud.Ready() && hud.Overrides("hud")))
            for (const auto& b : live->BossBars())
                data.bars.push_back({b.title, b.progress, b.color, b.overlay});

        if (data.bars.size() > 8)
            data.bars.resize(8);
        const auto sb = live->Scoreboard();
        if (sb.visible && !pause.IsOpen() && !(hud.Ready() && hud.Overrides("scoreboard"))) {
            data.scoreboard = true;
            data.scoreboardTitle = sb.title;
            for (const auto& r : sb.rows)
                data.rows.emplace_back(r.name, r.score);
        }
        if (data.Empty())
            return hide();

        const int w = static_cast<int>(hud.GuiWidth()), h = static_cast<int>(hud.GuiHeight());
        std::string signature = ui::ServerLayerSignature(data, w, h);
        if (serverLayer.shown && signature == serverLayer.signature)
            return;
        serverLayer.signature = std::move(signature);
        std::string error;
        serverLayer.shown = hud.Overlay().SetServerLayer(ui::BuildServerLayer(data, w, h), error);
        if (!serverLayer.shown)
            Log("ui", "{}", error);
    }

    void Perlin::UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        if (!chat.IsOpen() || (!live && !PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))) {
            chatLeftDown = false;
            return;
        }
        const ui::GuiViewport gui = ui::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = screen::PointerOrPreview(input);
        hud.Overlay().MouseMove(gui.ToGui(pointer.x), gui.ToGui(pointer.y));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        if (down && !chatLeftDown) {
            const std::string link = hud.Overlay().HoverLink();
            if (link.starts_with("link:url:")) {
                const std::string url = link.substr(9);
                if (url.starts_with("https://") && platform)
                    platform->OpenUrl(url);
                else if (url.starts_with("http://") && platform)
                    platform->OpenUrl("https://" + url.substr(7));
            } else if (link.size() > 6 && link[5] == '/' && live) {
                live->SendCommand(link.substr(6));
                chat.Close();
                input.SetTextMode(false);
            }
        }
        chatLeftDown = down;
    }
}
