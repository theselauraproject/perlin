#include "client/screen/screens/InventoryScreen.hpp"
#include "util/Resources.hpp"

#include "client/systems/model/EffectData.hpp"
#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/ui/UiSprite.hpp"
#include "client/systems/i18n/Lang.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace perlin::ui {
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
            x = 29 + ((addr - kInvAddrWork) % 3) * 18;
            y = 16 + ((addr - kInvAddrWork) / 3) * 18;
            return;
        }
        if (addr == kInvAddrOutput && workbench) {
            x = 123;
            y = 34;
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
        void Chrome(SceneBuilder& b, bool workbench, bool horse = false, int chestRows = 0, int furnace = -1) {
            if (furnace >= 0) {
                AddSprite(b, furnace == 1 ? "ui_blast_furnace.png" : furnace == 2 ? "ui_smoker.png" : "ui_furnace.png",
                          0, 0, 176, 166, 0, 0);
                return;
            }
            if (chestRows > 0) {
                const int top = 17 + chestRows * 18;
                AddSprite(b, "ui_chest.png", 0, 0, 176, top, 0, 0);
                AddSprite(b, "ui_chest.png", 0, 126, 176, 96, 0, top);
                return;
            }
            if (horse) {
                AddSprite(b, "ui_horse.png", 0, 0, 176, 166, 0, 0);
                return;
            }
            AddSprite(b, workbench ? "ui_crafting_table.png" : "ui_inventory.png", 0, 0, 176, 166, 0, 0);
        }

        void Cell(SceneBuilder& b, const std::string& id, int px, int py, int addr, const HudItem& item,
                  bool workbench, int chestRows = 0) {
            int x, y;
            InventorySlotPos(addr, x, y, workbench, chestRows);
            BeginSlot(b, id, px + x, py + y);
            if (addr == kInvAddrHorse || addr == kInvAddrHorse + 1) {
                AddSprite(b, "ui_horse_slot.png", 0, 0, 18, 18, 0, 0);
                if (item.Empty())
                    AddSprite(b, "ui_horse_icons.png", (addr - kInvAddrHorse) * 16, 0, 16, 16, 1, 1);
            }
            if (item.Empty() &&
                ((addr >= kInvAddrArmor && addr < kInvAddrOffhand) || addr == kInvAddrOffhand)) {
                const int icon = addr == kInvAddrOffhand ? 4 : addr - kInvAddrArmor;
                AddSprite(b, "ui_slot_icons.png", icon * 16, 0, 16, 16, 1, 1);
            }
            AddSlotItem(b, item, -1, 10);
            EndSlot(b);
        }

        void Label(SceneBuilder& b, int x, int y, const std::string& text, const std::string& z) {
            kit::Label(b, static_cast<float>(x), static_cast<float>(y), 0, "label", kit::Plain(text), z);
        }
    }

    void BeginSlot(SceneBuilder& b, const std::string& id, int x, int y) {
        kit::Begin(b, static_cast<float>(x), static_cast<float>(y), 18, 18, "slot", id, "2");
    }

    void AddSlotItem(SceneBuilder& b, const HudItem& item, int countX, int countY) {
        if (item.Empty())
            return;
        SceneNode& node = kit::Item(b, 1, 1, 16, 16, item.Key(), "4");
        if (item.enchanted)
            node.glint = "1";
        if (item.count > 1)
            kit::Label(b, static_cast<float>(countX), static_cast<float>(countY), 19, "cnt",
                       kit::Plain(std::to_string(item.count)), "6");
    }

    void EndSlot(SceneBuilder& b) {
        kit::Box(b, 1, 1, 16, 16, "hl", {}, "5");
        b.End();
    }

    void AddEffectsPanel(SceneBuilder& b, const std::vector<HudEffect>& effects, int x, int y, int guiW) {
        if (effects.empty())
            return;
        const int space = guiW - x;
        if (space < 32)
            return;
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
            kit::Begin(b, static_cast<float>(x), static_cast<float>(ey), static_cast<float>(w), 32, "fx", {}, "2");
            SceneNode& back = kit::Image(b, 0, 0, static_cast<float>(w), 32,
                                         std::string("mcres:") + (e.ambient ? "ui_inv_effect_background_ambient.png"
                                                                            : "ui_inv_effect_background.png"),
                                         "1");
            back.nine = "4 4 4 4";
            kit::Image(b, 7, 7, 18, 18, "mcres:effect_" + std::string(info.texture) + ".png", "3");
            if (wide) {
                kit::Label(b, 28, 6, 0, "fxn", kit::Plain(EffectLabel(e)), "4");
                kit::Label(b, 28, 16, 0, "fxd", kit::Plain(EffectDuration(e)), "4");
            }
            b.End();
        }
    }

    SceneDoc BuildInventoryScreen(const InventoryView& v, int guiW, int guiH) {
        const int px = InventoryPanelX(v, guiW), py = (guiH - InventoryPanelH(v)) / 2;
        const bool wb = v.workbench;
        const int rows = v.chestRows;
        SceneDoc doc;
        SceneBuilder slots(doc.scene.lists["slots"]);
        const int labelX = px + (v.furnace >= 0 ? 88 - static_cast<int>(v.title.size()) * 3
                                 : v.horse || rows > 0 ? 8 : wb ? 28 : 97);
        const int labelY = py + (v.furnace >= 0 || v.horse || rows > 0 ? 6 : wb ? 6 : 8);
        kit::Label(slots, static_cast<float>(labelX), static_cast<float>(labelY), 0, "label",
                   kit::Formatted(v.title.empty() ? i18n::Tr(v.horse ? "entity.minecraft.horse" : "container.crafting")
                                                  : v.title),
                   "3");
        if (v.furnace >= 0) {
            for (int i = 0; i < 3; i++)
                Cell(slots, "k" + std::to_string(i), px, py, kInvAddrChest + i, v.chestItems[static_cast<size_t>(i)],
                     false, -1);
            const int flame = static_cast<int>(std::ceil(v.burn * 13.0f));
            if (flame > 0) {
                kit::Begin(slots, static_cast<float>(px + 56), static_cast<float>(py + 36 + 14 - flame), 14,
                           static_cast<float>(flame), {}, {}, "3");
                AddSprite(slots, "ui_furnace_progress.png", 0, 14 - flame, 14, flame, 0, 0);
                slots.End();
            }
            const int arrow = static_cast<int>(std::floor(v.cook * 24.0f));
            if (arrow > 0) {
                kit::Begin(slots, static_cast<float>(px + 79), static_cast<float>(py + 34), static_cast<float>(arrow), 16,
                           {}, {}, "3");
                AddSprite(slots, "ui_furnace_progress.png", 0, 14, arrow, 16, 0, 0);
                slots.End();
            }
            Label(slots, px + 8, py + 72, i18n::Tr("container.inventory"), "3");
        } else if (rows > 0) {
            for (int i = 0; i < rows * 9; i++)
                Cell(slots, "k" + std::to_string(i), px, py, kInvAddrChest + i, v.chestItems[static_cast<size_t>(i)],
                     false, rows);
            Label(slots, px + 8, py + rows * 18 + 20, i18n::Tr("container.inventory"), "3");
        } else if (v.horse) {
            Cell(slots, "h0", px, py, kInvAddrHorse, v.horseItems[0], false);
            Cell(slots, "h1", px, py, kInvAddrHorse + 1, v.horseItems[1], false);
            if (v.horseChest > 0) {
                kit::Begin(slots, static_cast<float>(px + 79), static_cast<float>(py + 17), 90, 54, {}, {}, "1");
                AddSprite(slots, "ui_horse_chest.png", 0, 0, 90, 54, 0, 0);
                slots.End();
                for (int i = 0; i < std::min(v.horseChest, 15); i++)
                    Cell(slots, "h" + std::to_string(i + 2), px, py, kInvAddrHorse + 2 + i,
                         v.horseItems[static_cast<size_t>(i + 2)], false);
            }
            Label(slots, px + 8, py + 72, i18n::Tr("container.inventory"), "3");
        } else if (!wb) {
            for (int i = 0; i < 4; i++)
                Cell(slots, "a" + std::to_string(i), px, py, kInvAddrArmor + i, v.armor[static_cast<size_t>(i)], wb);
            Cell(slots, "off", px, py, kInvAddrOffhand, v.offhand, wb);
            for (int i = 0; i < 4; i++)
                Cell(slots, "c" + std::to_string(i), px, py, kInvAddrCraft + i, v.craft[static_cast<size_t>(i)], wb);
        } else {
            for (int i = 0; i < 9; i++)
                Cell(slots, "w" + std::to_string(i), px, py, kInvAddrWork + i, v.work[static_cast<size_t>(i)], wb);
        }
        if (!v.horse && rows == 0 && v.furnace < 0)
            Cell(slots, "co", px, py, kInvAddrOutput, v.output, wb);
        for (int i = 0; i < 36; i++)
            Cell(slots, "s" + std::to_string(i), px, py, i, v.slots[static_cast<size_t>(i)], wb, rows);

        SceneBuilder chrome(doc.scene.lists["chrome"]);
        Chrome(chrome, wb, v.horse, rows, v.furnace);

        const int bx = (guiW - 147) / 2 - 86;
        SceneBuilder book(doc.scene.lists["book"]);
        if (v.bookOpen) {
            kit::Begin(book, static_cast<float>(bx), static_cast<float>(py), 147, 166, {}, "bookpanel", "2");
            AddSprite(book, "ui_recipe_book.png", 0, 0, 147, 166, 0, 0);
            book.End();
            for (size_t n = 0; n < v.recipes.size(); n++) {
                const RecipeCard& c = v.recipes[n];
                const int cx = bx + 11 + static_cast<int>(n % kBookCols) * 25,
                          cy = py + 31 + static_cast<int>(n / kBookCols) * 25;
                kit::Begin(book, static_cast<float>(cx), static_cast<float>(cy), 25, 25, "rc", "r" + std::to_string(n),
                           "3");
                AddSprite(book, "ui_recipe_slots.png", c.craftable ? 0 : 25, 0, 25, 25, 0, 0);
                kit::Item(book, 4, 4, 16, 16, c.out.Key(), "4");
                if (c.out.count > 1)
                    kit::Label(book, 3, 13, 19, "cnt", kit::Plain(std::to_string(c.out.count)), "6");
                book.End();
            }
            const auto page = [&](const char* id, int x, int rest, int hover) {
                kit::Begin(book, static_cast<float>(x), static_cast<float>(py + 137), 12, 17, "pg", id, "6");
                AddSprite(book, "ui_recipe_pages.png", rest, 0, 12, 17, 0, 0);
                kit::Begin(book, 0, 0, 0, 0, "ph");
                AddSprite(book, "ui_recipe_pages.png", hover, 0, 12, 17, 0, 0);
                book.End();
                book.End();
            };
            if (v.bookPage > 0)
                page("rp", bx + 38, 0, 12);
            if (v.bookPage + 1 < v.bookPages)
                page("rn", bx + 93, 24, 36);
            Label(book, bx + 54, py + 141,
                  std::to_string(v.bookPage + 1) + "/" + std::to_string(std::max(1, v.bookPages)), "6");
        }
        const int btnX = wb ? px + 5 : px + 104, btnY = wb ? py + 34 : py + 61;
        SceneBuilder bookButton(doc.scene.lists["bookbutton"]);
        if (!(v.horse || rows > 0)) {
            kit::Begin(bookButton, static_cast<float>(btnX), static_cast<float>(btnY), 20, 18, "rbtn", "rb", "6");
            kit::Begin(bookButton, 0, 0, 0, 0, "rn");
            AddSprite(bookButton, "ui_recipe_button.png", 0, 0, 20, 18, 0, 0);
            bookButton.End();
            kit::Begin(bookButton, 0, 0, 0, 0, "rh");
            AddSprite(bookButton, "ui_recipe_button.png", 0, 18, 20, 18, 0, 0);
            bookButton.End();
            bookButton.End();
        }

        SceneBuilder effects(doc.scene.lists["effects"]);
        AddEffectsPanel(effects, v.effects, px + kInvPanelW + 2, py, guiW);
        SceneBuilder cursor(doc.scene.lists["cursor"]);
        AddInventoryCursor(cursor, v);
        SceneBuilder tooltip(doc.scene.lists["tooltip"]);
        AddInventoryTooltip(tooltip, v, guiW, guiH);

        doc.scene.theme = Theme::Inventory;
        auto& L = doc.scene.lists;
        auto& out = doc.scene.nodes;
        {
            SceneBuilder sb(out);
            kit::Rect(sb, 0, 0, static_cast<float>(guiW), static_cast<float>(guiH), "#101010c8", "auto", {}, "dim");
        }
        kit::Append(out, L["book"]);
        kit::Append(out, L["effects"]);
        kit::Container(out, "panel", static_cast<float>(px), static_cast<float>(py), 176, 166, "auto",
                       std::move(L["chrome"]));
        kit::Append(out, L["bookbutton"]);
        kit::Append(out, L["slots"]);
        kit::TopLayer(out, static_cast<float>(guiW), static_cast<float>(guiH), "50", L["cursor"], L["tooltip"]);
        L.clear();
        return doc;
    }

    void AddInventoryCursor(SceneBuilder& b, const InventoryView& v) {
        if (v.cursor.Empty())
            return;
        kit::Begin(b, static_cast<float>(static_cast<int>(v.mouseX) - 8), static_cast<float>(static_cast<int>(v.mouseY) - 8),
                   16, 16, {}, "cursor", "30");
        SceneNode& item = kit::Item(b, 0, 0, 16, 16, v.cursor.Key());
        if (v.cursor.enchanted)
            item.glint = "1";
        if (v.cursor.count > 1)
            kit::Label(b, -3, 9, 19, "cnt", kit::Plain(std::to_string(v.cursor.count)), "31");
        b.End();
    }

    void AddInventoryTooltip(SceneBuilder& b, const InventoryView& v, int guiW, int guiH) {
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
                AddTooltip(b, BuildItemTooltip(*it, v.advancedTooltips), static_cast<int>(v.mouseX),
                           static_cast<int>(v.mouseY), guiW, guiH);
        }
    }
}
