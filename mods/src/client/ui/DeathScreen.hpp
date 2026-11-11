#pragma once
#include <cstdint>
#include <string>

namespace perlin::ui {
    struct DeathScreenText {
        std::string title, cause, respawn, titleScreen;
        bool buttonsEnabled = true;
    };

    inline constexpr const char* kDeathRespawnId = "respawn";
    inline constexpr const char* kDeathTitleId = "title";

    std::string DefaultDeathScreenTemplate();
    std::string EmbeddedDeathScreenTemplate();
    std::string LoadDeathScreenTemplate(const std::string& dataDir, uint64_t& version);
    std::string BuildDeathScreenRml(const std::string& templateText, const DeathScreenText& text, int guiW,
                                    int guiH);
}
