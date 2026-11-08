#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::OpenInventory(TimePoint now) {
        inventory.shown = true;
        inventory.openedAt = now;
        inventory.picked = -1;
        inventory.signature.clear();
        inputManager.TakeTyped();
        input.SetTextMode(true);
        CenterCursorForGui();
    }

    void Perlin::UpdateInventoryScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                                       int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_INVENTORY") != nullptr;
        const auto now = Clock::now();
        if (live)
            live->InvPump();
        const bool canOpen = (live || preview) && !death.shown && !form.shown && !pause.shown &&
                             !chat.IsOpen() && state.mode != ui::GameMode::Spectator;
        const bool workbenchOpened = live && live->TakeWorkbenchOpened();
        const bool horseOpened = live && live->TakeHorseOpened();
        const bool chestOpened = live && live->TakeChestOpened();
        if (!inventory.shown && (workbenchOpened || horseOpened || chestOpened) && !death.shown) {
            if (chestOpened) {
                inventory.chestUi.Warm(ServerPacks::Instance().Directories());
                inventory.chestUi.WaitReady(150);
            }
            OpenInventory(now);
        }
        if (!inventory.shown && canOpen &&
            (inputManager.Pressed(Action::Inventory) || (preview && !inventory.previewOpened))) {
            inventory.previewOpened = true;
            OpenInventory(now);
            if (live) {

                if (live->RidingUid() != 0 && live->OpenMountInventory()) {
                    inventory.mountAsk = now;
                } else {
                    live->OpenInventory();
                    inventory.mountAsk = {};
                }
            }
        }
        if (inventory.shown && live && inventory.mountAsk != TimePoint{} &&
            now - inventory.mountAsk > std::chrono::milliseconds(450)) {
            inventory.mountAsk = {};
            if (live->RidingUid() != 0 && !live->HorseOpen())
                live->OpenInventory();
        }
        if (!inventory.shown)
            return;
        bool close = false;
        inventory.workbench =
            live ? live->WorkbenchOpen() : (preview && PERLIN_ENV("PERLIN_PREVIEW_WORKBENCH") != nullptr);
        inventory.horse = live && live->HorseOpen();
        const char* previewChest = preview ? PERLIN_ENV("PERLIN_PREVIEW_CHEST") : nullptr;
        inventory.chest = (live && live->ChestOpen()) || previewChest;
        if (preview && !inventory.bookInit && PERLIN_ENV("PERLIN_PREVIEW_BOOK")) {
            inventory.bookInit = true;
            inventory.book = true;
        }
        const bool creativeMode = (live || preview) && state.mode == ui::GameMode::Creative &&
                                  !inventory.workbench && !inventory.horse && !inventory.chest;
        const bool searching = creativeMode && creativeScreen.tab == ui::kCreativeSearchTab;
        if (death.shown || (live && live->TakeServerClosedInventory()))
            close = true;
        if (inventory.closeClicked) {
            inventory.closeClicked = false;
            close = true;
        }
        const auto typedIs = [&](const InputManager::Typed& t, Action a) {
            const Binding& b = options.GetKey(a);
            return t.character && b.device == Binding::Device::Keyboard && std::toupper(t.key) == b.code;
        };
        for (const auto& t : inputManager.TakeTyped()) {
            if (now - inventory.openedAt < std::chrono::milliseconds(150))
                continue;
            if (!t.character && t.key == Key::Escape)
                close = true;
            else if (searching &&
                     ui::TextEdit::Focused().Apply(t, creativeScreen.search, ui::TextLimits{50})) {
            } else if (typedIs(t, Action::Inventory) && !searching)
                close = true;
            else if (typedIs(t, Action::Drop) && live && inventory.hoverAddr >= 0)
                live->InvDropSlot(inventory.hoverAddr, t.ctrl);
            else if (typedIs(t, Action::SwapOffhand) && live && inventory.hoverAddr >= 0)
                live->InvSwapOffhand(inventory.hoverAddr);
            else if (live && inventory.hoverAddr >= 0) {
                for (int h = 0; h < 9; h++)
                    if (typedIs(t, static_cast<Action>(static_cast<int>(Action::Hotbar1) + h))) {
                        live->InvHotbarKey(inventory.hoverAddr, h);
                        break;
                    }
            }
        }
        if (close) {
            if (live)
                live->CloseInventory();
            hud.Overlay().ClearOverlay();
            inventory.shown = false;
            inventory.hoverId.clear();
            inventory.hover = -1;
            inventory.hoverAddr = -1;
            if (!chat.IsOpen() && !death.shown)
                input.SetTextMode(false);
            return;
        }

        if (creativeMode || (preview && PERLIN_ENV("PERLIN_PREVIEW_CREATIVE") && !inventory.workbench)) {
            UpdateCreativeScreen(hud, live, state, fbW, fbH);
            return;
        }

        if (PERLIN_ENV("PERLIN_PREVIEW_ADV"))
            debug.advancedTooltips = true;
        ui::InventoryView v;
        v.advancedTooltips = debug.advancedTooltips;
        if (live) {
            const auto inv = live->GetInventory();
            for (int i = 0; i < 9; i++)
                v.slots[i] = detail::ToHudItem(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[9 + i] = detail::ToHudItem(inv.main[i]);
            for (int i = 0; i < 4; i++) {
                v.armor[i] = detail::ToHudItem(inv.armor[i]);
                v.craft[i] = detail::ToHudItem(inv.craft[i]);
            }
            for (int i = 0; i < 9; i++)
                v.work[i] = detail::ToHudItem(inv.work[i]);
            v.horse = inv.horseOpen;
            v.horseChest = inv.horseChestSlots;
            for (int i = 0; i < 17; i++)
                v.horseItems[static_cast<size_t>(i)] = detail::ToHudItem(inv.horse[i]);
            if (inv.furnaceKind >= 0) {
                v.furnace = inv.furnaceKind;
                v.chestRows = -1;
                for (int i = 0; i < 3; i++)
                    v.chestItems[static_cast<size_t>(i)] = detail::ToHudItem(inv.chest[i]);
                const auto progress = live->Furnace();
                v.cook = progress.cook;
                v.burn = progress.burn;
                v.title = i18n::Tr(inv.furnaceKind == 1   ? "container.blast_furnace"
                                   : inv.furnaceKind == 2 ? "container.smoker"
                                                          : "container.furnace");
            } else if (inv.chestOpen) {
                v.chestRows = inv.chestFitRows ? std::clamp((inv.chestSlots + 8) / 9, 1, 6)
                                               : inv.chestSlots > 27 ? 6
                                                                     : 3;
                for (int i = 0; i < 54; i++)
                    v.chestItems[static_cast<size_t>(i)] = detail::ToHudItem(inv.chest[i]);
                v.title =
                    i18n::Tr(v.chestRows == 6 && inv.chestTitle == "container.chest" ? "container.chestDouble"
                                                                                     : inv.chestTitle);
                inventory.chestScreen =
                    ui::ChestScreenName(inv.chestFitRows ? "container.chest" : inv.chestTitle, v.chestRows);
            }
            v.offhand = detail::ToHudItem(inv.offhand);
            v.output = detail::ToHudItem(inv.output);
            v.cursor = detail::ToHudItem(inv.cursor);
            v.selectedHotbar = inv.selected;
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[i] = state.hotbar[i];
            v.slots[9].name = "minecraft:cobblestone";
            v.slots[9].count = 64;
            v.slots[10].name = "minecraft:oak_planks";
            v.slots[10].count = 12;
            v.slots[20].name = "minecraft:diamond";
            v.slots[20].count = 3;
            for (int i = 0; i < 4; i++)
                v.armor[i] = state.armorItems[i];
            v.selectedHotbar = state.selectedSlot;
            if (previewChest) {
                v.chestRows = std::atoi(previewChest) > 27 ? 6 : 3;
                static const char* const kLoot[] = {"minecraft:iron_ingot", "minecraft:bread",
                                                    "minecraft:diamond_sword", "minecraft:oak_log",
                                                    "minecraft:gold_ingot"};
                for (int i = 0; i < v.chestRows * 9; i += 4) {
                    v.chestItems[static_cast<size_t>(i)].name = kLoot[(i / 4) % 5];
                    v.chestItems[static_cast<size_t>(i)].count = i % 3 == 0 ? 1 : 16 + i;
                }
                v.title = i18n::Tr(v.chestRows == 6 ? "container.chestDouble" : "container.chest");
                inventory.chestScreen = ui::ChestScreenName("container.chest", v.chestRows);
            }
        }

        v.effects = state.effects;
        if (inventory.dragButton != 0 && inventory.dragSlots.size() >= 2 && !v.cursor.Empty()) {
            const auto viewItem = [&](int a) -> ui::HudItem* {
                if (a < 36)
                    return &v.slots[static_cast<size_t>(a)];
                if (a < 40)
                    return &v.armor[static_cast<size_t>(a - 36)];
                if (a == 40)
                    return &v.offhand;
                if (a < 45)
                    return &v.craft[static_cast<size_t>(a - 41)];
                if (a >= 47 && a < 56)
                    return &v.work[static_cast<size_t>(a - 47)];
                if (a >= ui::kInvAddrChest && a < ui::kInvAddrChest + 54)
                    return &v.chestItems[static_cast<size_t>(a - ui::kInvAddrChest)];
                return nullptr;
            };
            std::vector<ui::HudItem*> targets;
            for (int a : inventory.dragSlots)
                if (ui::HudItem* it = viewItem(a);
                    it && (it->Empty() || (it->name == v.cursor.name && it->aux == v.cursor.aux)))
                    targets.push_back(it);
            if (targets.size() >= 2) {
                const int total = v.cursor.count;
                const int each =
                    inventory.dragButton == 2 ? 1 : std::max(1, total / static_cast<int>(targets.size()));
                int left = total;
                for (ui::HudItem* it : targets) {
                    const int n = std::min(each, left);
                    if (n <= 0)
                        break;
                    if (it->Empty()) {
                        const ui::HudItem proto = v.cursor;
                        *it = proto;
                        it->count = n;
                    } else
                        it->count += n;
                    left -= n;
                }
                v.cursor.count = left;
                if (left <= 0)
                    v.cursor = ui::HudItem{};
            }
        }
        v.workbench = inventory.workbench;
        v.bookOpen = inventory.book;
        if (inventory.book && now - inventory.recipeAt > std::chrono::milliseconds(300)) {
            inventory.recipeAt = now;
            std::vector<BedrockSession::RecipeEntry> all;
            if (live) {
                all = live->GetRecipes(inventory.workbench ? 3 : 2);
            } else {
                static const char* const kNames[] = {"minecraft:oak_planks",     "minecraft:stick",
                                                     "minecraft:crafting_table", "minecraft:torch",
                                                     "minecraft:wooden_pickaxe", "minecraft:chest",
                                                     "minecraft:furnace",        "minecraft:bread"};
                for (int i = 0; i < 26; i++) {
                    BedrockSession::RecipeEntry e;
                    e.index = i;
                    e.craftable = i % 3 != 0;
                    e.out.name = kNames[i % 8];
                    e.out.count = i % 5 == 0 ? 4 : 1;
                    all.push_back(e);
                }
            }
            std::stable_sort(all.begin(), all.end(),
                             [](const auto& a, const auto& b) { return a.craftable && !b.craftable; });
            inventory.recipeAll = std::move(all);
        }
        if (inventory.book) {
            constexpr int perPage = ui::kBookCols * ui::kBookRows;
            v.bookPages = std::max(1, static_cast<int>((inventory.recipeAll.size() + perPage - 1) / perPage));
            inventory.bookPage = std::clamp(inventory.bookPage, 0, v.bookPages - 1);
            v.bookPage = inventory.bookPage;
            v.recipes.clear();
            for (int n = 0; n < perPage; n++) {
                const size_t at = static_cast<size_t>(inventory.bookPage * perPage + n);
                if (at >= inventory.recipeAll.size())
                    break;
                ui::RecipeCard c;
                c.out = detail::ToHudItem(inventory.recipeAll[at].out);
                c.craftable = inventory.recipeAll[at].craftable;
                c.index = inventory.recipeAll[at].index;
                v.recipes.push_back(std::move(c));
            }
        }

        if (inventory.tmpl.Refresh("inventory_screen.rml", ui::DefaultInventoryScreenTemplate(),
                                   std::chrono::milliseconds(2000), now, "inventory screen"))
            inventory.signature.clear();

        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        inventory.mouseX = gx;
        inventory.mouseY = gy;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            inventory.hoverId = hud.Overlay().OverlayHover();
            inventory.hoverAddr = ui::InventoryAddrFromId(inventory.hoverId);
        }
        v.hovered = inventory.hoverAddr;
        v.hoveredId = inventory.hoverId;
        v.mouseX = gx;
        v.mouseY = gy;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" +
                                std::to_string(inventory.hoverAddr) + inventory.hoverId +
                                (inventory.book ? "B" : "b") + std::to_string(inventory.bookPage) +
                                (inventory.workbench ? "W" : "w") + std::to_string(v.recipes.size());
        for (const auto& e : v.effects)
            signature += std::to_string(e.id) + ":" + std::to_string(e.amplifier) + ":" +
                         std::to_string(e.ticks / 20) + ";";
        const auto sig = [&](const ui::HudItem& it) {
            signature += it.name + ":" + std::to_string(it.count) + ":" + std::to_string(it.aux) +
                         (it.enchanted ? "g" : "") + std::to_string(it.enchants.size()) +
                         (it.hasColor ? std::to_string(it.color) : "") + ";";
        };
        for (const auto& it : v.slots)
            sig(it);
        for (const auto& it : v.armor)
            sig(it);
        for (const auto& it : v.craft)
            sig(it);
        for (const auto& it : v.work)
            sig(it);
        if (v.chestRows) {
            inventory.chestUi.Warm(ServerPacks::Instance().Directories());
            signature +=
                "K" + std::to_string(v.chestRows) + v.title + (inventory.chestUi.Ready() ? "J" : "j");
            for (const auto& it : v.chestItems)
                sig(it);
        }
        if (v.furnace >= 0)
            signature += "F" + std::to_string(v.furnace) + ":" + std::to_string(static_cast<int>(v.cook * 24)) + ":" +
                         std::to_string(static_cast<int>(v.burn * 13));
        if (v.horse) {
            signature += "H" + std::to_string(v.horseChest);
            for (const auto& it : v.horseItems)
                sig(it);
        }
        for (const auto& c : v.recipes) {
            signature += c.craftable ? "c" : "n";
            sig(c.out);
        }
        sig(v.offhand);
        sig(v.output);
        sig(v.cursor);
        if (signature != inventory.signature || !hud.Overlay().HasOverlay()) {
            inventory.signature = signature;
            std::string error;
            std::string chestRml;
            inventory.jsonPanel = v.chestRows > 0 && inventory.chestUi.Build(v, inventory.chestScreen, guiW,
                                                                             guiH, chestRml, inventory.panel);
            if (v.chestRows > 0 && !inventory.jsonPanel &&
                inventory.chestUi.Problem() != inventory.chestProblem) {
                inventory.chestProblem = inventory.chestUi.Problem();
                if (!inventory.chestProblem.empty())
                    Log("chest", "json ui: {}, using the built-in layout", inventory.chestProblem);
            }
            if (inventory.jsonPanel && !hud.Overlay().SetOverlay(chestRml, error)) {
                Log("chest", "{}, using the built-in layout", error);
                inventory.jsonPanel = false;
            }
            if (!inventory.jsonPanel &&
                !hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(inventory.tmpl.text, v, guiW, guiH),
                                          error)) {
                Log("ui", "inventory_screen.rml: {}", error);
                inventory.tmpl.text = ui::DefaultInventoryScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(inventory.tmpl.text, v, guiW, guiH),
                                         error);
            }
            hud.Overlay().MouseMove(gx, gy);
        }
        hud.Overlay().MoveOverlayElement("cursor", static_cast<int>(gx) - 8, static_cast<int>(gy) - 8);
        {
            int tx, ty;
            ui::PlaceTooltip(static_cast<int>(gx), static_cast<int>(gy), guiW, guiH, tx, ty);
            hud.Overlay().MoveOverlayElement("tip", tx, ty);
        }
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool left = input.IsMouseDown(Mouse::Left), right = input.IsMouseDown(Mouse::Right);
        const bool fresh = now - inventory.openedAt > std::chrono::milliseconds(150);
        const bool leftEdge = left && !inventory.leftDown, rightEdge = right && !inventory.rightDown;
        if (live && fresh) {
            const bool slotTarget = inventory.hoverAddr >= 0 && inventory.hoverAddr != ui::kInvAddrOutput;
            if (inventory.dragButton == 0 && (leftEdge || rightEdge) && !v.cursor.Empty() && slotTarget &&
                !input.IsKeyDown(Key::Shift)) {
                inventory.dragButton = leftEdge ? 1 : 2;
                inventory.dragSlots.assign(1, inventory.hoverAddr);
                inventory.signature.clear();
            } else if (leftEdge || rightEdge) {
                const int px = ui::InventoryPanelX(v, guiW), panelH = ui::InventoryPanelH(v),
                          py = (guiH - panelH) / 2;
                const bool outside = inventory.jsonPanel
                                         ? gx < inventory.panel[0] || gy < inventory.panel[1] ||
                                               gx > inventory.panel[0] + inventory.panel[2] ||
                                               gy > inventory.panel[1] + inventory.panel[3]
                                         : gx < px || gy < py || gx > px + ui::kInvPanelW || gy > py + panelH;
                const bool isRight = !leftEdge;
                const bool overBook =
                    inventory.book && gx >= (guiW - 147) / 2 - 86 && gx < (guiW - 147) / 2 + 61;
                if (inventory.hoverId == "close") {
                    inventory.closeClicked = true;
                } else if (inventory.hoverId == "rb") {
                    inventory.book = !inventory.book;
                    inventory.recipeAt = {};
                } else if (inventory.hoverId == "rp") {
                    inventory.bookPage--;
                } else if (inventory.hoverId == "rn") {
                    inventory.bookPage++;
                } else if (inventory.book && inventory.hoverId.size() > 1 && inventory.hoverId[0] == 'r' &&
                           std::isdigit(static_cast<unsigned char>(inventory.hoverId[1]))) {
                    const size_t n = static_cast<size_t>(std::atoi(inventory.hoverId.c_str() + 1));
                    if (n < v.recipes.size())
                        live->InvFillRecipe(v.recipes[n].index);
                } else if (inventory.hoverAddr >= 0) {
                    if (DispatchModSlotClick(inventory.hoverAddr, isRight, input.IsKeyDown(Key::Shift))) {
                    } else if (input.IsKeyDown(Key::Shift))
                        live->InvShiftClick(inventory.hoverAddr);
                    else
                        live->InvClick(inventory.hoverAddr, isRight);
                } else if (outside && !overBook) {
                    live->InvDropCursor(!isRight);
                }
                inventory.signature.clear();
            }
            if (inventory.dragButton != 0) {
                const bool held = inventory.dragButton == 1 ? left : right;
                if (held) {
                    if (slotTarget && std::find(inventory.dragSlots.begin(), inventory.dragSlots.end(),
                                                inventory.hoverAddr) == inventory.dragSlots.end()) {
                        inventory.dragSlots.push_back(inventory.hoverAddr);
                        inventory.signature.clear();
                    }
                } else {
                    if (inventory.dragSlots.size() >= 2)
                        live->InvSpread(inventory.dragSlots, inventory.dragButton == 2 ? 1 : 0);
                    else if (inventory.dragSlots.size() == 1)
                        live->InvClick(inventory.dragSlots[0], inventory.dragButton == 2);
                    inventory.dragButton = 0;
                    inventory.dragSlots.clear();
                    inventory.signature.clear();
                }
            }
        } else if (!left && !right) {
            inventory.dragButton = 0;
            inventory.dragSlots.clear();
        }
        inventory.leftDown = left;
        inventory.rightDown = right;
    }
}
