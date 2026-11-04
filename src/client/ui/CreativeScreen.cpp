#include "client/ui/CreativeScreen.hpp"

#include "client/ui/InventoryScreen.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cstdlib>

namespace perlin::ui {
    std::string DefaultCreativeScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:7-->
<rml>
<head>
<title>creative</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcitem { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; decorator: vertical-gradient( rgba(16,16,16,192) rgba(16,16,16,208) ); }
#panel { left: {{x}}px; top: {{y}}px; width: 195px; height: 136px; z-index: 2; }
.tab { width: 26px; height: 32px; }
.tab.off { z-index: 1; }
.tab.on { z-index: 3; }
.ticon { width: 16px; height: 16px; z-index: 4; pointer-events: none; }
.label { font-family: minecraft; font-size: 8px; line-height: 9px; color: #404040; white-space: nowrap; z-index: 3; }
.search { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 3; }
.fx { z-index: 2; }
.fxn { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 4; }
.fxd { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #7f7f7f; white-space: nowrap; z-index: 4; }
.slot { width: 18px; height: 18px; z-index: 3; }
.slot .hl { display: none; left: 1px; top: 1px; width: 16px; height: 16px; background-color: rgba(255,255,255,128); z-index: 5; pointer-events: none; }
.slot:hover .hl { display: block; }
.slot mcitem { z-index: 4; }
.cnt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; text-align: right; white-space: nowrap; z-index: 6; }
#scroll { left: {{sx}}px; top: {{sy}}px; width: 12px; height: 15px; z-index: 3; }
#sbar { left: {{bx}}px; top: {{by}}px; width: 14px; height: 112px; z-index: 3; }
#cursor { z-index: 30; pointer-events: none; width: 16px; height: 16px; }
#cursor .cnt { z-index: 31; }
{{tipstyle}}</style>
</head>
<body>
  <div id="dim"></div>
  <div id="panel">{{chrome}}</div>
  {{tabs}}
  {{effects}}
  <div class="label" style="left: {{lx}}px; top: {{ly}}px;">{{title}}</div>
  {{search}}
  {{scrollbar}}
  {{slots}}
  <div id="top" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; z-index: 50; pointer-events: none;"><mclayer layer="top" style="display: block; position: absolute; width: 0px; height: 0px; z-index: -999;"></mclayer>{{cursor}}{{tooltip}}<mclayer layer="gui" style="display: block; position: absolute; width: 0px; height: 0px; z-index: 999;"></mclayer></div>
</body>
</rml>
)PERLINRML";
    }

    void CreativeTabPos(int tab, int& x, int& y, bool& top) {
        top = tab != kCreativeSurvivalTab;
        const int col = tab == kCreativeSearchTab || tab == kCreativeSurvivalTab ? 6 : tab;
        x = 27 * col;
        if (col == 6)
            x = kCreativeW - 27 + 1;
        y = top ? -28 : kCreativeH - 4;
    }

    void CreativeSlotPos(const std::string& id, bool survival, int& x, int& y) {
        x = y = 0;
        if (id.empty())
            return;
        const int v = id.size() > 1 ? std::atoi(id.c_str() + 1) : 0;
        if (id[0] == 'g') {
            x = 8 + (v % 9) * 18;
            y = 17 + (v / 9) * 18;
        } else if (id[0] == 's' && v < 9) {
            x = 8 + v * 18;
            y = 111;
        } else if (id[0] == 's') {
            x = 8 + ((v - 9) % 9) * 18;
            y = 53 + ((v - 9) / 9) * 18;
        } else if (id[0] == 'a') {
            x = v < 2 ? 53 : 107;
            y = v % 2 == 0 ? 5 : 32;
        } else if (id == "off") {
            x = 34;
            y = 19;
        } else if (id == "del") {
            x = 172;
            y = 111;
        }
        (void)survival;
    }

    namespace {
        std::string Cell(const std::string& id, int px, int py, bool survival, const HudItem& item,
                         int iconIndex) {
            int x, y;
            CreativeSlotPos(id, survival, x, y);
            std::string s = "  <div id=\"" + id + "\" class=\"slot\" style=\"left:" + std::to_string(px + x) +
                            "px;top:" + std::to_string(py + y) + "px;\">";
            if (item.Empty() && iconIndex >= 0)
                s += SpriteRml("ui_slot_icons.png", iconIndex * 16, 0, 16, 16, 1, 1);
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

        const char* ChromeFor(int tab) {
            if (tab == kCreativeSurvivalTab)
                return "ui_creative_survival.png";
            if (tab == kCreativeSearchTab)
                return "ui_creative_search.png";
            return "ui_creative_items.png";
        }
    }

    std::string BuildCreativeScreenRml(const std::string& templateText, const CreativeView& v, int guiW,
                                       int guiH) {
        const int px = (guiW - kCreativeW) / 2, py = (guiH - kCreativeH) / 2;
        const bool survival = v.tab == kCreativeSurvivalTab;
        std::string tabs;
        for (int t = 0; t < kCreativeTabs; t++) {
            if (!v.tabPresent[static_cast<size_t>(t)])
                continue;
            int x, y;
            bool top;
            CreativeTabPos(t, x, y, top);
            const bool selected = t == v.tab;
            const int row = (top ? 0 : 2) + (selected ? 1 : 0);
            const int col = t == kCreativeSearchTab || t == kCreativeSurvivalTab ? 6 : t;
            tabs += "  <div id=\"t" + std::to_string(t) + "\" class=\"tab " + (selected ? "on" : "off") +
                    "\" style=\"left:" + std::to_string(px + x) + "px;top:" + std::to_string(py + y) +
                    "px;\">" + SpriteRml("ui_creative_tabs.png", col * 26, row * 32, 26, 32, 0, 0) +
                    "</div>\n";
            HudItem icon = v.tabIcons[static_cast<size_t>(t)];
            if (t == kCreativeSearchTab) {
                icon = HudItem{};
                icon.name = "minecraft:compass";
                icon.count = 1;
            }
            if (t == kCreativeSurvivalTab)
                icon.name = "minecraft:grass_block", icon.count = 1, icon.blockRuntimeId = 0, icon.aux = 0;
            const int iy = py + y + (top ? 9 : 7);
            if (icon.Empty())
                continue;
            tabs += "  <mcitem key=\"" + EscapeRml(icon.Key()) +
                    "\" class=\"ticon\" style=\"left:" + std::to_string(px + x + 5) +
                    "px;top:" + std::to_string(iy) + "px;width:16px;height:16px;\"/>\n";
        }

        std::string slots;
        std::string search;
        std::string scrollbar;
        if (survival) {
            for (int i = 0; i < 4; i++)
                slots += Cell("a" + std::to_string(i), px, py, true, v.armor[static_cast<size_t>(i)], i);
            slots += Cell("off", px, py, true, v.offhand, 4);
            for (int i = 9; i < 36; i++)
                slots += Cell("s" + std::to_string(i), px, py, true, v.slots[static_cast<size_t>(i)], -1);
            slots += Cell("del", px, py, true, HudItem{}, -1);
        } else {
            for (int i = 0; i < 45; i++)
                slots += Cell("g" + std::to_string(i), px, py, false, v.grid[static_cast<size_t>(i)], -1);
            const int thumb = 18 + static_cast<int>(97.0f * std::clamp(v.scroll, 0.0f, 1.0f));
            scrollbar = "  <div id=\"scroll\" style=\"left:" + std::to_string(px + 175) +
                        "px;top:" + std::to_string(py + thumb) + "px;\">" +
                        SpriteRml("ui_creative_misc.png", v.scrollable ? 0 : 12, 0, 12, 15, 0, 0) +
                        "</div>\n" + "  <div id=\"sbar\" style=\"left:" + std::to_string(px + 174) +
                        "px;top:" + std::to_string(py + 18) + "px;width:14px;height:112px;\"></div>\n";
            if (v.tab == kCreativeSearchTab)
                search = "  <div class=\"search\" style=\"left:" + std::to_string(px + 84) +
                         "px;top:" + std::to_string(py + 6) + "px;\">" + TextFieldRml(v.search, true) +
                         "</div>\n";
        }
        for (int i = 0; i < 9; i++)
            slots += Cell("s" + std::to_string(i), px, py, survival, v.slots[static_cast<size_t>(i)], -1);

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

        std::string tip;
        std::string tipText;
        if (!v.hovered.empty() && v.cursor.Empty()) {
            const int n = v.hovered.size() > 1 ? std::atoi(v.hovered.c_str() + 1) : 0;
            const HudItem* it = nullptr;
            if (v.hovered[0] == 'g' && n >= 0 && n < 45) {
                it = &v.grid[static_cast<size_t>(n)];
                tipText = v.gridNames[static_cast<size_t>(n)];
            } else if (v.hovered[0] == 's' && n >= 0 && n < 36)
                it = &v.slots[static_cast<size_t>(n)];
            else if (v.hovered[0] == 'a' && n >= 0 && n < 4)
                it = &v.armor[static_cast<size_t>(n)];
            else if (v.hovered == "off")
                it = &v.offhand;
            else if (v.hovered[0] == 't' && n >= 0 && n < kCreativeTabs)
                tipText = v.tabTitles[static_cast<size_t>(n)];
            std::vector<TooltipLine> lines;
            if (it && !it->Empty())
                lines = BuildItemTooltip(*it, v.advancedTooltips, tipText);
            else if (!tipText.empty())
                lines.push_back({tipText, 0xFFFFFF, false});
            tip = TooltipRml(lines, static_cast<int>(v.mouseX), static_cast<int>(v.mouseY), guiW, guiH);
        }

        const int thumbY = 18 + static_cast<int>(97.0f * std::clamp(v.scroll, 0.0f, 1.0f));
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x", std::to_string(px));
        ReplaceAll(d, "y", std::to_string(py));
        ReplaceAll(d, "sx", std::to_string(px + 175));
        ReplaceAll(d, "sy", std::to_string(py + thumbY));
        ReplaceAll(d, "bx", std::to_string(px + 174));
        ReplaceAll(d, "by", std::to_string(py + 18));
        ReplaceAll(d, "lx", std::to_string(px + 8));
        ReplaceAll(d, "ly", std::to_string(py + 6));
        ReplaceAll(d, "title", survival ? std::string() : FormatCodesToRml(v.title));
        ReplaceAll(d, "chrome", SpriteRml(ChromeFor(v.tab), 0, 0, kCreativeW, kCreativeH, 0, 0));
        ReplaceAll(d, "tabs", tabs);
        ReplaceAll(d, "effects", BuildEffectsPanel(v.effects, px + kCreativeW + 2, py, guiW));
        ReplaceAll(d, "search", search);
        ReplaceAll(d, "scrollbar", scrollbar);
        ReplaceAll(d, "slots", slots);
        ReplaceAll(d, "cursor", cursor);
        ReplaceAll(d, "tooltip", tip);
        ReplaceAll(d, "tipstyle", TooltipStyles());
        return d;
    }
}
