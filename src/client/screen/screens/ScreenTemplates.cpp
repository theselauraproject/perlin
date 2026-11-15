#include "client/screen/screens/ScreenTemplates.hpp"

#include "client/screen/screens/CreativeScreen.hpp"
#include "client/screen/screens/DeathScreen.hpp"
#include "client/screen/screens/DisconnectScreen.hpp"
#include "client/screen/screens/InventoryScreen.hpp"
#include "client/screen/screens/LoadingScreen.hpp"
#include "client/screen/screens/PauseScreen.hpp"
#include "client/ui/UiTemplate.hpp"

#include <cstdint>

namespace perlin::screen {
    void PreloadScreenTemplates(const std::string& dataDir) {
        using namespace perlin::ui;
        uint64_t version = 0;
        LoadUiTemplate(dataDir, "ui/screens/inventory_screen.rml", DefaultInventoryScreenTemplate(), version);
        LoadUiTemplate(dataDir, "ui/screens/pause_screen.rml", DefaultPauseScreenTemplate(), version);
        LoadUiTemplate(dataDir, "ui/screens/loading_screen.rml", DefaultLoadingScreenTemplate(), version);
        LoadUiTemplate(dataDir, "ui/screens/disconnect_screen.rml", DefaultDisconnectScreenTemplate(), version);
        LoadUiTemplate(dataDir, "ui/screens/creative_screen.rml", DefaultCreativeScreenTemplate(), version);
        LoadDeathScreenTemplate(dataDir, version);
    }
}
