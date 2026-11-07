#include "client/PerlinInternal.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/Perlin.hpp"

#include "client/i18n/Lang.hpp"
#include "client/input/Keys.hpp"
#include "client/ui/CreativeScreen.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/ItemData.hpp"
#include "client/PreviewWorld.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/UiTemplate.hpp"
#include "util/Env.hpp"
#include "util/Paths.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {

    namespace {
        using Clock = std::chrono::steady_clock;

        ui::HudItem Convert(const BedrockSession::ItemStackInfo& info) {
            ui::HudItem item;
            if (info.Empty())
                return item;
            item.name = info.name;
            item.count = info.count;
            item.blockRuntimeId = info.blockRuntimeId;
            item.enchanted = info.enchanted;
            item.customName = info.customName;
            item.hasColor = info.hasColor;
            item.color = info.color;
            item.nbtTags = info.nbtTags;
            item.enchants = info.enchants;
            item.lore = info.lore;
            item.maxDamage = ui::MaxDurability(info.name);
            item.aux = item.maxDamage > 0 ? std::clamp(info.damage, 0, item.maxDamage) : info.aux;
            return item;
        }

        std::string Translate(const char* key, const char* fallback) {
            std::string s = i18n::Tr(key);
            return s == key ? std::string(fallback) : s;
        }
    }

    void Perlin::UpdateCreativeScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                                      int fbH) {
        const auto now = Clock::now();
        static BedrockSession::CreativeCatalog catalog;
        static uint32_t catalogVersion = ~0u;
        if (live && live->CreativeVersion() != catalogVersion) {
            catalog = live->Creative();
            catalogVersion = live->CreativeVersion();
            creativeScreen.signature.clear();
        } else if (!live && catalog.items.empty()) {
            const char* names[4][6] = {{"minecraft:brick_block", "minecraft:stone", "minecraft:oak_planks",
                                        "minecraft:glass", "minecraft:cobblestone", "minecraft:sandstone"},
                                       {"minecraft:grass_block", "minecraft:dirt", "minecraft:oak_log",
                                        "minecraft:oak_leaves", "minecraft:sand", "minecraft:gravel"},
                                       {"minecraft:diamond_sword", "minecraft:iron_pickaxe", "minecraft:bow",
                                        "minecraft:iron_helmet", "minecraft:shield", "minecraft:elytra"},
                                       {"minecraft:apple", "minecraft:bucket", "minecraft:diamond",
                                        "minecraft:stick", "minecraft:torch", "minecraft:redstone"}};
            for (int g = 0; g < 4; g++) {
                BedrockSession::CreativeGroup group;
                group.category = g + 1;
                catalog.groups.push_back(group);
                for (int rep = 0; rep < 12; rep++)
                    for (int k = 0; k < 6; k++) {
                        BedrockSession::CreativeItem item;
                        item.entryId = static_cast<uint32_t>(1 + g * 100 + rep * 6 + k);
                        item.info.name = names[g][k];
                        item.info.count = 1;
                        item.group = g;
                        catalog.items.push_back(item);
                    }
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_ICONS")) {
                catalog = {};
                BedrockSession::CreativeGroup group;
                group.category = 1;
                catalog.groups.push_back(group);
                uint32_t id = 1;
                std::string only;
                if (const char* list = PERLIN_ENV("PERLIN_PREVIEW_ONLY"))
                    only = std::string(",") + list + ",";
                for (const auto& [hash, name] : PreviewBlockList()) {
                    if (!only.empty() && !only.contains("," + name.substr(10) + ","))
                        continue;
                    BedrockSession::CreativeItem item;
                    item.entryId = id++;
                    item.info.name = name;
                    item.info.count = 1;
                    item.info.blockRuntimeId = static_cast<uint32_t>(BlockPalette::Instance().RuntimeId(
                        BlockPalette::Instance().Intern(static_cast<int32_t>(hash))));
                    item.group = 0;
                    catalog.items.push_back(item);
                }
            }
            catalogVersion = 0;
        }

        if (!live)
            if (const char* forced = PERLIN_ENV("PERLIN_PREVIEW_CREATIVETAB"))
                creativeScreen.tab = std::atoi(forced);
        std::array<bool, ui::kCreativeTabs> present{};
        std::array<ui::HudItem, ui::kCreativeTabs> icons;
        for (const auto& g : catalog.groups) {
            const int tab = g.category - 1;
            if (tab >= 0 && tab < 4) {
                present[static_cast<size_t>(tab)] = true;
                if (icons[static_cast<size_t>(tab)].Empty())
                    icons[static_cast<size_t>(tab)] = Convert(g.icon);
            }
        }
        for (const auto& c : catalog.items) {
            if (c.group < 0 || static_cast<size_t>(c.group) >= catalog.groups.size())
                continue;
            const int tab = catalog.groups[static_cast<size_t>(c.group)].category - 1;
            if (tab >= 0 && tab < 4) {
                present[static_cast<size_t>(tab)] = true;
                if (icons[static_cast<size_t>(tab)].Empty())
                    icons[static_cast<size_t>(tab)] = Convert(c.info);
            }
        }
        present[ui::kCreativeSearchTab] = true;
        present[ui::kCreativeSurvivalTab] = true;
        if (!present[static_cast<size_t>(std::clamp(creativeScreen.tab, 0, ui::kCreativeTabs - 1))])
            creativeScreen.tab = ui::kCreativeSearchTab;
        creativeScreen.tab = std::clamp(creativeScreen.tab, 0, ui::kCreativeTabs - 1);

        std::vector<const BedrockSession::CreativeItem*> shown;
        if (creativeScreen.tab < 4) {
            for (const auto& c : catalog.items)
                if (c.group >= 0 && static_cast<size_t>(c.group) < catalog.groups.size() &&
                    catalog.groups[static_cast<size_t>(c.group)].category == creativeScreen.tab + 1)
                    shown.push_back(&c);
        } else if (creativeScreen.tab == ui::kCreativeSearchTab) {
            const std::string needle = str::Lower(creativeScreen.search);
            for (const auto& c : catalog.items) {
                if (needle.empty() ||
                    str::Lower(ui::UiAssets::Instance().ItemName(c.info.name, c.info.aux)).contains(needle) ||
                    str::Lower(c.info.name).contains(needle))
                    shown.push_back(&c);
            }
        }
        const int rows = static_cast<int>((shown.size() + 8) / 9);
        const bool scrollable = rows > 5;
        const int maxRow = std::max(0, rows - 5);
        if (!scrollable)
            creativeScreen.scroll = 0.0f;
        if (scrollable && std::fabs(inputManager.Scroll()) > 0.0f &&
            creativeScreen.tab != ui::kCreativeSurvivalTab) {
            const float step = 1.0f / static_cast<float>(maxRow);
            creativeScreen.scroll =
                std::clamp(creativeScreen.scroll - (inputManager.Scroll() > 0 ? step : -step), 0.0f, 1.0f);
        }
        int firstRow = static_cast<int>(std::lround(creativeScreen.scroll * static_cast<float>(maxRow)));
        if (const char* row = PERLIN_ENV("PERLIN_PREVIEW_ROW"))
            firstRow = std::clamp(std::atoi(row), 0, maxRow);

        creativeScreen.dollVisible = creativeScreen.tab == ui::kCreativeSurvivalTab;
        ui::CreativeView v;
        v.tab = creativeScreen.tab;
        v.tabPresent = present;
        v.tabIcons = icons;
        v.tabTitles[0] = Translate("itemGroup.name.construction", "Construction");
        v.tabTitles[1] = Translate("itemGroup.name.nature", "Nature");
        v.tabTitles[2] = Translate("itemGroup.name.equipment", "Equipment");
        v.tabTitles[3] = Translate("itemGroup.name.items", "Items");
        v.tabTitles[ui::kCreativeSearchTab] = Translate("itemGroup.search", "Search Items");
        v.tabTitles[ui::kCreativeSurvivalTab] = Translate("itemGroup.name.inventory", "Survival Inventory");
        v.title = v.tabTitles[static_cast<size_t>(creativeScreen.tab)];
        v.advancedTooltips = debug.advancedTooltips;
        v.effects = state.effects;
        v.scroll = creativeScreen.scroll;
        v.scrollable = scrollable;
        v.search = creativeScreen.search;
        std::array<uint32_t, 45> entry{};
        for (int i = 0; i < 45; i++) {
            const size_t idx = static_cast<size_t>((firstRow * 9) + i);
            if (idx < shown.size()) {
                v.grid[static_cast<size_t>(i)] = Convert(shown[idx]->info);
                v.grid[static_cast<size_t>(i)].count = 1;
                v.gridNames[static_cast<size_t>(i)] =
                    shown[idx]->info.customName.empty()
                        ? ui::UiAssets::Instance().ItemName(shown[idx]->info.name, shown[idx]->info.aux)
                        : shown[idx]->info.customName;
                entry[static_cast<size_t>(i)] = shown[idx]->entryId;
            }
        }
        if (live) {
            const auto inv = live->GetInventory();
            for (int i = 0; i < 9; i++)
                v.slots[static_cast<size_t>(i)] = Convert(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[static_cast<size_t>(9 + i)] = Convert(inv.main[i]);
            for (int i = 0; i < 4; i++)
                v.armor[static_cast<size_t>(i)] = Convert(inv.armor[i]);
            v.offhand = Convert(inv.offhand);
            v.cursor = Convert(inv.cursor);
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[static_cast<size_t>(i)] = state.hotbar[i];
            for (int i = 0; i < 4; i++)
                v.armor[static_cast<size_t>(i)] = state.armorItems[i];
        }

        if (creativeScreen.tmpl.Refresh("creative_screen.rml", ui::DefaultCreativeScreenTemplate(),
                                        std::chrono::milliseconds(2000), now))
            creativeScreen.signature.clear();

        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        inventory.mouseX = gx;
        inventory.mouseY = gy;
        std::string hoverId;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            hoverId = hud.Overlay().OverlayHover();
        }
        inventory.hoverAddr = ui::InventoryAddrFromId(hoverId);
        if (hoverId == "del" || (!hoverId.empty() && hoverId[0] == 'g'))
            inventory.hoverAddr = -1;
        v.hovered = hoverId;
        v.mouseX = gx;
        v.mouseY = gy;

        const bool left = input.IsMouseDown(Mouse::Left), right = input.IsMouseDown(Mouse::Right);
        const bool fresh = now - inventory.openedAt > std::chrono::milliseconds(150);
        const int px = (guiW - ui::kCreativeW) / 2, py = (guiH - ui::kCreativeH) / 2;
        const bool leftEdge = left && !inventory.leftDown, rightEdge = right && !inventory.rightDown;
        if (live && fresh && (leftEdge || rightEdge)) {
            int clickedTab = -1;
            if (leftEdge)
                for (int t = 0; t < ui::kCreativeTabs; t++) {
                    if (!present[static_cast<size_t>(t)])
                        continue;
                    int tx, ty;
                    bool top;
                    ui::CreativeTabPos(t, tx, ty, top);
                    const float x0 = static_cast<float>(px + tx),
                                y0 = static_cast<float>(py + (top ? ty : ty + 4));
                    const float y1 = top ? static_cast<float>(py) : y0 + 28.0f;
                    if (gx >= x0 && gx < x0 + 26.0f && gy >= y0 && gy < y1) {
                        clickedTab = t;
                        break;
                    }
                }
            if (clickedTab >= 0) {
                if (clickedTab != creativeScreen.tab) {
                    creativeScreen.tab = clickedTab;
                    creativeScreen.scroll = 0.0f;
                    creativeScreen.signature.clear();
                }
            } else if (hoverId == "sbar" && leftEdge && scrollable) {
                creativeScreen.dragging = true;
            } else if (!hoverId.empty() && hoverId[0] == 'g') {
                const int n = std::atoi(hoverId.c_str() + 1);
                if (n >= 0 && n < 45) {
                    if (!v.cursor.Empty())
                        live->InvDestroyCursor();
                    else if (entry[static_cast<size_t>(n)] != 0)
                        live->InvCreativeTake(entry[static_cast<size_t>(n)],
                                              input.IsKeyDown(Key::Shift) ? 2 : (rightEdge ? 1 : 0));
                }
            } else if (hoverId == "del") {
                live->InvDestroyCursor();
            } else if (inventory.hoverAddr >= 0) {
                if (DispatchModSlotClick(inventory.hoverAddr, rightEdge, input.IsKeyDown(Key::Shift))) {
                } else if (input.IsKeyDown(Key::Shift))
                    live->InvShiftClick(inventory.hoverAddr);
                else if (!v.cursor.Empty() && inventory.hoverAddr != ui::kInvAddrOutput &&
                         inventory.dragButton == 0) {
                    inventory.dragButton = leftEdge ? 1 : 2;
                    inventory.dragSlots.assign(1, inventory.hoverAddr);
                } else
                    live->InvClick(inventory.hoverAddr, rightEdge);
            } else if (v.cursor.Empty() == false && (gx < px - 30 || gx > px + ui::kCreativeW + 30 ||
                                                     gy < py - 30 || gy > py + ui::kCreativeH + 30)) {
                live->InvDropCursor(!rightEdge);
            }
        }
        bool needRefresh = leftEdge || rightEdge || (!left && inventory.dragButton != 0);
        const bool middle = input.IsMouseDown(Mouse::Middle);
        const bool middleEdge = middle && !inventory.middleDown;
        if (live && fresh) {
            const bool slotTarget = inventory.hoverAddr >= 0 && inventory.hoverAddr != ui::kInvAddrOutput;
            if (middleEdge) {
                if (!hoverId.empty() && hoverId[0] == 'g') {
                    const int n = std::atoi(hoverId.c_str() + 1);
                    if (n >= 0 && n < 45 && entry[static_cast<size_t>(n)] != 0 && v.cursor.Empty())
                        live->InvCreativeTake(entry[static_cast<size_t>(n)], 0);
                } else if (slotTarget) {
                    if (v.cursor.Empty())
                        live->InvCreativeClone(inventory.hoverAddr);
                    else
                        live->InvCreativePlace(inventory.hoverAddr);
                    inventory.dragSlots.assign(1, inventory.hoverAddr);
                }
            } else if (middle && slotTarget && !v.cursor.Empty() &&
                       std::find(inventory.dragSlots.begin(), inventory.dragSlots.end(),
                                 inventory.hoverAddr) == inventory.dragSlots.end()) {
                inventory.dragSlots.push_back(inventory.hoverAddr);
                live->InvCreativePlace(inventory.hoverAddr);
            }
            if (!middle && inventory.dragButton == 0)
                inventory.dragSlots.clear();
            if (inventory.dragButton != 0) {
                const bool held = inventory.dragButton == 1 ? left : right;
                if (held) {
                    if (slotTarget && std::find(inventory.dragSlots.begin(), inventory.dragSlots.end(),
                                                inventory.hoverAddr) == inventory.dragSlots.end())
                        inventory.dragSlots.push_back(inventory.hoverAddr);
                } else {
                    if (inventory.dragSlots.size() >= 2)
                        live->InvSpread(inventory.dragSlots, inventory.dragButton == 2 ? 1 : 0);
                    else if (inventory.dragSlots.size() == 1)
                        live->InvClick(inventory.dragSlots[0], inventory.dragButton == 2);
                    inventory.dragButton = 0;
                    inventory.dragSlots.clear();
                }
            }
            if (middleEdge || middle)
                needRefresh = true;
        }
        inventory.middleDown = middle;
        if (!left)
            creativeScreen.dragging = false;
        if (creativeScreen.dragging && scrollable) {
            const float t = std::clamp((gy - static_cast<float>(py + 18) - 7.5f) / 97.0f, 0.0f, 1.0f);
            creativeScreen.scroll = t;
        }
        inventory.leftDown = left;
        inventory.rightDown = right;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + hoverId + "|" +
                                std::to_string(creativeScreen.tab) + "|" +
                                std::to_string(static_cast<int>(creativeScreen.scroll * 1000)) + "|" +
                                creativeScreen.search + "|" + std::to_string(catalogVersion);
        for (const auto& e : v.effects)
            signature += std::to_string(e.id) + ":" + std::to_string(e.amplifier) + ":" +
                         std::to_string(e.ticks / 20) + ";";
        const auto sig = [&](const ui::HudItem& it) {
            signature += it.name + ":" + std::to_string(it.count) + ":" + std::to_string(it.aux) +
                         (it.enchanted ? "g" : "") + ";";
        };
        for (const auto& it : v.slots)
            sig(it);
        for (const auto& it : v.armor)
            sig(it);
        sig(v.offhand);
        sig(v.cursor);
        if (signature != creativeScreen.signature || !hud.Overlay().HasOverlay()) {
            creativeScreen.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildCreativeScreenRml(creativeScreen.tmpl.text, v, guiW, guiH),
                                          error)) {
                Log("ui", "creative_screen.rml: {}", error);
                creativeScreen.tmpl.text = ui::DefaultCreativeScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildCreativeScreenRml(creativeScreen.tmpl.text, v, guiW, guiH),
                                         error);
            }
            hud.Overlay().MouseMove(gx, gy);
        }
        if (needRefresh)
            creativeScreen.signature.clear();
        hud.Overlay().MoveOverlayElement("cursor", static_cast<int>(gx) - 8, static_cast<int>(gy) - 8);
        {
            int tx, ty;
            ui::PlaceTooltip(static_cast<int>(gx), static_cast<int>(gy), guiW, guiH, tx, ty);
            hud.Overlay().MoveOverlayElement("tip", tx, ty);
        }
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
    }
}
