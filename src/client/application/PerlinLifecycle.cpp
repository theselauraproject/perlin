#include "client/ui/GuiViewport.hpp"
#include "util/Version.hpp"
#include "client/application/PerlinInternal.hpp"
#include "util/Log.hpp"
#include "client/services/update/Updater.hpp"

namespace perlin {

    void Perlin::LeaveToServerList(ui::Hud& hud) {
        Log("perlin", "left {}, back to the server list", options.server);
        replayStudio.FinishRecording("you left the server");
        connectGeneration++;
        Accounts().CancelConnect();
        SetStatusHint({});
        sessionReady = false;
        if (session) {
            if (connectsInFlight.load() == 0 && !session->Transferring()) {
                session->Disconnect();
                session->SetFriendLease(nullptr);
            }
            retiredSessions.push_back(std::move(session));
        }
        serverBrowser.ReleaseFriendLease();
        particleLevel = nullptr;
        hud.Overlay().ClearOverlay();
        screens.Close(disconnect);
        disconnect.textMode = disconnect.leftDown = false;
        disconnect.signature.clear();
        screens.Close(loading);
        loading.textMode = loading.leftDown = false;
        loading.signature.clear();
        loading.startAt = {};
        loading.readyArmed = false;
        screens.CloseAll({&pause, &inventory, &death, &form});
        death.textMode = false;
        pause.signature.clear();
        death.signature.clear();
        if (chat.IsOpen())
            chat.Close();
        packManager.RequestLevelUnload();
        titleScreen.active = true;
        titleScreen.returnToServers = !inSingleplayer;
        inSingleplayer = false;
    }

    bool Perlin::WorldCovered() const {

        return titleScreen.active || titleScreen.IsOpen() || loading.IsOpen() || disconnect.IsOpen() ||
               (session && !loading.done && !loading.modHidden);
    }
}
