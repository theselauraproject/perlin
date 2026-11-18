#pragma once
#include "client/systems/config/Options.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/systems/model/HudState.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::ui {
    struct LocatorView {
        double eyeX = 0, eyeY = 0, eyeZ = 0;
        float yaw = 0, pitch = 0, fov = 70;
    };

    struct TitleTimes {
        int remaining = 0;
        int fadeIn = 0, stay = 0, fadeOut = 0;
    };

    GameMode GameModeFromProtocol(int gameType);
    ChatLog::Layout ChatLayoutFor(const ChatSettings& settings);

    std::optional<LocatorMarker> ProjectLocator(const LocatorView& view, double x, double y, double z);

    inline constexpr int kActionBarTicks = 60; // 3 s, anim_actionbar_text_alpha_out
    float ActionBarAlpha(int overlayTime, float partial);
    float TitleAlpha(const TitleTimes& times, float partial);

    int ItemRarity(std::string_view shortName, bool enchanted);
    std::string RarityItemName(int rarity, bool customNamed, const std::string& name);
    float ItemNameAlpha(int toolTimer);

    Texture SkinHeadTexture(const std::vector<uint8_t>& rgba, int skinWidth);
    Texture DefaultHeadTexture();
    bool SkinHasHead(int skinWidth, int skinHeight);
}
