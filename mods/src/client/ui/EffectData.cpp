#include "client/ui/EffectData.hpp"

#include <cstdint>
#include <cstdio>

namespace perlin::ui {
    bool GetEffectInfo(int id, EffectInfo& out) {
        static const EffectInfo kEffects[] = {
            {},
            {"speed", "Speed", false, 0x33EBFF},
            {"slowness", "Slowness", true, 0x8BAFE0},
            {"haste", "Haste", false, 0xD9C043},
            {"mining_fatigue", "Mining Fatigue", true, 0x4A4217},
            {"strength", "Strength", false, 0xFFC700},
            {"instant_health", "Instant Health", false, 0xF82423},
            {"instant_damage", "Instant Damage", true, 0xA9656A},
            {"jump_boost", "Jump Boost", false, 0xFDFF84},
            {"nausea", "Nausea", true, 0x551D4A},
            {"regeneration", "Regeneration", false, 0xCD5CAB},
            {"resistance", "Resistance", false, 0x9146F0},
            {"fire_resistance", "Fire Resistance", false, 0xFF9900},
            {"water_breathing", "Water Breathing", false, 0x98DAC0},
            {"invisibility", "Invisibility", false, 0xF6F6F6},
            {"blindness", "Blindness", true, 0x1F1F23},
            {"night_vision", "Night Vision", false, 0xC2FF66},
            {"hunger", "Hunger", true, 0x587653},
            {"weakness", "Weakness", true, 0x484D48},
            {"poison", "Poison", true, 0x87A363},
            {"wither", "Wither", true, 0x736156},
            {"health_boost", "Health Boost", false, 0xF87D23},
            {"absorption", "Absorption", false, 0x2552A5},
            {"saturation", "Saturation", false, 0xF82421},
            {"levitation", "Levitation", true, 0xCEFFFF},
            {"poison", "Fatal Poison", true, 0x4E9331},
            {"conduit_power", "Conduit Power", false, 0x1DC2D1},
            {"slow_falling", "Slow Falling", false, 0xF3CFB9},
            {"bad_omen", "Bad Omen", true, 0x0B6138},
            {"hero_of_the_village", "Hero of the Village", false, 0x44FF44},
            {"darkness", "Darkness", true, 0x292721},
            {"trial_omen", "Trial Omen", true, 0x16A6A6},
            {"wind_charged", "Wind Charged", true, 0xBDC9FF},
            {"weaving", "Weaving", true, 0x78695A},
            {"oozing", "Oozing", true, 0x99FFA3},
            {"infested", "Infested", true, 0x8C9B8C},
            {"raid_omen", "Raid Omen", true, 0xDE4058},
            {"breath_of_the_nautilus", "Breath of the Nautilus", false, 0x98DAC0}};
        if (id <= 0 || id >= static_cast<int>(sizeof(kEffects) / sizeof(kEffects[0])))
            return false;
        out = kEffects[id];
        return true;
    }

    std::string EffectLabel(const HudEffect& effect) {
        EffectInfo info;
        if (!GetEffectInfo(effect.id, info))
            return "Effect " + std::to_string(effect.id);
        static const char* const kRoman[] = {"", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
        std::string label = info.name;
        if (effect.amplifier >= 1 && effect.amplifier <= 9)
            label += std::string(" ") + kRoman[effect.amplifier];
        else if (effect.amplifier > 9)
            label += " " + std::to_string(effect.amplifier + 1);
        return label;
    }

    std::string EffectDuration(const HudEffect& effect) {
        if (effect.ticks < 0 || effect.ticks >= 32767 * 20)
            return "**:**";
        const int seconds = effect.ticks / 20;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d:%02d", seconds / 60, seconds % 60);
        return buf;
    }
}
