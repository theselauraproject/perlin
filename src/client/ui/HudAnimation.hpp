#pragma once
#include "client/systems/model/HudState.hpp"

#include <chrono>
#include <cstdint>

namespace perlin::ui {
    struct HudAnimation {
        using TimePoint = std::chrono::steady_clock::time_point;

        uint64_t ticks = 0;
        int toolTimer = 0;
        HudItem lastTool;
        TimePoint titleAtSeen, actionAtSeen;
        int overlayTime = 0, actionSincePush = 1000, titleTime = 0, titleFadeIn = 10, titleStay = 70, titleFadeOut = 20;
        bool haveHealth = false;
        int lastHealth = 0;
        int lastHealthTick = 0, lastHurtTick = -1000;
        int blinkUntil = 0, displayHealth = 0;
    };
}
