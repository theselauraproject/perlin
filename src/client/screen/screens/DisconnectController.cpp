#include "client/screen/screens/DisconnectController.hpp"

#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/ui/GuiViewport.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Version.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace perlin::screen {
    DisconnectController::DisconnectController(Deps d)
        : disconnect(d.disconnect), screens(d.screens), input(d.input), options(d.options), chat(d.chat),
          panorama(d.panorama), ports(d.ports) {}

    bool DisconnectController::Update(ui::Hud& hud, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT") != nullptr;
        BedrockSession* session = ports.Session();
        const bool failed = preview || (session && session->GetState() == BedrockSession::State::Failed &&
                                        !session->Transferring());
        const auto now = Clock::now();
        if (!failed) {
            if (disconnect.IsOpen()) {
                hud.Overlay().ClearOverlay();
                screens.Close(disconnect);
                disconnect.signature.clear();
            }
            if (disconnect.textMode) {
                disconnect.textMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            disconnect.leftDown = false;
            return false;
        }
        if (!disconnect.IsOpen()) {
            screens.Open(disconnect);
            disconnect.openedAt = now;
            ports.CloseGameplayScreens();
            Log("ui", "disconnect screen shown");
        }
        if (!disconnect.textMode) {
            disconnect.textMode = true;
            input.SetTextMode(true);
        }

        ui::DisconnectScreenText t;
        t.rejoin = i18n::Tr("perlin.disconnect.rejoin");
        t.quit = i18n::Tr("menu.disconnect");
        BedrockSession::DisconnectInfo info;
        std::string lastError;
        if (preview) {
            info.happened = true;
            info.reason = 55;
            const char kind = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0];
            if (kind == 'c' || kind == 's') {
                info.outdated = kind == 'c' ? 1 : 2;
                info.serverVersion = kind == 'c' ? "1.26.70" : "1.21.50";
            }
            info.message =
                PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0] == 'b'
                    ? "You are banned from this server.\nReason: Cheating (fly hacks)\nExpires in 29d 23h 59m"
                    : "Kicked by an operator";
            if (info.outdated)
                info.message.clear();
        } else {
            info = session->GetDisconnectInfo();
            lastError = session->LastError();
        }

        const bool olderServer = !preview && info.outdated == 2 && info.serverProtocol > 0 &&
                                 info.serverProtocol < kBedrockProtocol && !ports.InSingleplayer();
        const bool overrideTried =
            ports.Override().protocol == info.serverProtocol && ports.Override().server == options.server;
        const auto joinAsServerVersion = [&] {
            ports.Override().server = options.server;
            ports.Override().protocol = info.serverProtocol;
            ports.Override().version = info.serverVersion;
            if (ports.Override().dontAsk && !options.versionMismatchNoAsk) {
                options.versionMismatchNoAsk = true;
                options.Save();
            }
            Log("perlin", "joining {} again as protocol {} ({})", options.server, info.serverProtocol,
                info.serverVersion);
            hud.Overlay().ClearOverlay();
            screens.Close(disconnect);
            disconnect.signature.clear();
            ports.SetSessionReady(false);
            ports.Connect();
        };
        if (olderServer && !overrideTried && options.versionMismatchNoAsk) {
            joinAsServerVersion();
            return true;
        }
        std::string message = str::StripFormatting(info.message);
        if (!message.empty() && !message.contains(' ') && message.contains('.')) {
            const std::string translated = i18n::Tr(message);
            if (translated != message)
                message = translated;
        }
        if (info.outdated != 0) {
            const bool clientOld = info.outdated == 1;
            t.title = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client"
                                         : "perlin.disconnect.outdated_server");
            t.reason = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client_reason"
                                          : "perlin.disconnect.outdated_server_reason");
            const std::string theirs = !info.serverVersion.empty() ? info.serverVersion
                                       : info.serverProtocol > 0
                                           ? "protocol " + std::to_string(info.serverProtocol)
                                           : "?";
            t.reason +=
                "\n\n" + i18n::Tr("perlin.disconnect.versions", {theirs, std::string(kBedrockVersion)});
            if (!message.empty())
                t.reason += "\n" + message;
            if (olderServer && !overrideTried) {
                t.title =
                    i18n::TrOr("perlin.disconnect.older_server", "This server advertises an older version");
                const std::vector<std::string> args = {theirs, std::to_string(info.serverProtocol),
                                                       std::string(kBedrockVersion),
                                                       std::to_string(kBedrockProtocol)};
                t.reason = i18n::Tr("perlin.disconnect.older_server_reason", args);
                if (t.reason == "perlin.disconnect.older_server_reason")
                    t.reason =
                        "The server says it runs Minecraft " + args[0] + " (protocol " + args[1] +
                        "); Perlin speaks " + args[2] + " (" + args[3] +
                        "). Many servers, The Hive among them, still let newer clients in.\n\nPerlin can "
                        "join announcing " +
                        args[0] +
                        ". Things that changed between the two versions may not work. Are you sure?";
                t.rejoin = i18n::TrOr("perlin.disconnect.join_anyway", "Join Anyway");
                t.extra = std::string(ports.Override().dontAsk ? "[x] " : "[  ] ") +
                          i18n::TrOr("perlin.disconnect.dont_ask", "Don't ask again");
            } else if (olderServer) {
                t.reason += "\n\n" + i18n::TrOr("perlin.disconnect.older_server_failed",
                                                  "Joining as the server's version did not work either.");
            }
        } else if (info.happened && info.connectionLost) {
            t.title = i18n::Tr("perlin.disconnect.lost");
            t.reason = i18n::Tr("perlin.disconnect.lost_reason");
            if (!message.empty())
                t.reason += "\n" + message;
        } else if (info.happened) {
            const bool ban = ui::LooksLikeBan(message);
            t.title = i18n::Tr(ban ? "perlin.disconnect.banned" : "perlin.disconnect.title");
            t.reason = message.empty() ? i18n::Tr("perlin.disconnect.no_reason") : message;
            switch (info.reason) {
            case 44:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.id_conflict");
                break;
            case 8:
            case 35:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.version");
                break;
            case 46:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.login");
                break;
            default:
                break;
            }
            if (info.reason >= 0)
                t.code = i18n::Tr("perlin.disconnect.code", {std::to_string(info.reason)});
        } else {
            t.title = i18n::Tr("perlin.disconnect.failed");
            t.reason = lastError.empty() ? i18n::Tr("perlin.disconnect.no_reason") : lastError;
        }

        if (disconnect.tmpl.Refresh("ui/screens/disconnect_screen.rml", ui::DefaultDisconnectScreenTemplate(),
                                    std::chrono::milliseconds(400), now, "disconnect screen"))
            disconnect.signature.clear();
        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        t.panorama = panorama.Draw(fbW, fbH, true);
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title +
                                      "|" + t.reason + "|" + t.code + (t.panorama ? "|p" : "|d");
        if (signature != disconnect.signature || !hud.Overlay().HasOverlay()) {
            disconnect.signature = signature;
            std::string error;
            ui::SceneDoc doc = ui::BuildDisconnectScreen(disconnect.tmpl.text, t, guiW, guiH);
            if (!hud.Overlay().SetOverlay(doc.rml, doc.scene, error)) {
                Log("ui", "disconnect_screen.rml: {}", error);
                disconnect.tmpl.text = ui::DefaultDisconnectScreenTemplate();
                doc = ui::BuildDisconnectScreen(disconnect.tmpl.text, t, guiW, guiH);
                hud.Overlay().SetOverlay(doc.rml, doc.scene, error);
            }
        }
        const glm::vec2 pointer = screen::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && disconnect.leftDown && now - disconnect.openedAt > std::chrono::milliseconds(300) &&
            !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDisconnectExtraId) {
                ports.Override().dontAsk = !ports.Override().dontAsk;
                disconnect.signature.clear();
            } else if (hover == ui::kDisconnectRejoinId && olderServer && !overrideTried) {
                joinAsServerVersion();
                return true;
            } else if (hover == ui::kDisconnectRejoinId) {
                Log("ui", "disconnect screen: rejoin");
                hud.Overlay().ClearOverlay();
                screens.Close(disconnect);
                disconnect.signature.clear();
                if (ports.SessionReady() && session && session->RequestReconnect()) {
                } else {
                    ports.SetSessionReady(false);
                    ports.Connect();
                }
            } else if (hover == ui::kDisconnectQuitId) {
                ports.LeaveToServerList(hud);
            }
        }
        disconnect.leftDown = down;
        return true;
    }
}
