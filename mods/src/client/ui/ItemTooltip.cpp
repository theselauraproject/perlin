#include "client/ui/ItemTooltip.hpp"

#include "client/i18n/Lang.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiAssets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace perlin::ui {
    namespace {
        struct Enchant {
            const char* name;
            int maxLevel;
            bool curse;
        };

        const Enchant kEnchants[] = {{"Protection", 4, false},
                                     {"Fire Protection", 4, false},
                                     {"Feather Falling", 4, false},
                                     {"Blast Protection", 4, false},
                                     {"Projectile Protection", 4, false},
                                     {"Thorns", 3, false},
                                     {"Respiration", 3, false},
                                     {"Depth Strider", 3, false},
                                     {"Aqua Affinity", 1, false},
                                     {"Sharpness", 5, false},
                                     {"Smite", 5, false},
                                     {"Bane of Arthropods", 5, false},
                                     {"Knockback", 2, false},
                                     {"Fire Aspect", 2, false},
                                     {"Looting", 3, false},
                                     {"Efficiency", 5, false},
                                     {"Silk Touch", 1, false},
                                     {"Unbreaking", 3, false},
                                     {"Fortune", 3, false},
                                     {"Power", 5, false},
                                     {"Punch", 2, false},
                                     {"Flame", 1, false},
                                     {"Infinity", 1, false},
                                     {"Luck of the Sea", 3, false},
                                     {"Lure", 3, false},
                                     {"Frost Walker", 2, false},
                                     {"Mending", 1, false},
                                     {"Curse of Binding", 1, true},
                                     {"Curse of Vanishing", 1, true},
                                     {"Impaling", 5, false},
                                     {"Riptide", 3, false},
                                     {"Loyalty", 3, false},
                                     {"Channeling", 1, false},
                                     {"Multishot", 1, false},
                                     {"Piercing", 4, false},
                                     {"Quick Charge", 3, false},
                                     {"Soul Speed", 3, false},
                                     {"Swift Sneak", 3, false},
                                     {"Wind Burst", 3, false},
                                     {"Density", 5, false},
                                     {"Breach", 4, false}};

        std::string Roman(int n) {
            static const char* const k[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
            if (n >= 1 && n <= 10)
                return k[n];
            return std::to_string(n);
        }

        std::string Num(double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.2f", v);
            std::string s = b;
            while (!s.empty() && s.back() == '0')
                s.pop_back();
            if (!s.empty() && s.back() == '.')
                s.pop_back();
            return s;
        }

        std::string Short(const std::string& id) {
            const size_t c = id.find(':');
            return c == std::string::npos ? id : id.substr(c + 1);
        }

        int Tier(const std::string& name) {
            static const char* const kTiers[] = {"wooden_", "stone_",   "iron_",
                                                 "golden_", "diamond_", "netherite_"};
            for (int i = 0; i < 6; i++)
                if (name.starts_with(kTiers[i]))
                    return i;
            return -1;
        }

        struct Attr {
            const char* slot;
            double damage = 0, speed = 0;
            double armor = 0, toughness = 0, knockback = 0;
        };

        bool Attributes(const std::string& name, Attr& a) {
            const int tier = Tier(name);
            static const double kSword[] = {4, 5, 6, 4, 7, 8};
            static const double kAxe[] = {7, 9, 9, 7, 9, 10};
            static const double kAxeSpeed[] = {0.8, 0.8, 0.9, 1.0, 1.0, 1.0};
            static const double kPick[] = {2, 3, 4, 2, 5, 6};
            static const double kShovel[] = {2.5, 3.5, 4.5, 2.5, 5.5, 6.5};
            static const double kHoeSpeed[] = {1, 2, 3, 1, 4, 4};
            if (tier >= 0) {
                const std::string kind = name.substr(name.find('_') + 1);
                a.slot = "mainhand";
                if (kind == "sword") {
                    a.damage = kSword[tier];
                    a.speed = 1.6;
                    return true;
                }
                if (kind == "axe") {
                    a.damage = kAxe[tier];
                    a.speed = kAxeSpeed[tier];
                    return true;
                }
                if (kind == "pickaxe") {
                    a.damage = kPick[tier];
                    a.speed = 1.2;
                    return true;
                }
                if (kind == "shovel") {
                    a.damage = kShovel[tier];
                    a.speed = 1.0;
                    return true;
                }
                if (kind == "hoe") {
                    a.damage = 1;
                    a.speed = kHoeSpeed[tier];
                    return true;
                }
            }
            if (name == "trident") {
                a.slot = "mainhand";
                a.damage = 9;
                a.speed = 1.1;
                return true;
            }
            if (name == "mace") {
                a.slot = "mainhand";
                a.damage = 6;
                a.speed = 0.6;
                return true;
            }
            static const struct {
                const char* material;
                double v[4];
                double toughness;
                double kb;
            } kArmor[] = {{"leather", {1, 3, 2, 1}, 0, 0},   {"golden", {2, 5, 3, 1}, 0, 0},
                          {"chainmail", {2, 5, 4, 1}, 0, 0}, {"iron", {2, 6, 5, 2}, 0, 0},
                          {"diamond", {3, 8, 6, 3}, 2, 0},   {"netherite", {3, 8, 6, 3}, 3, 0.1}};
            static const char* const kPiece[] = {"_helmet", "_chestplate", "_leggings", "_boots"};
            static const char* const kSlot[] = {"head", "chest", "legs", "feet"};
            for (const auto& m : kArmor)
                for (int i = 0; i < 4; i++)
                    if (name == std::string(m.material) + kPiece[i]) {
                        a.slot = kSlot[i];
                        a.armor = m.v[i];
                        a.toughness = m.toughness;
                        a.knockback = m.kb;
                        return true;
                    }
            if (name == "turtle_helmet") {
                a.slot = "head";
                a.armor = 2;
                return true;
            }
            return false;
        }

        const char* SlotLabel(const char* slot) {
            if (!std::strcmp(slot, "mainhand"))
                return "When in Main Hand:";
            if (!std::strcmp(slot, "head"))
                return "When on Head:";
            if (!std::strcmp(slot, "chest"))
                return "When on Body:";
            if (!std::strcmp(slot, "legs"))
                return "When on Legs:";
            return "When on Feet:";
        }

    }

    int ItemRarity(const HudItem& item) {
        const std::string n = Short(item.name);
        int rarity = 0;
        static const char* const kUncommon[] = {"experience_bottle", "nether_star",   "totem_of_undying",
                                                "heart_of_the_sea",  "dragon_breath", "enchanted_book"};
        static const char* const kRare[] = {"golden_apple", "beacon", "conduit"};
        static const char* const kEpic[] = {
            "enchanted_golden_apple",  "dragon_egg",      "command_block", "chain_command_block",
            "repeating_command_block", "structure_block", "jigsaw"};
        for (const char* k : kUncommon)
            if (n == k)
                rarity = 1;
        for (const char* k : kRare)
            if (n == k)
                rarity = 2;
        for (const char* k : kEpic)
            if (n == k)
                rarity = 3;
        if (n.starts_with("music_disc_"))
            rarity = 2;
        if (!item.enchants.empty() && n != "enchanted_book")
            rarity = rarity <= 1 ? 2 : 3;
        return rarity;
    }

    namespace {
        std::function<std::vector<std::string>(const HudItem&)>& Extender() {
            static std::function<std::vector<std::string>(const HudItem&)> extender;
            return extender;
        }
    }

    void SetTooltipExtender(std::function<std::vector<std::string>(const HudItem&)> extender) {
        Extender() = std::move(extender);
    }

    std::vector<TooltipLine> BuildItemTooltip(const HudItem& item, bool advanced,
                                              const std::string& nameOverride) {
        std::vector<TooltipLine> lines;
        if (item.Empty())
            return lines;
        static const uint32_t kRarityColor[] = {0xFFFFFF, 0xFFFF55, 0x55FFFF, 0xFF55FF};
        const std::string shortName = Short(item.name);
        const bool custom = !item.customName.empty();
        std::string title =
            custom
                ? item.customName
                : (!nameOverride.empty() ? nameOverride : UiAssets::Instance().ItemName(item.name, item.aux));
        lines.push_back({title, kRarityColor[ItemRarity(item)], custom});

        for (const auto& [id, level] : item.enchants) {
            if (id < 0 || id >= static_cast<int>(sizeof(kEnchants) / sizeof(kEnchants[0])))
                continue;
            const Enchant& e = kEnchants[id];
            std::string text = e.name;
            if (!(level == 1 && e.maxLevel == 1))
                text += " " + Roman(level);
            lines.push_back({text, e.curse ? 0xFF5555u : 0xAAAAAAu, false});
        }
        for (const std::string& l : item.lore)
            lines.push_back({l, 0xAA00AA, true});

        Attr attr{};
        if (Attributes(shortName, attr)) {
            lines.push_back({"", 0xFFFFFF, false});
            lines.push_back({SlotLabel(attr.slot), 0xAAAAAA, false});
            if (attr.damage > 0)
                lines.push_back({Num(attr.damage) + " " + std::string("Attack Damage"), 0x00AA00, false});
            if (attr.speed > 0)
                lines.push_back({Num(attr.speed) + " " + std::string("Attack Speed"), 0x00AA00, false});
            if (attr.armor > 0)
                lines.push_back({"+" + Num(attr.armor) + " " + std::string("Armor"), 0x5555FF, false});
            if (attr.toughness > 0)
                lines.push_back(
                    {"+" + Num(attr.toughness) + " " + std::string("Armor Toughness"), 0x5555FF, false});
            if (attr.knockback > 0)
                lines.push_back(
                    {"+" + Num(attr.knockback) + " " + std::string("Knockback Resistance"), 0x5555FF, false});
        }

        if (advanced) {
            if (item.maxDamage > 0)
                lines.push_back({"Durability: " + std::to_string(item.maxDamage - item.aux) + " / " +
                                     std::to_string(item.maxDamage),
                                 0xFFFFFF, false});
            lines.push_back({item.name, 0x555555, false});
            if (item.nbtTags > 0)
                lines.push_back({"NBT: " + std::to_string(item.nbtTags) + " tag(s)", 0x555555, false});
        }
        if (const auto& extend = Extender())
            for (std::string& text : extend(item))
                lines.push_back({std::move(text), 0xAAAAAA, false});
        return lines;
    }

    namespace {
        int gTipW = 0, gTipH = 0;
    }

    void PlaceTooltip(int mouseX, int mouseY, int guiW, int guiH, int& x, int& y) {
        x = mouseX + 12;
        y = mouseY - 12;
        if (x + gTipW > guiW)
            x = std::max(4, mouseX - 12 - gTipW);
        if (y + gTipH > guiH)
            y = std::max(4, guiH - gTipH - 4);
        if (y < 4)
            y = 4;
    }

    std::string TooltipRml(const std::vector<TooltipLine>& input, int mouseX, int mouseY, int guiW,
                           int guiH) {
        std::vector<TooltipLine> lines;
        for (const TooltipLine& l : input) {
            size_t start = 0;
            for (;;) {
                const size_t nl = l.text.find('\n', start);
                TooltipLine part = l;
                part.text = l.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
                while (!part.text.empty() && part.text.back() == '\r')
                    part.text.pop_back();
                lines.push_back(std::move(part));
                if (nl == std::string::npos)
                    break;
                start = nl + 1;
            }
        }
        if (lines.empty())
            return {};
        int widest = 0;
        for (const TooltipLine& l : lines)
            widest = std::max(widest, static_cast<int>(McFont::Instance().Width(l.text, 1.0f)));
        const int estW = widest + 10;
        const int estH = static_cast<int>(lines.size()) * 10 + (lines.size() > 1 ? 2 : 0) + 8;
        gTipW = estW;
        gTipH = estH;
        int x, y;
        PlaceTooltip(mouseX, mouseY, guiW, guiH, x, y);
        char color[16];
        std::string body;
        for (size_t i = 0; i < lines.size(); i++) {
            const TooltipLine& l = lines[i];
            std::snprintf(color, sizeof color, "#%06X", l.color & 0xFFFFFFu);
            body += std::string("<div class=\"ln\" style=\"color:") + color + ";" +
                    (l.italic ? "font-style:italic;" : "") +
                    (i == 0 && lines.size() > 1 ? "margin-bottom:2px;" : "") + "\">" +
                    (l.text.empty() ? std::string("&nbsp;") : FormatCodesToRml(l.text)) + "</div>";
        }
        return "  <div id=\"tip\" style=\"left:" + std::to_string(x) + "px;top:" + std::to_string(y) +
               "px;width:" + std::to_string(estW) + "px;height:" + std::to_string(estH) +
               "px;\"><mcimg class=\"tipb\" src=\"textures/ui/purpleBorder\" nine=\"4 4 4 4\" "
               "style=\"width:" +
               std::to_string(estW) + "px;height:" + std::to_string(estH) +
               "px;\"/><div class=\"txt\" style=\"width:" + std::to_string(estW - 10) + "px;\">" + body +
               "</div></div>\n";
    }

    const char* TooltipStyles() {
        return "mcimg { display: block; position: absolute; }\n#tip { z-index: 40; pointer-events: none; }\n"
               "#tip .tipb { left: 0px; top: 0px; z-index: 1; }\n"
               "#tip .txt { position: absolute; left: 5px; top: 4px; display: block; font-family: "
               "minecraft-shadow; font-size: 8px; line-height: 10px; white-space: nowrap; z-index: 2; }\n"
               "#tip .ln { position: relative; display: block; white-space: nowrap; }\n";
    }
}
