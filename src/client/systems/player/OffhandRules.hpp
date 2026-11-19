#pragma once
#include "client/systems/model/HudState.hpp"
#include "util/Env.hpp"

#include <string>

namespace perlin::offhand {
    inline std::string Bare(const std::string& n) {
        return n.starts_with("minecraft:") ? n.substr(10) : n;
    }

    inline bool Weapon(const ui::HudItem& item) {
        const std::string n = Bare(item.name);
        return n.ends_with("_sword") || n.ends_with("_axe") || n.ends_with("_spear") || n == "mace" ||
               n == "trident";
    }

    inline bool MainIdle(const ui::HudItem& main) {
        const std::string n = Bare(main.name);
        return main.Empty() || n.ends_with("_sword") || n.ends_with("_pickaxe") || n.ends_with("_spear") ||
               n == "mace";
    }

    inline bool UseOffhand(const ui::HudItem& main, const ui::HudItem& off) {
        return OffhandExperiment() && !off.Empty() && MainIdle(main);
    }

    inline bool AttackOffhand(const ui::HudItem& main, const ui::HudItem& off) {
        return OffhandExperiment() && main.Empty() && Weapon(off);
    }

    inline const ui::HudItem& UseItem(const ui::HudItem& main, const ui::HudItem& off) {
        return UseOffhand(main, off) ? off : main;
    }
}
