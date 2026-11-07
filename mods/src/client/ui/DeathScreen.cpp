#include "client/ui/DeathScreen.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Resources.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace perlin::ui {
    namespace fs = std::filesystem;

    std::string DefaultDeathScreenTemplate() {
        std::string text = resources::ReadText("death_screen.rml");
        return text.empty() ? EmbeddedDeathScreenTemplate() : text;
    }

    std::string LoadDeathScreenTemplate(const std::string& dataDir, uint64_t& version) {
        return LoadUiTemplate(dataDir, "death_screen.rml", DefaultDeathScreenTemplate(), version);
    }

    std::string BuildDeathScreenRml(const std::string& templateText, const DeathScreenText& t, int guiW,
                                    int guiH) {
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 100));
        ReplaceAll(d, "btn1_y", std::to_string(guiH / 4 + 72));
        ReplaceAll(d, "btn2_y", std::to_string(guiH / 4 + 96));
        ReplaceAll(d, "off", t.buttonsEnabled ? "" : " off");
        ReplaceAll(d, "title_screen", EscapeRml(t.titleScreen));
        ReplaceAll(d, "title", FormatCodesToRml(t.title));
        ReplaceAll(d, "cause", FormatCodesToRml(t.cause));
        ReplaceAll(d, "respawn", EscapeRml(t.respawn));
        return d;
    }
}
