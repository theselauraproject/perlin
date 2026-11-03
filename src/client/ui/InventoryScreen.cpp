#include "client/ui/InventoryScreen.hpp"

#include "client/ui/EffectData.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace perlin::ui {
    std::string DefaultInventoryScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:11-->
<rml>
<head>
<title>inventory</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcitem { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(16,16,16,200); }
#panel { left: {{x}}px; top: {{y}}px; width: 176px; height: 166px; }
.r { position: absolute; }
.label { font-family: minecraft; font-size: 8px; line-height: 9px; color: #404040; white-space: nowrap; }
.slot { width: 18px; height: 18px; z-index: 2; }
.slot .hl { display: none; left: 1px; top: 1px; width: 16px; height: 16px; background-color: rgba(255,255,255,128); z-index: 5; pointer-events: none; }
.slot:hover .hl { display: block; }
.slot mcitem { z-index: 4; }
.cnt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; text-align: right; white-space: nowrap; z-index: 6; }
#cursor { z-index: 30; pointer-events: none; width: 16px; height: 16px; }
#cursor .cnt { z-index: 31; }
.rbtn { width: 20px; height: 18px; z-index: 6; }
.rbtn .rh { display: none; left: 0px; top: 0px; }
.rbtn:hover .rh { display: block; }
.rbtn .rn { left: 0px; top: 0px; }
.fx { z-index: 2; }
.fxn { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 4; }
.fxd { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #7f7f7f; white-space: nowrap; z-index: 4; }
.rc { width: 25px; height: 25px; z-index: 3; }
.rc mcitem { z-index: 4; }
.pg { width: 12px; height: 17px; z-index: 6; }
.pg .ph { display: none; left: 0px; top: 0px; }
.pg:hover .ph { display: block; }
{{tipstyle}}</style>
</head>
<body>
  <div id="dim"></div>
  {{book}}
  {{effects}}
  <div id="panel">{{chrome}}</div>
  {{bookbutton}}
  <div class="label" style="left: {{lx}}px; top: {{ly}}px; z-index: 3;">{{title}}</div>
  {{slots}}
  <div id="top" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; z-index: 50; pointer-events: none;"><mclayer layer="top" style="display: block; position: absolute; width: 0px; height: 0px; z-index: -999;"></mclayer>{{cursor}}{{tooltip}}<mclayer layer="gui" style="display: block; position: absolute; width: 0px; height: 0px; z-index: 999;"></mclayer></div>
</body>
</rml>
)PERLINRML";
    }

    std::string PrettyItemName(const std::string& id) {
        std::string name = id;
        if (const size_t colon = name.find(':'); colon != std::string::npos)
            name = name.substr(colon + 1);
        bool start = true;
        for (char& c : name) {
            if (c == '_') {
                c = ' ';
                start = true;
            } else {
                if (start)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                start = false;
            }
        }
        return name;
    }

    int InventoryAddrFromId(const std::string& id) {
        if (id == "off")
            return kInvAddrOffhand;
        if (id == "co")
            return kInvAddrOutput;
        if (id.size() < 2 || !std::isdigit(static_cast<unsigned char>(id[1])))
            return -1;
        const int v = std::atoi(id.c_str() + 1);
        if (id[0] == 's' && v >= 0 && v < 36)
            return v;
        if (id[0] == 'a' && v >= 0 && v < 4)
            return kInvAddrArmor + v;
        if (id[0] == 'c' && v >= 0 && v < 4)
            return kInvAddrCraft + v;
        if (id[0] == 'w' && v >= 0 && v < 9)
            return kInvAddrWork + v;
        if (id[0] == 'k' && v >= 0 && v < 54)
            return kInvAddrChest + v;
        if (id[0] == 'h' && v >= 0 && v < 17)
            return kInvAddrHorse + v;
        return -1;
    }

    int InventoryPanelX(const InventoryView& v, int guiW) {
        return (guiW - kInvPanelW) / 2 + (v.bookOpen ? 77 : 0);
    }

    int InventoryPanelH(const InventoryView& v) {
        return v.chestRows > 0 ? 114 + v.chestRows * 18 : kInvPanelH;
    }

    void InventorySlotPos(int addr, int& x, int& y, bool workbench, int chestRows) {
        x = y = 0;
        if (addr >= kInvAddrChest && chestRows < 0) {
            const int c = addr - kInvAddrChest;
            x = c == 2 ? 115 : 55;
            y = c == 0 ? 16 : c == 1 ? 52 : 34;
            return;
        }
        if (addr >= kInvAddrChest) {
            const int c = addr - kInvAddrChest;
            x = 7 + (c % 9) * 18;
            y = 17 + (c / 9) * 18;
            return;
        }
        if (chestRows > 0 && addr < 36) {
            x = 7 + (addr % 9) * 18;
            y = addr < 9 ? chestRows * 18 + 88 : chestRows * 18 + 30 + ((addr - 9) / 9) * 18;
            return;
        }
        if (addr >= kInvAddrHorse) {
            const int h = addr - kInvAddrHorse;
            if (h == 0) {
                x = 7;
                y = 17;
            } else if (h == 1) {
                x = 7;
                y = 35;
            } else {
                x = 79 + ((h - 2) % 5) * 18;
                y = 17 + ((h - 2) / 5) * 18;
            }
            return;
        }
        if (addr >= kInvAddrWork) {
            x = 30 + ((addr - kInvAddrWork) % 3) * 18;
            y = 17 + ((addr - kInvAddrWork) / 3) * 18;
            return;
        }
        if (addr == kInvAddrOutput && workbench) {
            x = 124;
            y = 35;
            return;
        }
        if (addr < 9) {
            x = 7 + addr * 18;
            y = 141;
        } else if (addr < 36) {
            x = 7 + ((addr - 9) % 9) * 18;
            y = 83 + ((addr - 9) / 9) * 18;
        } else if (addr < 40) {
            x = 7;
            y = 7 + (addr - 36) * 18;
        } else if (addr == kInvAddrOffhand) {
            x = 76;
            y = 61;
        } else if (addr < kInvAddrOutput) {
            x = 97 + ((addr - kInvAddrCraft) % 2) * 18;
            y = 17 + ((addr - kInvAddrCraft) / 2) * 18;
        } else {
            x = 153;
            y = 27;
        }
    }

    namespace {
        std::string Chrome(bool workbench, bool horse = false, int chestRows = 0, int furnace = -1) {
            if (furnace >= 0)
                return SpriteRml(furnace == 1 ? "ui_blast_furnace.png" : furnace == 2 ? "ui_smoker.png" : "ui_furnace.png", 0,
                                 0, 176, 166, 0, 0);
            if (chestRows > 0) {
                const int top = 17 + chestRows * 18;
                return SpriteRml("ui_chest.png", 0, 0, 176, top, 0, 0) +
                       SpriteRml("ui_chest.png", 0, 126, 176, 96, 0, top);
            }
            if (horse)
                return SpriteRml("ui_horse.png", 0, 0, 176, 166, 0, 0);
            return SpriteRml(workbench ? "ui_crafting_table.png" : "ui_inventory.png", 0, 0, 176, 166, 0, 0);
        }

        std::string Cell(const std::string& id, int px, int py, int addr, const HudItem& item, bool workbench,
                         int chestRows = 0) {
            int x, y;
            InventorySlotPos(addr, x, y, workbench, chestRows);
            std::string s = "  <div id=\"" + id + "\" class=\"slot\" style=\"left:" + std::to_string(px + x) +
                            "px;top:" + std::to_string(py + y) + "px;\">";
            if (addr == kInvAddrHorse || addr == kInvAddrHorse + 1) {
                s += SpriteRml("ui_horse_slot.png", 0, 0, 18, 18, 0, 0);
                if (item.Empty())
                    s += SpriteRml("ui_horse_icons.png", (addr - kInvAddrHorse) * 16, 0, 16, 16, 1, 1);
            }
            if (item.Empty() &&
                ((addr >= kInvAddrArmor && addr < kInvAddrOffhand) || addr == kInvAddrOffhand)) {
                const int icon = addr == kInvAddrOffhand ? 4 : addr - kInvAddrArmor;
                s += SpriteRml("ui_slot_icons.png", icon * 16, 0, 16, 16, 1, 1);
            }
            if (!item.Empty()) {
                s += "<mcitem key=\"" + EscapeRml(item.Key()) +
                     "\" style=\"left:1px;top:1px;width:16px;height:16px;\"";
                if (item.enchanted)
                    s += " glint=\"1\"";
                s += "/>";
                if (item.count > 1)
                    s += "<div class=\"cnt\" style=\"left:-1px;top:10px;width:19px;height:9px;\">" +
                         std::to_string(item.count) + "</div>";
            }
            s += "<div class=\"hl\"></div></div>\n";
            return s;
        }
    }

    std::string BuildEffectsPanel(const std::vector<HudEffect>& effects, int x, int y, int guiW) {
        std::string out;
        if (effects.empty())
            return out;
        const int space = guiW - x;
        if (space < 32)
            return out;
        const bool wide = space >= 120;
        const int count = static_cast<int>(effects.size());
        const int gap = count > 5 ? 132 / (count - 1) : 33;
        for (int i = 0; i < count; i++) {
            const HudEffect& e = effects[static_cast<size_t>(i)];
            EffectInfo info;
            if (!GetEffectInfo(e.id, info))
                continue;
            const int ey = y + i * gap;
            const int w = wide ? 120 : 32;
            out += "  <div class=\"fx\" style=\"left:" + std::to_string(x) + "px;top:" + std::to_string(ey) +
                   "px;width:" + std::to_string(w) + "px;height:32px;\">";
            out += "<mcimg src=\"mcres:" +
                   std::string(e.ambient ? "ui_inv_effect_background_ambient.png"
                                         : "ui_inv_effect_background.png") +
                   "\" nine=\"4 4 4 4\" style=\"left:0px;top:0px;width:" + std::to_string(w) +
                   "px;height:32px;z-index:1;\"/>";
            out += "<mcimg src=\"mcres:effect_" + std::string(info.texture) +
                   ".png\" style=\"left:7px;top:7px;width:18px;height:18px;z-index:3;\"/>";
            if (wide) {
                out +=
                    "<div class=\"fxn\" style=\"left:28px;top:6px;\">" + EscapeRml(EffectLabel(e)) + "</div>";
                out += "<div class=\"fxd\" style=\"left:28px;top:16px;\">" + EscapeRml(EffectDuration(e)) +
                       "</div>";
            }
            out += "</div>\n";
        }
        return out;
    }

    std::string BuildInventoryScreenRml(const std::string& templateText, const InventoryView& v, int guiW,
                                        int guiH) {
        const int px = InventoryPanelX(v, guiW), py = (guiH - InventoryPanelH(v)) / 2;
        const bool wb = v.workbench;
        const int rows = v.chestRows;
        std::string slots;
        if (v.furnace >= 0) {
            for (int i = 0; i < 3; i++)
                slots += Cell("k" + std::to_string(i), px, py, kInvAddrChest + i, v.chestItems[static_cast<size_t>(i)],
                              false, -1);
            const int flame = static_cast<int>(std::ceil(v.burn * 13.0f));
            if (flame > 0)
                slots += "  <div style=\"left:" + std::to_string(px + 56) + "px;top:" + std::to_string(py + 36 + 14 - flame) +
                         "px;width:14px;height:" + std::to_string(flame) + "px;z-index:3;\">" +
                         SpriteRml("ui_furnace_progress.png", 0, 14 - flame, 14, flame, 0, 0) + "</div>\n";
            const int arrow = static_cast<int>(std::floor(v.cook * 24.0f));
            if (arrow > 0)
                slots += "  <div style=\"left:" + std::to_string(px + 79) + "px;top:" + std::to_string(py + 34) +
                         "px;width:" + std::to_string(arrow) + "px;height:16px;z-index:3;\">" +
                         SpriteRml("ui_furnace_progress.png", 0, 14, arrow, 16, 0, 0) + "</div>\n";
            slots += "  <div class=\"label\" style=\"left:" + std::to_string(px + 8) + "px;top:" + std::to_string(py + 72) +
                     "px;z-index:3;\">" + EscapeRml(i18n::Tr("container.inventory")) + "</div>\n";
        } else if (rows > 0) {
            for (int i = 0; i < rows * 9; i++)
                slots += Cell("k" + std::to_string(i), px, py, kInvAddrChest + i,
                              v.chestItems[static_cast<size_t>(i)], false, rows);
            slots += "  <div class=\"label\" style=\"left:" + std::to_string(px + 8) +
                     "px;top:" + std::to_string(py + rows * 18 + 20) + "px;z-index:3;\">" +
                     EscapeRml(i18n::Tr("container.inventory")) + "</div>\n";
        } else if (v.horse) {
            slots += Cell("h0", px, py, kInvAddrHorse, v.horseItems[0], false);
            slots += Cell("h1", px, py, kInvAddrHorse + 1, v.horseItems[1], false);
            if (v.horseChest > 0) {
                slots += "  <div style=\"left:" + std::to_string(px + 79) +
                         "px;top:" + std::to_string(py + 17) + "px;width:90px;height:54px;z-index:1;\">" +
                         SpriteRml("ui_horse_chest.png", 0, 0, 90, 54, 0, 0) + "</div>\n";
                for (int i = 0; i < std::min(v.horseChest, 15); i++)
                    slots += Cell("h" + std::to_string(i + 2), px, py, kInvAddrHorse + 2 + i,
                                  v.horseItems[static_cast<size_t>(i + 2)], false);
            }
            slots += "  <div class=\"label\" style=\"left:" + std::to_string(px + 8) +
                     "px;top:" + std::to_string(py + 72) + "px;z-index:3;\">" +
                     EscapeRml(i18n::Tr("container.inventory")) + "</div>\n";
        } else if (!wb) {
            for (int i = 0; i < 4; i++)
                slots += Cell("a" + std::to_string(i), px, py, kInvAddrArmor + i,
                              v.armor[static_cast<size_t>(i)], wb);
            slots += Cell("off", px, py, kInvAddrOffhand, v.offhand, wb);
            for (int i = 0; i < 4; i++)
                slots += Cell("c" + std::to_string(i), px, py, kInvAddrCraft + i,
                              v.craft[static_cast<size_t>(i)], wb);
        } else {
            for (int i = 0; i < 9; i++)
                slots += Cell("w" + std::to_string(i), px, py, kInvAddrWork + i,
                              v.work[static_cast<size_t>(i)], wb);
        }
        if (!v.horse && rows == 0 && v.furnace < 0)
            slots += Cell("co", px, py, kInvAddrOutput, v.output, wb);
        for (int i = 0; i < 36; i++)
            slots += Cell("s" + std::to_string(i), px, py, i, v.slots[static_cast<size_t>(i)], wb, rows);

        const int bx = (guiW - 147) / 2 - 86;
        std::string book;
        if (v.bookOpen) {
            book += "  <div id=\"bookpanel\" style=\"left:" + std::to_string(bx) +
                    "px;top:" + std::to_string(py) + "px;width:147px;height:166px;z-index:2;\">" +
                    SpriteRml("ui_recipe_book.png", 0, 0, 147, 166, 0, 0) + "</div>\n";
            for (size_t n = 0; n < v.recipes.size(); n++) {
                const RecipeCard& c = v.recipes[n];
                const int cx = bx + 11 + static_cast<int>(n % kBookCols) * 25,
                          cy = py + 31 + static_cast<int>(n / kBookCols) * 25;
                book += "  <div id=\"r" + std::to_string(n) +
                        "\" class=\"rc\" style=\"left:" + std::to_string(cx) +
                        "px;top:" + std::to_string(cy) + "px;\">" +
                        SpriteRml("ui_recipe_slots.png", c.craftable ? 0 : 25, 0, 25, 25, 0, 0) +
                        "<mcitem key=\"" + EscapeRml(c.out.Key()) +
                        "\" style=\"left:4px;top:4px;width:16px;height:16px;\"/>";
                if (c.out.count > 1)
                    book += "<div class=\"cnt\" style=\"left:3px;top:13px;width:19px;height:9px;\">" +
                            std::to_string(c.out.count) + "</div>";
                book += "</div>\n";
            }
            if (v.bookPage > 0)
                book += "  <div id=\"rp\" class=\"pg\" style=\"left:" + std::to_string(bx + 38) +
                        "px;top:" + std::to_string(py + 137) + "px;\">" +
                        SpriteRml("ui_recipe_pages.png", 0, 0, 12, 17, 0, 0) + "<div class=\"ph\">" +
                        SpriteRml("ui_recipe_pages.png", 12, 0, 12, 17, 0, 0) + "</div></div>\n";
            if (v.bookPage + 1 < v.bookPages)
                book += "  <div id=\"rn\" class=\"pg\" style=\"left:" + std::to_string(bx + 93) +
                        "px;top:" + std::to_string(py + 137) + "px;\">" +
                        SpriteRml("ui_recipe_pages.png", 24, 0, 12, 17, 0, 0) + "<div class=\"ph\">" +
                        SpriteRml("ui_recipe_pages.png", 36, 0, 12, 17, 0, 0) + "</div></div>\n";
            book += "  <div class=\"label\" style=\"left:" + std::to_string(bx + 54) +
                    "px;top:" + std::to_string(py + 141) + "px;z-index:6;\">" +
                    std::to_string(v.bookPage + 1) + "/" + std::to_string(std::max(1, v.bookPages)) +
                    "</div>\n";
        }
        const int btnX = wb ? px + 5 : px + 104, btnY = wb ? py + 34 : py + 61;
        const std::string bookButton =
            v.horse || rows > 0
                ? std::string()
                : "  <div id=\"rb\" class=\"rbtn\" style=\"left:" + std::to_string(btnX) +
                      "px;top:" + std::to_string(btnY) + "px;\"><div class=\"rn\">" +
                      SpriteRml("ui_recipe_button.png", 0, 0, 20, 18, 0, 0) + "</div><div class=\"rh\">" +
                      SpriteRml("ui_recipe_button.png", 0, 18, 20, 18, 0, 0) + "</div></div>\n";

        std::string effects = BuildEffectsPanel(v.effects, px + kInvPanelW + 2, py, guiW);

        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x", std::to_string(px));
        ReplaceAll(d, "y", std::to_string(py));
        ReplaceAll(d, "lx", std::to_string(px + (v.furnace >= 0 ? 88 - static_cast<int>(v.title.size()) * 3 : v.horse || rows > 0 ? 8 : wb ? 28 : 97)));
        ReplaceAll(d, "ly", std::to_string(py + (v.furnace >= 0 || v.horse || rows > 0 ? 6 : wb ? 6 : 8)));
        ReplaceAll(d, "title",
                   FormatCodesToRml(v.title.empty()
                                        ? i18n::Tr(v.horse ? "entity.minecraft.horse" : "container.crafting")
                                        : v.title));
        ReplaceAll(d, "chrome", Chrome(wb, v.horse, rows, v.furnace));
        ReplaceAll(d, "book", book);
        ReplaceAll(d, "effects", effects);
        ReplaceAll(d, "bookbutton", bookButton);
        ReplaceAll(d, "slots", slots);
        ReplaceAll(d, "cursor", InventoryCursorRml(v));
        ReplaceAll(d, "tooltip", InventoryTooltipRml(v, guiW, guiH));
        ReplaceAll(d, "tipstyle", TooltipStyles());
        return d;
    }

    std::string InventoryCursorRml(const InventoryView& v) {
        std::string cursor;
        if (!v.cursor.Empty()) {
            cursor = "  <div id=\"cursor\" style=\"left:" + std::to_string(static_cast<int>(v.mouseX) - 8) +
                     "px;top:" + std::to_string(static_cast<int>(v.mouseY) - 8) + "px;\"><mcitem key=\"" +
                     EscapeRml(v.cursor.Key()) + "\" style=\"left:0px;top:0px;width:16px;height:16px;\"" +
                     (v.cursor.enchanted ? " glint=\"1\"" : "") + "/>";
            if (v.cursor.count > 1)
                cursor += "<div class=\"cnt\" style=\"left:-3px;top:9px;width:19px;height:9px;\">" +
                          std::to_string(v.cursor.count) + "</div>";
            cursor += "</div>\n";
        }
        return cursor;
    }

    std::string InventoryTooltipRml(const InventoryView& v, int guiW, int guiH) {
        std::string tip;
        if (v.cursor.Empty()) {
            const HudItem* it = nullptr;
            if (v.hovered >= 0) {
                if (v.hovered >= kInvAddrChest)
                    it = &v.chestItems[static_cast<size_t>(std::min(v.hovered - kInvAddrChest, 53))];
                else if (v.hovered >= kInvAddrHorse)
                    it = &v.horseItems[static_cast<size_t>(std::min(v.hovered - kInvAddrHorse, 16))];
                else if (v.hovered < 36)
                    it = &v.slots[static_cast<size_t>(v.hovered)];
                else if (v.hovered < kInvAddrOffhand)
                    it = &v.armor[static_cast<size_t>(v.hovered - kInvAddrArmor)];
                else if (v.hovered == kInvAddrOffhand)
                    it = &v.offhand;
                else if (v.hovered < kInvAddrOutput)
                    it = &v.craft[static_cast<size_t>(v.hovered - kInvAddrCraft)];
                else if (v.hovered == kInvAddrOutput)
                    it = &v.output;
                else if (v.hovered - kInvAddrWork < 9)
                    it = &v.work[static_cast<size_t>(v.hovered - kInvAddrWork)];
            } else if (v.hoveredId.size() > 1 && v.hoveredId[0] == 'r' &&
                       std::isdigit(static_cast<unsigned char>(v.hoveredId[1]))) {
                const size_t n = static_cast<size_t>(std::atoi(v.hoveredId.c_str() + 1));
                if (n < v.recipes.size())
                    it = &v.recipes[n].out;
            }
            if (it && !it->Empty())
                tip = TooltipRml(BuildItemTooltip(*it, v.advancedTooltips), static_cast<int>(v.mouseX),
                                 static_cast<int>(v.mouseY), guiW, guiH);
        }
        return tip;
    }
}
