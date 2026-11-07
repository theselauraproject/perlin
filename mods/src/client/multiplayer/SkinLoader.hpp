#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin {

    struct SkinConfig {
        std::string skinPath;
        std::string capePath;
        bool slimArms = false;
        bool autoSlim = false;
    };

    struct LoadedSkin {
        int skinWidth = 0, skinHeight = 0;
        std::vector<uint8_t> skinRgba;
        int capeWidth = 0, capeHeight = 0;
        std::vector<uint8_t> capeRgba;
        bool slimArms = false;
        std::string skinId;
        std::string capeId;
        bool hasSkin() const { return !skinRgba.empty(); }
        bool hasCape() const { return !capeRgba.empty(); }
    };

    bool DetectSlimSkin(const std::vector<uint8_t>& rgba, int width, int height);

    bool LoadSkinConfig(const SkinConfig& config, LoadedSkin& out, std::string& error);

    std::vector<uint8_t> UpgradeLegacySkin(const std::vector<uint8_t>& rgba64x32);

}
