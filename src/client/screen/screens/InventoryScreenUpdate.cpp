#include "client/ui/GuiViewport.hpp"
#include "client/screen/screens/InventoryScreen.hpp"
#include "client/screen/screens/CreativeScreen.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/ui/Hud.hpp"
#include "client/systems/preview/PreviewWorld.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "util/Strings.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"

namespace perlin::screen {
    Claim InventoryScreen::Update(Frame& frame) {
        UpdateInventory(frame.hud, frame.live, frame.fbW, frame.fbH);
        return Claim::None;
    }

    void InventoryScreen::UpdateInventory(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_INVENTORY") != nullptr;
        const auto now = Clock::now();
        if (live)
            live->InvPump();
        const bool canOpen = (live || preview) && !host.screens.IsOpen(Id::Death) && !host.screens.IsOpen(Id::Form) && !host.screens.IsOpen(Id::Pause) &&
                             !host.chat.IsOpen() && host.hudState.mode != ui::GameMode::Spectator;
        const bool workbenchOpened = live && live->TakeWorkbenchOpened();
        const bool horseOpened = live && live->TakeHorseOpened();
        const bool chestOpened = live && live->TakeChestOpened();
        if (live) {
            if (now - lastWarm > std::chrono::seconds(1)) {
                lastWarm = now;
                this->chestUi.Warm(host.serverPackDirectories());
            }
        }
        if (!this->IsOpen() && (workbenchOpened || horseOpened || chestOpened) && !host.screens.IsOpen(Id::Death)) {
            if (chestOpened) {
                this->chestUi.Warm(host.serverPackDirectories());
                this->chestUi.WaitReady(150);
            }
            Open(now);
        }
        if (!this->IsOpen() && canOpen &&
            (host.inputManager.Pressed(Action::Inventory) || (preview && !this->previewOpened))) {
            this->previewOpened = true;
            Open(now);
            if (live) {

                if (live->RidingUid() != 0 && live->OpenMountInventory()) {
                    this->mountAsk = now;
                } else {
                    live->OpenInventory();
                    this->mountAsk = {};
                }
            }
        }
        if (this->IsOpen() && live && this->mountAsk != TimePoint{} &&
            now - this->mountAsk > std::chrono::milliseconds(450)) {
            this->mountAsk = {};
            if (live->RidingUid() != 0 && !live->HorseOpen())
                live->OpenInventory();
        }
        if (!this->IsOpen())
            return;
        bool close = false;
        this->workbench =
            live ? live->WorkbenchOpen() : (preview && PERLIN_ENV("PERLIN_PREVIEW_WORKBENCH") != nullptr);
        this->horse = live && live->HorseOpen();
        const char* previewChest = preview ? PERLIN_ENV("PERLIN_PREVIEW_CHEST") : nullptr;
        this->chest = (live && live->ChestOpen()) || previewChest;
        if (preview && !this->bookInit && PERLIN_ENV("PERLIN_PREVIEW_BOOK")) {
            this->bookInit = true;
            this->book = true;
        }
        const bool creativeMode = (live || preview) && host.hudState.mode == ui::GameMode::Creative &&
                                  !this->workbench && !this->horse && !this->chest;
        const bool searching = creativeMode && creative.tab == ui::kCreativeSearchTab;
        if (host.screens.IsOpen(Id::Death) || (live && live->TakeServerClosedInventory()))
            close = true;
        if (this->closeClicked) {
            this->closeClicked = false;
            close = true;
        }
        const auto typedIs = [&](const InputManager::Typed& t, Action a) {
            const Binding& b = host.options.GetKey(a);
            return t.character && b.device == Binding::Device::Keyboard && std::toupper(t.key) == b.code;
        };
        for (const auto& t : host.inputManager.TakeTyped()) {
            if (now - this->openedAt < std::chrono::milliseconds(150))
                continue;
            if (!t.character && t.key == Key::Escape)
                close = true;
            else if (searching &&
                     ui::TextEdit::Focused().Apply(t, creative.search, ui::TextLimits{.maxBytes = 50})) {
            } else if (typedIs(t, Action::Inventory) && !searching)
                close = true;
            else if (typedIs(t, Action::Drop) && live && this->hoverAddr >= 0)
                live->InvDropSlot(this->hoverAddr, t.ctrl);
            else if (typedIs(t, Action::SwapOffhand) && live && this->hoverAddr >= 0)
                live->InvSwapOffhand(this->hoverAddr);
            else if (live && this->hoverAddr >= 0) {
                for (int h = 0; h < 9; h++)
                    if (typedIs(t, static_cast<Action>(static_cast<int>(Action::Hotbar1) + h))) {
                        live->InvHotbarKey(this->hoverAddr, h);
                        break;
                    }
            }
        }
        if (close) {
            if (live)
                live->CloseInventory();
            hud.Overlay().ClearOverlay();
            host.screens.Close(*this);
            this->hoverId.clear();
            this->hover = -1;
            this->hoverAddr = -1;
            if (!host.chat.IsOpen() && !host.screens.IsOpen(Id::Death))
                host.input.SetTextMode(false);
            return;
        }

        if (creativeMode || (preview && PERLIN_ENV("PERLIN_PREVIEW_CREATIVE") && !this->workbench)) {
            UpdateCreative(hud, live, fbW, fbH);
            return;
        }

        if (PERLIN_ENV("PERLIN_PREVIEW_ADV"))
            host.advancedTooltips = true;
        ui::InventoryView v;
        v.advancedTooltips = host.advancedTooltips;
        if (live) {
            const auto inv = live->GetInventory();
            for (int i = 0; i < 9; i++)
                v.slots[i] = ToHudItem(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[9 + i] = ToHudItem(inv.main[i]);
            for (int i = 0; i < 4; i++) {
                v.armor[i] = ToHudItem(inv.armor[i]);
                v.craft[i] = ToHudItem(inv.craft[i]);
            }
            for (int i = 0; i < 9; i++)
                v.work[i] = ToHudItem(inv.work[i]);
            v.horse = inv.horseOpen;
            v.horseChest = inv.horseChestSlots;
            for (int i = 0; i < 17; i++)
                v.horseItems[static_cast<size_t>(i)] = ToHudItem(inv.horse[i]);
            if (inv.furnaceKind >= 0) {
                v.furnace = inv.furnaceKind;
                v.chestRows = -1;
                for (int i = 0; i < 3; i++)
                    v.chestItems[static_cast<size_t>(i)] = ToHudItem(inv.chest[i]);
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
                    v.chestItems[static_cast<size_t>(i)] = ToHudItem(inv.chest[i]);
                v.title =
                    i18n::Tr(v.chestRows == 6 && inv.chestTitle == "container.chest" ? "container.chestDouble"
                                                                                     : inv.chestTitle);
                this->chestScreen =
                    ui::ChestScreenName(inv.chestFitRows ? "container.chest" : inv.chestTitle, v.chestRows);
            }
            v.offhand = ToHudItem(inv.offhand);
            v.output = ToHudItem(inv.output);
            v.cursor = ToHudItem(inv.cursor);
            v.selectedHotbar = inv.selected;
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[i] = host.hudState.hotbar[i];
            v.slots[9].name = "minecraft:cobblestone";
            v.slots[9].count = 64;
            v.slots[10].name = "minecraft:oak_planks";
            v.slots[10].count = 12;
            v.slots[20].name = "minecraft:diamond";
            v.slots[20].count = 3;
            for (int i = 0; i < 4; i++)
                v.armor[i] = host.hudState.armorItems[i];
            v.selectedHotbar = host.hudState.selectedSlot;
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
                this->chestScreen = ui::ChestScreenName("container.chest", v.chestRows);
            }
        }

        v.effects = host.hudState.effects;
        if (this->dragButton != 0 && this->dragSlots.size() >= 2 && !v.cursor.Empty()) {
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
            for (int a : this->dragSlots)
                if (ui::HudItem* it = viewItem(a);
                    it && (it->Empty() || (it->name == v.cursor.name && it->aux == v.cursor.aux)))
                    targets.push_back(it);
            if (targets.size() >= 2) {
                const int total = v.cursor.count;
                const int each =
                    this->dragButton == 2 ? 1 : std::max(1, total / static_cast<int>(targets.size()));
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
        v.workbench = this->workbench;
        v.bookOpen = this->book;
        if (this->book && now - this->recipeAt > std::chrono::milliseconds(300)) {
            this->recipeAt = now;
            std::vector<BedrockSession::RecipeEntry> all;
            if (live) {
                all = live->GetRecipes(this->workbench ? 3 : 2);
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
            std::ranges::stable_sort(
                all, [](const auto& a, const auto& b) { return a.craftable && !b.craftable; });
            this->recipeAll = std::move(all);
        }
        if (this->book) {
            constexpr int perPage = ui::kBookCols * ui::kBookRows;
            v.bookPages = std::max(1, static_cast<int>((this->recipeAll.size() + perPage - 1) / perPage));
            this->bookPage = std::clamp(this->bookPage, 0, v.bookPages - 1);
            v.bookPage = this->bookPage;
            v.recipes.clear();
            for (int n = 0; n < perPage; n++) {
                const size_t at = static_cast<size_t>(this->bookPage * perPage + n);
                if (at >= this->recipeAll.size())
                    break;
                ui::RecipeCard c;
                c.out = ToHudItem(this->recipeAll[at].out);
                c.craftable = this->recipeAll[at].craftable;
                c.index = this->recipeAll[at].index;
                v.recipes.push_back(std::move(c));
            }
        }

        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = screen::PointerOrPreview(host.input);
        float mx = pointer.x, my = pointer.y;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        this->mouseX = gx;
        this->mouseY = gy;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, host.options.guiScale);
            this->hoverId = hud.Overlay().OverlayHover();
            this->hoverAddr = ui::InventoryAddrFromId(this->hoverId);
        }
        v.hovered = this->hoverAddr;
        v.hoveredId = this->hoverId;
        v.mouseX = gx;
        v.mouseY = gy;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" +
                                std::to_string(this->hoverAddr) + this->hoverId +
                                (this->book ? "B" : "b") + std::to_string(this->bookPage) +
                                (this->workbench ? "W" : "w") + std::to_string(v.recipes.size());
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
            this->chestUi.Warm(host.serverPackDirectories());
            signature +=
                "K" + std::to_string(v.chestRows) + v.title + (this->chestUi.Ready() ? "J" : "j");
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
        if (signature != this->signature || !hud.Overlay().HasOverlay()) {
            this->signature = signature;
            std::string error;
            ui::SceneDoc chestDoc;
            this->jsonPanel = v.chestRows > 0 && this->chestUi.Build(v, this->chestScreen, guiW,
                                                                             guiH, chestDoc, this->panel);
            if (v.chestRows > 0 && !this->jsonPanel &&
                this->chestUi.Problem() != this->chestProblem) {
                this->chestProblem = this->chestUi.Problem();
                if (!this->chestProblem.empty())
                    Log("chest", "json ui: {}, using the built-in layout", this->chestProblem);
            }
            if (this->jsonPanel && !hud.Overlay().SetOverlay(chestDoc, error)) {
                Log("chest", "{}, using the built-in layout", error);
                this->jsonPanel = false;
            }
            if (!this->jsonPanel)
                hud.Overlay().SetOverlay(ui::BuildInventoryScreen(v, guiW, guiH), error);
            hud.Overlay().MouseMove(gx, gy);
        }
        hud.Overlay().MoveOverlayElement("cursor", static_cast<int>(gx) - 8, static_cast<int>(gy) - 8);
        {
            int tx, ty;
            ui::PlaceTooltip(static_cast<int>(gx), static_cast<int>(gy), guiW, guiH, tx, ty);
            hud.Overlay().MoveOverlayElement("tip", tx, ty);
        }
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);

        const bool left = host.input.IsMouseDown(Mouse::Left), right = host.input.IsMouseDown(Mouse::Right);
        const bool fresh = now - this->openedAt > std::chrono::milliseconds(150);
        const bool leftEdge = left && !this->leftDown, rightEdge = right && !this->rightDown;
        if (live && fresh) {
            const bool slotTarget = this->hoverAddr >= 0 && this->hoverAddr != ui::kInvAddrOutput;
            if (this->dragButton == 0 && (leftEdge || rightEdge) && !v.cursor.Empty() && slotTarget &&
                !host.input.IsKeyDown(Key::Shift)) {
                this->dragButton = leftEdge ? 1 : 2;
                this->dragSlots.assign(1, this->hoverAddr);
                this->signature.clear();
            } else if (leftEdge || rightEdge) {
                const int px = ui::InventoryPanelX(v, guiW), panelH = ui::InventoryPanelH(v),
                          py = (guiH - panelH) / 2;
                const bool outside = this->jsonPanel
                                         ? gx < this->panel[0] || gy < this->panel[1] ||
                                               gx > this->panel[0] + this->panel[2] ||
                                               gy > this->panel[1] + this->panel[3]
                                         : gx < px || gy < py || gx > px + ui::kInvPanelW || gy > py + panelH;
                const bool isRight = !leftEdge;
                const bool overBook =
                    this->book && gx >= (guiW - 147) / 2 - 86 && gx < (guiW - 147) / 2 + 61;
                if (this->hoverId == "close") {
                    this->closeClicked = true;
                } else if (this->hoverId == "rb") {
                    this->book = !this->book;
                    this->recipeAt = {};
                } else if (this->hoverId == "rp") {
                    this->bookPage--;
                } else if (this->hoverId == "rn") {
                    this->bookPage++;
                } else if (this->book && this->hoverId.size() > 1 && this->hoverId[0] == 'r' &&
                           std::isdigit(static_cast<unsigned char>(this->hoverId[1]))) {
                    const size_t n = static_cast<size_t>(std::atoi(this->hoverId.c_str() + 1));
                    if (n < v.recipes.size())
                        live->InvFillRecipe(v.recipes[n].index);
                } else if (this->hoverAddr >= 0) {
                    if (host.dispatchSlotClick(this->hoverAddr, isRight, host.input.IsKeyDown(Key::Shift))) {
                    } else if (host.input.IsKeyDown(Key::Shift))
                        live->InvShiftClick(this->hoverAddr);
                    else
                        live->InvClick(this->hoverAddr, isRight);
                } else if (outside && !overBook) {
                    live->InvDropCursor(!isRight);
                }
                this->signature.clear();
            }
            if (this->dragButton != 0) {
                const bool held = this->dragButton == 1 ? left : right;
                if (held) {
                    if (slotTarget && std::ranges::find(this->dragSlots, this->hoverAddr) ==
                                          this->dragSlots.end()) {
                        this->dragSlots.push_back(this->hoverAddr);
                        this->signature.clear();
                    }
                } else {
                    if (this->dragSlots.size() >= 2)
                        live->InvSpread(this->dragSlots, this->dragButton == 2 ? 1 : 0);
                    else if (this->dragSlots.size() == 1)
                        live->InvClick(this->dragSlots[0], this->dragButton == 2);
                    this->dragButton = 0;
                    this->dragSlots.clear();
                    this->signature.clear();
                }
            }
        } else if (!left && !right) {
            this->dragButton = 0;
            this->dragSlots.clear();
        }
        this->leftDown = left;
        this->rightDown = right;
    }

