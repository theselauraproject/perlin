#pragma once
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace perlin::detail {
    inline bool IsAmmoFor(std::string_view weapon, std::string_view item) {
        if (item == "minecraft:arrow" || item == "minecraft:spectral_arrow" ||
            item == "minecraft:tipped_arrow")
            return true;
        return weapon == "minecraft:crossbow" && item == "minecraft:firework_rocket";
    }

    inline bool HasAmmo(const ui::HudState& st, BedrockSession* live, const std::string& weapon) {
        if ((weapon != "minecraft:bow" && weapon != "minecraft:crossbow") || !st.Survival())
            return true;
        if (weapon == "minecraft:crossbow" && live && live->HeldItemIsChargedCrossbow())
            return true;
        const auto ammo = [&](const auto& stack) { return !stack.Empty() && IsAmmoFor(weapon, stack.name); };
        if (ammo(st.offhand))
            return true;
        if (!live)
            return std::ranges::any_of(st.hotbar, ammo);
        const auto inv = live->GetInventory();
        return std::ranges::any_of(inv.hotbar, ammo) || std::ranges::any_of(inv.main, ammo);
    }
}
