#pragma once
#include "client/ui/EffectData.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::ui {
    struct HudItem {
        std::string name;
        int count = 0;
        int aux = 0;
        uint32_t blockRuntimeId = 0;
        int maxDamage = 0;
        bool enchanted = false;
        std::string customName;
        bool hasColor = false;
        int32_t color = 0;
        int nbtTags = 0;
        std::vector<std::pair<int, int>> enchants;
        std::vector<std::string> lore;
        std::vector<std::string> canDestroy;
        bool Empty() const { return name.empty() || count <= 0; }
        std::string Key() const {
            std::string k = name + ";" + std::to_string(maxDamage > 0 ? 0 : aux);
            if (blockRuntimeId != 0)
                k += "#" + std::to_string(blockRuntimeId);
            if (hasColor)
                k += "@" + std::to_string(static_cast<uint32_t>(color) & 0xFFFFFFu);
            return k;
        }
        bool operator==(const HudItem& o) const {
            return name == o.name && count == o.count && aux == o.aux && blockRuntimeId == o.blockRuntimeId &&
                   maxDamage == o.maxDamage && enchanted == o.enchanted && customName == o.customName &&
                   hasColor == o.hasColor && color == o.color && nbtTags == o.nbtTags &&
                   enchants == o.enchants && lore == o.lore && canDestroy == o.canDestroy;
        }
    };

    enum class GameMode : int { Survival = 0, Creative = 1, Adventure = 2, Spectator = 6 };

    struct LocatorMarker {
        float angle = 0.0f;
        float distance = 0.0f;
        int pitch = 0;
        uint32_t color = 0xFFFFFFu;
    };

    struct HudSidebarRow {
        std::string name;
        int score = 0;
    };

    struct HudBossBar {
        std::string title;
        float progress = 1.0f;
        int color = 0, overlay = 0;
    };

    struct HudState {
        std::vector<HudBossBar> bossBars;
        // server scoreboard in the sidebar slot (SetDisplayObjective "sidebar")
        bool sidebarVisible = false;
        std::string sidebarTitle;
        std::vector<HudSidebarRow> sidebar;
        bool locatorActive = false;
        std::vector<LocatorMarker> locator;
        bool visible = true;
        GameMode mode = GameMode::Survival;

        float health = 20.0f;
        float maxHealth = 20.0f;
        float absorption = 0.0f;
        int food = 20;
        float saturation = 5.0f;
        int armor = 0;
        float xpProgress = 0.0f;
        int xpLevel = 0;
        bool burning = false;
        int fireFrame = 0;
        // OnScreenTextureAnimation / totem: what to show (-1 none, effect id, 1000 totem) and progress 0..1
        int screenAnimation = -1;
        float screenAnimationT = 0;
        int air = 300;
        int maxAir = 300;
        bool inWater = false;
        bool poison = false, wither = false, freezing = false, regeneration = false, hungerEffect = false;
        bool hardcore = false;

        int selectedSlot = 0;
        HudItem hotbar[9];
        HudItem offhand;
        bool offhandRight = false;
        HudItem armorItems[4];

        uint64_t tick = 0;
        int healthBlinkUntil = 0;
        float displayHealth = 20.0f;
        int regenIndex = -1;

        std::string itemName;
        float itemNameAlpha = 0.0f;
        std::string actionBar;
        float actionBarAlpha = 0.0f;
        std::string title;
        std::string subtitle;
        float titleAlpha = 0.0f;

        struct ChatLine {
            std::string text;
            float alpha = 1.0f;
            int row = 0;
            struct Click {
                float x = 0, w = 0;
                std::string command;
            };
            std::vector<Click> clicks;
        };
        struct SubtitleLine {
            std::string text;
            int direction = 0;
            float brightness = 1.0f;
        };
        std::vector<SubtitleLine> subtitles;
        std::vector<ChatLine> chatLines;
        struct TabEntry {
            std::string name;
            std::string headKey;
        };
        bool tabOpen = false;
        std::vector<TabEntry> tabEntries;
        bool chatOpen = false;
        bool chatScrolled = false;
        int chatTotalLines = 0, chatScroll = 0;
        std::string chatInput;
        int chatCaret = 0;
        int chatSelStart = 0, chatSelEnd = 0;
        bool chatCaretVisible = true;
        std::string chatColors;
        std::vector<std::string> chatSuggestions;
        int chatSuggestSelected = -1;
        int chatSuggestStart = 0;
        std::string chatGhost;
        bool chatSuggestMore[2] = {false, false};
        std::string chatHint;
        bool debug = false;
        std::string moveStatus;
        bool advancedTooltips = false;
        std::vector<HudEffect> effects;
        std::string gameModeSwitcher;
        std::vector<std::string> debugLeft, debugRight;
        float guiW = 0, guiH = 0;

        bool showPaperDoll = false;
        bool showCrosshair = true;

        bool Survival() const { return mode == GameMode::Survival || mode == GameMode::Adventure; }
    };
}