    void InventoryScreen::Open(TimePoint now) {
        host.screens.Open(*this);
        this->openedAt = now;
        this->picked = -1;
        this->signature.clear();
        host.inputManager.TakeTyped();
        host.input.SetTextMode(true);
        screen::CenterCursorForGui(host);
    }

    void InventoryScreen::UpdateCreative(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto now = Clock::now();
                if (live && live->CreativeVersion() != creativeCatalogVersion) {
            creativeCatalog = live->Creative();
            creativeCatalogVersion = live->CreativeVersion();
            creative.signature.clear();
        } else if (!live && creativeCatalog.items.empty()) {
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
                creativeCatalog.groups.push_back(group);
                for (int rep = 0; rep < 12; rep++)
                    for (int k = 0; k < 6; k++) {
                        BedrockSession::CreativeItem item;
                        item.entryId = static_cast<uint32_t>(1 + g * 100 + rep * 6 + k);
                        item.info.name = names[g][k];
                        item.info.count = 1;
                        item.group = g;
                        creativeCatalog.items.push_back(item);
                    }
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_ICONS")) {
                creativeCatalog = {};
                BedrockSession::CreativeGroup group;
                group.category = 1;
                creativeCatalog.groups.push_back(group);
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
                    creativeCatalog.items.push_back(item);
                }
            }
            creativeCatalogVersion = 0;
        }

        if (!live)
            if (const char* forced = PERLIN_ENV("PERLIN_PREVIEW_CREATIVETAB"))
                creative.tab = std::atoi(forced);
        std::array<bool, ui::kCreativeTabs> present{};
        std::array<ui::HudItem, ui::kCreativeTabs> icons;
        for (const auto& g : creativeCatalog.groups) {
            const int tab = g.category - 1;
            if (tab >= 0 && tab < 4) {
                present[static_cast<size_t>(tab)] = true;
                if (icons[static_cast<size_t>(tab)].Empty())
                    icons[static_cast<size_t>(tab)] = ToHudItem(g.icon);
            }
        }
        for (const auto& c : creativeCatalog.items) {
            if (c.group < 0 || static_cast<size_t>(c.group) >= creativeCatalog.groups.size())
                continue;
            const int tab = creativeCatalog.groups[static_cast<size_t>(c.group)].category - 1;
            if (tab >= 0 && tab < 4) {
                present[static_cast<size_t>(tab)] = true;
                if (icons[static_cast<size_t>(tab)].Empty())
                    icons[static_cast<size_t>(tab)] = ToHudItem(c.info);
            }
        }
        present[ui::kCreativeSearchTab] = true;
        present[ui::kCreativeSurvivalTab] = true;
        if (!present[static_cast<size_t>(std::clamp(creative.tab, 0, ui::kCreativeTabs - 1))])
            creative.tab = ui::kCreativeSearchTab;
        creative.tab = std::clamp(creative.tab, 0, ui::kCreativeTabs - 1);

        std::vector<const BedrockSession::CreativeItem*> shown;
        if (creative.tab < 4) {
            for (const auto& c : creativeCatalog.items)
                if (c.group >= 0 && static_cast<size_t>(c.group) < creativeCatalog.groups.size() &&
                    creativeCatalog.groups[static_cast<size_t>(c.group)].category == creative.tab + 1)
                    shown.push_back(&c);
        } else if (creative.tab == ui::kCreativeSearchTab) {
            const std::string needle = str::Lower(creative.search);
            for (const auto& c : creativeCatalog.items) {
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
            creative.scroll = 0.0f;
        if (scrollable && std::fabs(host.inputManager.Scroll()) > 0.0f &&
            creative.tab != ui::kCreativeSurvivalTab) {
            const float step = 1.0f / static_cast<float>(maxRow);
            creative.scroll =
                std::clamp(creative.scroll - (host.inputManager.Scroll() > 0 ? step : -step), 0.0f, 1.0f);
        }
        int firstRow = static_cast<int>(std::lround(creative.scroll * static_cast<float>(maxRow)));
        if (const char* row = PERLIN_ENV("PERLIN_PREVIEW_ROW"))
            firstRow = std::clamp(std::atoi(row), 0, maxRow);

        creative.dollVisible = creative.tab == ui::kCreativeSurvivalTab;
        ui::CreativeView v;
        v.tab = creative.tab;
        v.tabPresent = present;
        v.tabIcons = icons;
        v.tabTitles[0] = i18n::TrOr("itemGroup.name.construction", "Construction");
        v.tabTitles[1] = i18n::TrOr("itemGroup.name.nature", "Nature");
        v.tabTitles[2] = i18n::TrOr("itemGroup.name.equipment", "Equipment");
        v.tabTitles[3] = i18n::TrOr("itemGroup.name.items", "Items");
        v.tabTitles[ui::kCreativeSearchTab] = i18n::TrOr("itemGroup.search", "Search Items");
        v.tabTitles[ui::kCreativeSurvivalTab] = i18n::TrOr("itemGroup.name.inventory", "Survival Inventory");
        v.title = v.tabTitles[static_cast<size_t>(creative.tab)];
        v.advancedTooltips = host.advancedTooltips;
        v.effects = host.hudState.effects;
        v.scroll = creative.scroll;
        v.scrollable = scrollable;
        v.search = creative.search;
        std::array<uint32_t, 45> entry{};
        for (int i = 0; i < 45; i++) {
            const size_t idx = static_cast<size_t>((firstRow * 9) + i);
            if (idx < shown.size()) {
                v.grid[static_cast<size_t>(i)] = ToHudItem(shown[idx]->info);
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
                v.slots[static_cast<size_t>(i)] = ToHudItem(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[static_cast<size_t>(9 + i)] = ToHudItem(inv.main[i]);
            for (int i = 0; i < 4; i++)
                v.armor[static_cast<size_t>(i)] = ToHudItem(inv.armor[i]);
            v.offhand = ToHudItem(inv.offhand);
            v.cursor = ToHudItem(inv.cursor);
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[static_cast<size_t>(i)] = host.hudState.hotbar[i];
            for (int i = 0; i < 4; i++)
                v.armor[static_cast<size_t>(i)] = host.hudState.armorItems[i];
        }

        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const glm::vec2 pointer = screen::PointerOrPreview(host.input);
        float mx = pointer.x, my = pointer.y;
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        this->mouseX = gx;
        this->mouseY = gy;
        std::string hoverId;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, host.options.guiScale);
            hoverId = hud.Overlay().OverlayHover();
        }
        this->hoverAddr = ui::InventoryAddrFromId(hoverId);
        if (hoverId == "del" || (!hoverId.empty() && hoverId[0] == 'g'))
            this->hoverAddr = -1;
        v.hovered = hoverId;
        v.mouseX = gx;
        v.mouseY = gy;

        const bool left = host.input.IsMouseDown(Mouse::Left), right = host.input.IsMouseDown(Mouse::Right);
        const bool fresh = now - this->openedAt > std::chrono::milliseconds(150);
        const int px = (guiW - ui::kCreativeW) / 2, py = (guiH - ui::kCreativeH) / 2;
        const bool leftEdge = left && !this->leftDown, rightEdge = right && !this->rightDown;
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
                if (clickedTab != creative.tab) {
                    creative.tab = clickedTab;
                    creative.scroll = 0.0f;
                    creative.signature.clear();
                }
            } else if (hoverId == "sbar" && leftEdge && scrollable) {
                creative.dragging = true;
            } else if (!hoverId.empty() && hoverId[0] == 'g') {
                const int n = std::atoi(hoverId.c_str() + 1);
                if (n >= 0 && n < 45) {
                    if (!v.cursor.Empty())
                        live->InvDestroyCursor();
                    else if (entry[static_cast<size_t>(n)] != 0)
                        live->InvCreativeTake(entry[static_cast<size_t>(n)],
                                              host.input.IsKeyDown(Key::Shift) ? 2 : (rightEdge ? 1 : 0));
                }
            } else if (hoverId == "del") {
                live->InvDestroyCursor();
            } else if (this->hoverAddr >= 0) {
                if (host.dispatchSlotClick(this->hoverAddr, rightEdge, host.input.IsKeyDown(Key::Shift))) {
                } else if (host.input.IsKeyDown(Key::Shift))
                    live->InvShiftClick(this->hoverAddr);
                else if (!v.cursor.Empty() && this->hoverAddr != ui::kInvAddrOutput &&
                         this->dragButton == 0) {
                    this->dragButton = leftEdge ? 1 : 2;
                    this->dragSlots.assign(1, this->hoverAddr);
                } else
                    live->InvClick(this->hoverAddr, rightEdge);
            } else if (v.cursor.Empty() == false && (gx < px - 30 || gx > px + ui::kCreativeW + 30 ||
                                                     gy < py - 30 || gy > py + ui::kCreativeH + 30)) {
                live->InvDropCursor(!rightEdge);
            }
        }
        bool needRefresh = leftEdge || rightEdge || (!left && this->dragButton != 0);
        const bool middle = host.input.IsMouseDown(Mouse::Middle);
        const bool middleEdge = middle && !this->middleDown;
        if (live && fresh) {
            const bool slotTarget = this->hoverAddr >= 0 && this->hoverAddr != ui::kInvAddrOutput;
            if (middleEdge) {
                if (!hoverId.empty() && hoverId[0] == 'g') {
                    const int n = std::atoi(hoverId.c_str() + 1);
                    if (n >= 0 && n < 45 && entry[static_cast<size_t>(n)] != 0 && v.cursor.Empty())
                        live->InvCreativeTake(entry[static_cast<size_t>(n)], 0);
                } else if (slotTarget) {
                    if (v.cursor.Empty())
                        live->InvCreativeClone(this->hoverAddr);
                    else
                        live->InvCreativePlace(this->hoverAddr);
                    this->dragSlots.assign(1, this->hoverAddr);
                }
            } else if (middle && slotTarget && !v.cursor.Empty() &&
                       std::ranges::find(this->dragSlots, this->hoverAddr) ==
                           this->dragSlots.end()) {
                this->dragSlots.push_back(this->hoverAddr);
                live->InvCreativePlace(this->hoverAddr);
            }
            if (!middle && this->dragButton == 0)
                this->dragSlots.clear();
            if (this->dragButton != 0) {
                const bool held = this->dragButton == 1 ? left : right;
                if (held) {
                    if (slotTarget && std::ranges::find(this->dragSlots, this->hoverAddr) ==
                                          this->dragSlots.end())
                        this->dragSlots.push_back(this->hoverAddr);
                } else {
                    if (this->dragSlots.size() >= 2)
                        live->InvSpread(this->dragSlots, this->dragButton == 2 ? 1 : 0);
                    else if (this->dragSlots.size() == 1)
                        live->InvClick(this->dragSlots[0], this->dragButton == 2);
                    this->dragButton = 0;
                    this->dragSlots.clear();
                }
            }
            if (middleEdge || middle)
                needRefresh = true;
        }
        this->middleDown = middle;
        if (!left)
            creative.dragging = false;
        if (creative.dragging && scrollable) {
            const float t = std::clamp((gy - static_cast<float>(py + 18) - 7.5f) / 97.0f, 0.0f, 1.0f);
            creative.scroll = t;
        }
        this->leftDown = left;
        this->rightDown = right;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + hoverId + "|" +
                                std::to_string(creative.tab) + "|" +
                                std::to_string(static_cast<int>(creative.scroll * 1000)) + "|" +
                                creative.search + "|" + std::to_string(creativeCatalogVersion);
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
        if (signature != creative.signature || !hud.Overlay().HasOverlay()) {
            creative.signature = signature;
            std::string error;
            hud.Overlay().SetOverlay(ui::BuildCreativeScreen(v, guiW, guiH), error);
            hud.Overlay().MouseMove(gx, gy);
        }
        if (needRefresh)
            creative.signature.clear();
        hud.Overlay().MoveOverlayElement("cursor", static_cast<int>(gx) - 8, static_cast<int>(gy) - 8);
        {
            int tx, ty;
            ui::PlaceTooltip(static_cast<int>(gx), static_cast<int>(gy), guiW, guiH, tx, ty);
            hud.Overlay().MoveOverlayElement("tip", tx, ty);
        }
        hud.UpdateOverlay(fbW, fbH, host.options.guiScale);
    }
}
