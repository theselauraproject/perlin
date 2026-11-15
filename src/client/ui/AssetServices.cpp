#include "client/ui/AssetServices.hpp"

#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "util/Paths.hpp"

namespace perlin::ui {
    void BindAssetServices(UiServices& services) {
        services.textWidth = [](const std::string& text, float s, bool ten) {
            return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
        };
        services.lineHeight = [](float s, bool ten) {
            return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
        };
        services.textureSize = [](const std::string& tex, int& w, int& h) {
            return UiAssets::Instance().ImageSize(tex, w, h);
        };
        services.localize = [](const std::string& key) { return UiAssets::Instance().Localize(key); };
    }

    std::string WarmKey(const std::vector<std::string>& packDirs) {
        std::string wanted;
        for (const std::string& d : packDirs)
            wanted += d + "|";
        wanted += paths::DataDir() + "|" + UiAssets::Instance().VanillaRoot() + "|" +
                  std::to_string(TexturePack::Instance().Generation());
        return wanted;
    }
}
