#include "client/selftest/SelfTests.hpp"

#include <cstdio>

namespace perlin::selftest {
    int Run(std::string_view name) {
        if (name == "handler_dispatch")
            return HandlerDispatch();
        if (name == "title_controller")
            return TitleController();
        if (name == "pack_manager")
            return PackManagerTest();
        if (name == "server_browser")
            return ServerBrowser();
        if (name == "audio_director")
            return AudioDirectorTest();
        if (name == "light_band_cache")
            return LightBandCache();
        if (name == "debug_overlay")
            return DebugOverlay();
        if (name == "player_interaction")
            return PlayerInteraction();
        if (name == "hud_screen" || name == "hud_screen_preview")
            return HudScreen();
        if (name == "studio")
            return Studio();
        if (name == "worldview" || name == "worldview_preview")
            return Worldview();
        if (name == "title_ports")
            return TitlePorts();
        if (name == "connection_controllers" || name == "connection_controllers_preview")
            return ConnectionControllers(name == "connection_controllers_preview");
        std::fprintf(stderr, "unknown selftest: %.*s\n", static_cast<int>(name.size()), name.data());
        return 2;
    }
}
