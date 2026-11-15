#include "client/screen/screens/CreativeScreen.hpp"
#include "util/Resources.hpp"

#include "client/screen/screens/InventoryScreen.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cstdlib>

namespace perlin::ui {
    std::string DefaultCreativeScreenTemplate() {
        return ui::ReadMarkup("ui/screens/creative_screen.rml");
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
        void Cell(SceneBuilder& b, const std::string& id, int px, int py, bool survival, const HudItem& item,
                  int iconIndex) {
            int x, y;
            CreativeSlotPos(id, survival, x, y);
            BeginSlot(b, id, px + x, py + y);
            if (item.Empty() && iconIndex >= 0)
                AddSprite(b, "ui_slot_icons.png", iconIndex * 16, 0, 16, 16, 1, 1);
            AddSlotItem(b, item, -1, 10);
            EndSlot(b);
        }

        const char* ChromeFor(int tab) {
            if (tab == kCreativeSurvivalTab)
                return "ui_creative_survival.png";
            if (tab == kCreativeSearchTab)
                return "ui_creative_search.png";
            return "ui_creative_items.png";
        }
    }

    SceneDoc BuildCreativeScreen(const std::string& templateText, const CreativeView& v, int guiW, int guiH) {
        const int px = (guiW - kCreativeW) / 2, py = (guiH - kCreativeH) / 2;
        const bool survival = v.tab == kCreativeSurvivalTab;
        SceneDoc doc;
        SceneBuilder tabs(doc.scene.lists["tabs"]);
        for (int t = 0; t < kCreativeTabs; t++) {
            if (!v.tabPresent[static_cast<size_t>(t)])
                continue;
            int x, y;
            bool top;
            CreativeTabPos(t, x, y, top);
            const bool selected = t == v.tab;
            const int row = (top ? 0 : 2) + (selected ? 1 : 0);
            const int col = t == kCreativeSearchTab || t == kCreativeSurvivalTab ? 6 : t;
            kit::Begin(tabs, static_cast<float>(px + x), static_cast<float>(py + y), 26, 32,
                       selected ? "tab on" : "tab off", "t" + std::to_string(t), selected ? "3" : "1");
            AddSprite(tabs, "ui_creative_tabs.png", col * 26, row * 32, 26, 32, 0, 0);
            tabs.End();
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
            SceneNode& node = kit::Item(tabs, static_cast<float>(px + x + 5), static_cast<float>(iy), 16, 16, icon.Key(), "4");
            node.cls = "ticon";
        }

        SceneBuilder slots(doc.scene.lists["slots"]);
        SceneBuilder search(doc.scene.lists["search"]);
        SceneBuilder scrollbar(doc.scene.lists["scrollbar"]);
        kit::Label(slots, static_cast<float>(px + 8), static_cast<float>(py + 6), 0, "label",
                   survival ? std::vector<SceneRun>() : kit::Formatted(v.title), "3");
        if (survival) {
            for (int i = 0; i < 4; i++)
                Cell(slots, "a" + std::to_string(i), px, py, true, v.armor[static_cast<size_t>(i)], i);
            Cell(slots, "off", px, py, true, v.offhand, 4);
            for (int i = 9; i < 36; i++)
                Cell(slots, "s" + std::to_string(i), px, py, true, v.slots[static_cast<size_t>(i)], -1);
            Cell(slots, "del", px, py, true, HudItem{}, -1);
        } else {
            for (int i = 0; i < 45; i++)
                Cell(slots, "g" + std::to_string(i), px, py, false, v.grid[static_cast<size_t>(i)], -1);
            const int thumb = 18 + static_cast<int>(97.0f * std::clamp(v.scroll, 0.0f, 1.0f));
            kit::Begin(scrollbar, static_cast<float>(px + 175), static_cast<float>(py + thumb), 12, 15, {}, "scroll", "3");
            AddSprite(scrollbar, "ui_creative_misc.png", v.scrollable ? 0 : 12, 0, 12, 15, 0, 0);
            scrollbar.End();
            kit::Box(scrollbar, static_cast<float>(px + 174), static_cast<float>(py + 18), 14, 112, {}, "sbar");
            if (v.tab == kCreativeSearchTab)
                kit::Label(search, static_cast<float>(px + 84), static_cast<float>(py + 6), 0, "search",
                           TextFieldRuns(v.search, true), "3");
        }
        for (int i = 0; i < 9; i++)
            Cell(slots, "s" + std::to_string(i), px, py, survival, v.slots[static_cast<size_t>(i)], -1);

        SceneBuilder cursor(doc.scene.lists["cursor"]);
        if (!v.cursor.Empty()) {
            kit::Begin(cursor, static_cast<float>(static_cast<int>(v.mouseX) - 8),
                       static_cast<float>(static_cast<int>(v.mouseY) - 8), 16, 16, {}, "cursor", "30");
            SceneNode& item = kit::Item(cursor, 0, 0, 16, 16, v.cursor.Key());
            if (v.cursor.enchanted)
                item.glint = "1";
            if (v.cursor.count > 1)
                kit::Label(cursor, -3, 9, 19, "cnt", kit::Plain(std::to_string(v.cursor.count)), "31");
            cursor.End();
        }

        SceneBuilder tooltip(doc.scene.lists["tooltip"]);
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
            AddTooltip(tooltip, lines, static_cast<int>(v.mouseX), static_cast<int>(v.mouseY), guiW, guiH);
        }

        SceneBuilder chrome(doc.scene.lists["chrome"]);
        AddSprite(chrome, ChromeFor(v.tab), 0, 0, kCreativeW, kCreativeH, 0, 0);
        SceneBuilder effects(doc.scene.lists["effects"]);
        AddEffectsPanel(effects, v.effects, px + kCreativeW + 2, py, guiW);

        doc.rml = templateText;
        ReplaceAll(doc.rml, "w", std::to_string(guiW));
        ReplaceAll(doc.rml, "h", std::to_string(guiH));
        ReplaceAll(doc.rml, "x", std::to_string(px));
        ReplaceAll(doc.rml, "y", std::to_string(py));
        EnsureSceneLists(doc);
        return doc;
    }
}
