#include <algorithm>
#include "client/screen/screens/HudScreen.hpp"
#include "client/systems/multiplayer/HudItemConvert.hpp"
#include "client/systems/sprites/ItemData.hpp"
#include "client/ui/HudFeed.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/systems/resources/AssetJobs.hpp"
#include "client/ui/GuiViewport.hpp"
#include "util/Clock.hpp"
#include "util/Paths.hpp"
#include "client/systems/sprites/PerlinBadge.hpp"
#include "client/systems/resources/Brand.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"

namespace perlin::screen {
    using detail::Clock;

    namespace {
        void PreviewHotbar(ui::HudState& s, int slot) {
            static const struct {
                const char* name;
                int count;
                int maxDamage;
                int aux;
            } kItems[9] = {{"minecraft:diamond_sword", 1, 1561, 120},
                           {"minecraft:grass_block", 64, 0, 0},
                           {"minecraft:oak_planks", 12, 0, 0},
                           {"minecraft:torch", 33, 0, 0},
                           {"minecraft:apple", 5, 0, 0},
                           {"minecraft:cooked_beef", 2, 0, 0},
                           {"minecraft:iron_pickaxe", 1, 250, 40},
                           {"minecraft:oak_log", 20, 0, 0},
                           {"minecraft:bucket", 1, 0, 0}};
            const bool enchanted = PERLIN_ENV("PERLIN_PREVIEW_ENCHANT") != nullptr;
            for (int i = 0; i < 9; i++) {
                ui::HudItem& item = s.hotbar[i];
                item = {};
                item.name = kItems[i].name;
                item.count = kItems[i].count;
                item.maxDamage = kItems[i].maxDamage;
                item.aux = kItems[i].aux;
                item.enchanted = enchanted;
                if (enchanted)
                    item.enchants = {{9, 3}, {12, 2}, {17, 3}, {26, 1}};
                item.nbtTags = 2;
            }
            if (const char* name = PERLIN_ENV("PERLIN_PREVIEW_ITEM")) {
                ui::HudItem& item = s.hotbar[std::clamp(slot, 0, 8)];
                item.name = name;
                item.count = 1;
                item.maxDamage = item.aux = 0;
            }
            if (const char* list = PERLIN_ENV("PERLIN_PREVIEW_ITEMS")) {
                const std::string all = list;
                size_t at = 0;
                for (int i = 0; i < 9 && at <= all.size(); i++) {
                    const size_t comma = std::min(all.find(',', at), all.size());
                    ui::HudItem& item = s.hotbar[i];
                    item = {};
                    item.name = all.substr(at, comma - at);
                    item.count = 1;
                    if (const char* dye = PERLIN_ENV("PERLIN_PREVIEW_DYE")) {
                        item.hasColor = true;
                        item.color = static_cast<int32_t>(std::strtoul(dye, nullptr, 16));
                    }
                    at = comma + 1;
                }
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_EMPTYHAND"))
                s.hotbar[slot] = {};
        }

        void PreviewScreenAnimation(ui::HudState& s) {
            const char* anim = PERLIN_ENV("PERLIN_PREVIEW_SCREENANIM");
            if (!anim)
                return;
            const double t =
                std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                              .count(),
                          2.5) /
                2.0;
            if (t >= 1.0)
                return;
            s.screenAnimation =
                std::string(anim) == "totem" ? BedrockSession::kTotemScreenAnimation : std::atoi(anim);
            s.screenAnimationT = static_cast<float>(t);
        }

        void PreviewTabList(ui::UiAssets& assets, ui::HudState& s) {
            const char* tab = PERLIN_ENV("PERLIN_PREVIEW_TAB");
            if (!tab)
                return;
            s.tabOpen = true;
            s.tabEntries.clear();
            assets.SetHead("preview", ui::DefaultHeadTexture());
            for (int i = 0; i < std::atoi(tab); i++)
                s.tabEntries.push_back({.name = "Player" + std::to_string(i + 1), .headKey = "preview"});
        }

        void PreviewLocator(ui::HudState& s) {
            s.locator.clear();
            s.locatorActive = false;
            const char* loc = PERLIN_ENV("PERLIN_PREVIEW_LOCATOR");
            if (!loc)
                return;
            s.locatorActive = true;
            const int count = std::max(1, std::atoi(loc));
            for (int i = 0; i < count; i++) {
                ui::LocatorMarker m;
                m.angle = count == 1
                              ? 0.0f
                              : -55.0f + 110.0f * static_cast<float>(i) / static_cast<float>(count - 1);
                m.distance = static_cast<float>(i) * 400.0f / static_cast<float>(std::max(1, count - 1));
                m.pitch = i % 4 == 1 ? 1 : (i % 4 == 3 ? -1 : 0);
                std::array<uint8_t, 16> id{};
                id[0] = static_cast<uint8_t>(37 * (i + 3));
                id[9] = static_cast<uint8_t>(91 * (i + 1));
                m.color = ui::LocatorColorFor(id);
                s.locator.push_back(m);
            }
        }

        void PreviewEffects(ui::HudState& s) {
            s.effects.clear();
            if (!PERLIN_ENV("PERLIN_PREVIEW_EFFECTS"))
                return;
            s.effects.push_back({13, 0, 200, false});
            s.effects.push_back({1, 1, 3600, false});
            s.effects.push_back({10, 0, 1200, true});
            s.effects.push_back({19, 0, 600, false});
            s.effects.push_back({2, 2, 90, false});
        }

        std::string LowerAscii(std::string v) {
            std::ranges::transform(v, v.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        }
    }

    HudScreen::HudScreen(Deps d)
        : uiAssets(d.uiAssets), headUnderwater(d.headUnderwater), hudAnim(d.hudAnim), options(d.options), inputManager(d.inputManager), hud(d.hud), hudState(d.hudState), camera(d.camera), loop(d.loop), hudPartial(d.hudPartial), previewSlot(d.previewSlot), chat(d.chat), form(d.form), inventory(d.inventory), pause(d.pause), session(d.session), applyModSlotOverlays(std::move(d.applyModSlotOverlays)), applyModHud(std::move(d.applyModHud)), worldCovered(std::move(d.worldCovered)), replayHidesHud(std::move(d.replayHidesHud)), renderer(d.renderer), levelRenderer(d.levelRenderer), entityRenderer(d.entityRenderer), dollSelf(d.dollSelf), screens(d.screens), replayCtl(d.replayCtl), drawModTint(std::move(d.drawModTint)), drawModGfx(std::move(d.drawModGfx)) {}

    void HudScreen::FillHudChat(ui::HudState& s) {
        ui::ChatLog::Layout layout = ui::ChatLayoutFor(options.chat);
        if (lastGuiW > 0)
            layout.width = std::min(layout.width, std::max(40, static_cast<int>((lastGuiW - 8) / layout.scale)));
        chat.SetLayout(layout);
        chat.Fill(s, hudAnim.ticks, detail::MsNow(), pause.IsOpen() && pause.OnChatSettings());
        // While typing, the full list stands above the input bar at the bottom as usual; top
        // chat is only for the messages shown during play.
        s.chatAtTop = !options.chat.bottom && !s.chatOpen;
        // Room for the paper doll whenever its option is on, not only while it shows (sprinting,
        // sneaking...), so the chat does not jump up and down; with the doll off, the very top.
        s.chatBelowDoll = options.paperDoll;
    }

    void HudScreen::FillHudSidebar(ui::HudState& s, BedrockSession* live, bool preview) {
        s.sidebarVisible = false;
        s.sidebarTitle.clear();
        s.sidebar.clear();
        if (live) {
            const auto sb = live->Scoreboard();
            s.sidebarVisible = sb.visible;
            if (sb.visible) {
                s.sidebarTitle = sb.title;
                for (const auto& r : sb.rows)
                    s.sidebar.push_back({r.name, r.score});
            }
        } else if (preview && PERLIN_ENV("PERLIN_PREVIEW_SCOREBOARD")) {
            s.sidebarVisible = true;
            s.sidebarTitle = "\xC2\xA7"
                             "e\xC2\xA7"
                             "lPERLIN";
            s.sidebar = {{"Kills", 12},
                         {"Deaths", 3},
                         {"\xC2\xA7"
                          "aCoins",
                          450}};
        }
    }

    void HudScreen::FillPreviewHud(ui::HudState& s) {
        s.health = 20.0f;
        s.food = 20;
        s.xpProgress = 0.55f;
        s.xpLevel = 7;
        s.armor = 0;
        s.selectedSlot = previewSlot;
        PreviewHotbar(s, previewSlot);
        s.burning = PERLIN_ENV("PERLIN_PREVIEW_FIRE") != nullptr;
        s.fireFrame = static_cast<int>(s.tick % 32);
        PreviewScreenAnimation(s);
        PreviewTabList(uiAssets, s);
        if (const char* n = PERLIN_ENV("PERLIN_PREVIEW_ITEMNAME")) {
            s.itemName = n;
            s.itemNameAlpha = 1.0f;
        }
        s.displayHealth = s.health;
        PreviewEffects(s);
        PreviewLocator(s);
    }

    void HudScreen::FillHudVitals(ui::HudState& s, BedrockSession& live) {
        const auto vitals = live.GetVitals();
        s.mode = ui::GameModeFromProtocol(live.GameType());
        s.health = vitals.health;
        s.maxHealth = std::max(1.0f, vitals.maxHealth);
        s.absorption = vitals.absorption;
        s.food = static_cast<int>(std::lround(vitals.hunger));
        s.saturation = vitals.saturation;
        s.xpLevel = vitals.level;
        s.xpProgress = vitals.xpProgress;
        s.air = vitals.air;
        s.burning = vitals.onFire && s.mode != ui::GameMode::Spectator;
        s.fireFrame = static_cast<int>(s.tick % 32);
        s.maxAir = std::max(1, vitals.maxAir);
        s.poison = vitals.poison;
        s.hardcore = live.Hardcore();
        s.wither = vitals.wither;
        s.regeneration = vitals.regeneration;
        s.hungerEffect = vitals.hungerEffect;
        s.healthBlinkUntil = hudAnim.blinkUntil;
        s.displayHealth = static_cast<float>(hudAnim.displayHealth);
        s.effects.clear();
        for (const auto& e : live.GetEffects())
            s.effects.push_back({e.id, e.amplifier, e.ticks, e.ambient});
    }

    void HudScreen::FillHudScreenAnimation(ui::HudState& s, BedrockSession& live) {
        if (auto next = live.TakeScreenAnimation(); next.effect >= 0)
            screenAnimation = next;
        if (screenAnimation.effect < 0)
            return;
        const float t =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - screenAnimation.started).count() /
            2.0f;
        if (t >= 1.0f) {
            screenAnimation = {};
            return;
        }
        s.screenAnimation = screenAnimation.effect;
        s.screenAnimationT = t;
    }

    void HudScreen::FillHudInventory(ui::HudState& s, BedrockSession& live) {
        const auto inv = live.GetInventory();
        s.selectedSlot = std::clamp(inv.selected, 0, 8);
        for (int i = 0; i < 9; i++)
            s.hotbar[i] = ToHudItem(inv.hotbar[i]);
        s.offhand = options.hand.showOffhand || OffhandExperiment() ? ToHudItem(inv.offhand) : ui::HudItem{};
        s.offhandRight = options.hand.leftHanded;
        int armor = 0;
        for (int i = 0; i < 4; i++) {
            s.armorItems[i] = ToHudItem(inv.armor[i]);
            armor += ui::ArmorPoints(inv.armor[i].name);
        }
        s.armor = std::min(20, armor);
    }

    void HudScreen::FillHudLocator(ui::HudState& s, BedrockSession& live) {
        s.locator.clear();
        s.locatorActive = false;
        const ui::LocatorView view{camera.x, camera.y, camera.z, camera.yaw, camera.pitch, camera.fov};
        std::unordered_set<int64_t> waypointActors;
        for (const BedrockSession::LocatorWaypoint& wp : live.GetLocatorWaypoints()) {
            if (wp.hasActor)
                waypointActors.insert(wp.actorId);
            if (!wp.visible)
                continue;
            double tx = wp.x, ty = wp.y, tz = wp.z;
            bool known = wp.hasPosition;
            if (wp.hasActor) {
                entity::Entity target;
                if (live.Entities().FindUnique(wp.actorId, target)) {
                    if (target.isSelf)
                        continue;
                    tx = target.pos.x;
                    ty = target.pos.y + 1.0;
                    tz = target.pos.z;
                    known = true;
                }
            }
            if (!known)
                continue;
            s.locatorActive = true;
            if (auto marker = ui::ProjectLocator(view, tx, ty, tz)) {
                marker->color =
                    wp.hasColor ? (static_cast<uint32_t>(wp.color) & 0xFFFFFFu) : ui::LocatorColorFor(wp.id);
                s.locator.push_back(*marker);
            }
        }
        const auto players = live.PlayerList();
        for (const auto& [id, position] : live.GetTrackedPlayerLocations()) {
            if (!position || id == live.SelfUid() || waypointActors.contains(id))
                continue;
            s.locatorActive = true;
            auto marker = ui::ProjectLocator(view, position->x, position->y, position->z);
            if (!marker)
                continue;
            // Use a known player UUID for the existing locator color palette.
            // Location packets remain useful before the corresponding actor loads.
            marker->color = 0xFFFFFF;
            const auto player = std::ranges::find(players, id, &BedrockSession::TabPlayer::uid);
            if (player != players.end() && player->uuid.size() == 32) {
                const auto hex = [](char c) -> uint8_t {
                    return static_cast<uint8_t>(c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10);
                };
                std::array<uint8_t, 16> uuid{};
                for (size_t i = 0; i < uuid.size(); ++i)
                    uuid[i] = static_cast<uint8_t>((hex(player->uuid[i * 2]) << 4) |
                                                   hex(player->uuid[i * 2 + 1]));
                marker->color = ui::LocatorColorFor(uuid);
            }
            s.locator.push_back(*marker);
        }
    }

    void HudScreen::FillHudTabList(ui::HudState& s, BedrockSession& live) {
        if (!pause.IsOpen())
            pause.players = false;
        s.tabOpen = inputManager.IsDown(Action::PlayerList) && !inputManager.Blocked();
        s.tabEntries.clear();
        if (!s.tabOpen)
            return;
        ui::UiAssets& assets = uiAssets;
        std::string badgeKey;
        if (options.perlinBadges && ui::PerlinBadgeSource().width > 0) {
            badgeKey = "perlin:badge";
            if (!assets.HasHead(badgeKey))
                assets.SetHead(badgeKey, ui::PerlinBadgeSource());
        }
        auto players = live.PlayerList();
        std::ranges::stable_sort(players, {},
                                 [](const BedrockSession::TabPlayer& p) { return LowerAscii(p.name); });
        for (const BedrockSession::TabPlayer& p : players) {
            std::string key = "default";
            if (p.skin && ui::SkinHasHead(p.skin->skinWidth, p.skin->skinHeight)) {
                key = p.uuid + ":" + std::to_string(p.skin->serial);
                if (!assets.HasHead(key))
                    assets.SetHead(key, ui::SkinHeadTexture(p.skin->skinRgba, p.skin->skinWidth));
            } else if (!assets.HasHead(key)) {
                assets.SetHead(key, ui::DefaultHeadTexture());
            }
            const bool perlinUser =
                (p.skin && brand::IsPerlinSkinId(p.skin->skinId)) || p.name == live.PlayerName();
            s.tabEntries.push_back({p.name, key, perlinUser ? badgeKey : std::string()});
        }
    }

    void HudScreen::FillHudHeldName(ui::HudState& s) {
        const ui::HudItem& held = s.hotbar[s.selectedSlot];
        if (held.Empty() || hudAnim.toolTimer <= 0 || s.mode == ui::GameMode::Spectator) {
            s.itemName.clear();
            s.itemNameAlpha = 0.0f;
            return;
        }
        const std::string_view shortName = held.name.starts_with("minecraft:")
                                               ? std::string_view(held.name).substr(10)
                                               : std::string_view(held.name);
        const bool customNamed = !held.customName.empty();
        const std::string display =
            customNamed ? held.customName : uiAssets.ItemName(held.name, held.aux);
        s.itemName = ui::RarityItemName(ui::ItemRarity(shortName, held.enchanted), customNamed, display);
        s.itemNameAlpha = ui::ItemNameAlpha(hudAnim.toolTimer);
        s.itemNameSerial = hudAnim.toolSerial;
    }

    void HudScreen::FillHudTitleText(ui::HudState& s, BedrockSession& live) {
        const auto text = live.GetTitleText();
        if (hudAnim.overlayTime > 0 && !text.actionBar.empty()) {
            s.actionBar = text.actionBar;
            s.actionBarAlpha = ui::ActionBarAlpha(hudAnim.overlayTime, hudPartial);
        } else {
            s.actionBar.clear();
            s.actionBarAlpha = 0.0f;
        }
        if (hudAnim.titleTime > 0 && !text.title.empty()) {
            s.title = text.title;
            s.subtitle = text.subtitle;
            s.titleAlpha = ui::TitleAlpha(
                {hudAnim.titleTime, hudAnim.titleFadeIn, hudAnim.titleStay, hudAnim.titleFadeOut},
                hudPartial);
        } else {
            s.title.clear();
            s.subtitle.clear();
            s.titleAlpha = 0.0f;
        }
    }

    void HudScreen::FillHudPickNotice(ui::HudState& s, BedrockSession& live) {
        if (live.PickItemFlash().at == GuiDataState::Clock::time_point{}) {
            pickNoticeTicks = 0;
            return;
        }
        if (live.TakePickItemNotice(pickNotice)) {
            // Presentation uses Perlin's existing item-text animation. The
            // producer's two-second availability window is tracked separately.
            pickNoticeTicks = 40;
            ++hudAnim.toolSerial;
        }
        if (pickNoticeTicks <= 0)
            return;
        s.itemName = pickNotice.name + '\n' + pickNotice.effects;
        s.itemNameAlpha = ui::ItemNameAlpha(pickNoticeTicks);
        s.itemNameSerial = hudAnim.toolSerial;
    }

    void HudScreen::FillHudState(ui::HudState& s, BedrockSession* live, bool preview) {
        applyModSlotOverlays(s);
        s.visible = options.showHud && (live || preview) && !(replayHidesHud()) &&
                    !form.IsOpen() && !inventory.IsOpen() && !worldCovered();
        s.tick = preview ? static_cast<uint64_t>(
                               std::chrono::duration<double>(Clock::now().time_since_epoch()).count() * 20.0)
                         : hudAnim.ticks;
        s.inWater = headUnderwater;
        FillHudChat(s);
        if (worldCovered()) {
            s.chatLines.clear();
            s.chatOpen = false;
        }
        FillHudSidebar(s, live, preview);
        if (preview) {
            FillPreviewHud(s);
            return;
        }
        if (!live)
            return;
        FillHudVitals(s, *live);
        FillHudScreenAnimation(s, *live);
        FillHudInventory(s, *live);
        FillHudLocator(s, *live);
        FillHudTabList(s, *live);
        FillHudHeldName(s);
        FillHudPickNotice(s, *live);
        FillHudTitleText(s, *live);
        s.paperDoll = options.paperDoll && (PaperDollActive(*live) || options.paperDollAlways);
    }

    void HudScreen::TickHud() {
        if (pickNoticeTicks > 0)
            --pickNoticeTicks;
        if (session)
            session->TickServerGui();
        ui::HudAnimation& a = hudAnim;
        a.ticks++;
        const int tick = static_cast<int>(a.ticks);
        ui::HudState& s = hudState;

        const int health = static_cast<int>(std::ceil(s.health));
        if (!a.haveHealth) {
            a.haveHealth = true;
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        if (health < a.lastHealth) {
            a.lastHurtTick = tick;
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 20;
        }
        if (tick - a.lastHealthTick > 20) {
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        a.lastHealth = health;

        const ui::HudItem& held = s.hotbar[std::clamp(s.selectedSlot, 0, 8)];
        if (held.Empty()) {
            a.toolTimer = 0;
        } else if (!a.lastTool.Empty() && a.lastTool.name == held.name &&
                   (held.maxDamage > 0 || a.lastTool.aux == held.aux)) {
            if (a.toolTimer > 0)
                a.toolTimer--;
        } else {
            a.toolTimer = 40;
            a.toolSerial++;
        }
        a.lastTool = held;

        if (a.overlayTime > 0)
            a.overlayTime--;
        if (a.actionSincePush < 100000)
            a.actionSincePush++;
        if (a.titleTime > 0)
            a.titleTime--;
        if (session) {
            const auto text = session->GetTitleText();
            if (text.actionAt != a.actionAtSeen) {
                a.actionAtSeen = text.actionAt;
                if (!text.actionBar.empty()) {
                    a.actionSincePush = 0;
                    a.overlayTime = ui::kActionBarTicks;
                }
            }
            if (text.titleAt != a.titleAtSeen) {
                a.titleAtSeen = text.titleAt;
                if (!text.title.empty()) {
                    a.titleFadeIn = std::max(0, text.fadeIn);
                    a.titleStay = std::max(0, text.stay);
                    a.titleFadeOut = std::max(0, text.fadeOut);
                    a.titleTime = a.titleFadeIn + a.titleStay + a.titleFadeOut;
                }
            }
            if (text.title.empty() && text.subtitle.empty())
                a.titleTime = 0;
        }
    }

    void HudScreen::FillFrameHud(FrameContext& f) {
        auto& live = f.live;
        auto& preview = loop->preview;
        if (hud)
            hud->SetJavaStyle(true);
        FillHudState(hudState, live, preview);
        hudState.bossBars.clear();
        if (live)
            for (const auto& b : live->BossBars()) {
                if (hudState.bossBars.size() == 8)
                    break;
                hudState.bossBars.push_back({b.title, b.progress, b.color, b.overlay});
            }
        hudState.sidebarVisible = false;
        hudState.sidebar.clear();
        if (live) {
            const auto sb = live->Scoreboard();
            hudState.sidebarVisible = sb.visible && !pause.IsOpen() && hud && hud->Overrides("scoreboard");
            hudState.sidebarTitle = sb.title;
            for (const auto& r : sb.rows)
            {
                if (logged.size() < 512 && logged.insert(r.name).second) {
                    std::string shown;
                    for (unsigned char c : r.name)
                        if (c >= 0x20 && c < 0x7F)
                            shown += static_cast<char>(c);
                        else {
                            char hex[8];
                            std::snprintf(hex, sizeof(hex), "\\x%02X", c);
                            shown += hex;
                        }
                    Log("scoreboard", "sidebar row \"{}\" score {}", shown, r.score);
                }
                hudState.sidebar.push_back({r.name, r.score});
            }
        }
        applyModHud(hudState);
    }

    void HudScreen::EnsureUi(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& startedAt = loop->startedAt;
        const double nowSeconds = std::chrono::duration<double>(frameStart - startedAt).count();
        const std::string vanillaRoot = options.bedrockSamplesDir + "/repo/resource_pack";
        if (nowSeconds >= hudRetry && (!hud || hudBootstrap)) {
            hudRetry = nowSeconds + 2.0;
            const bool vanillaUi = std::filesystem::exists(vanillaRoot + "/ui/hud_screen.json") &&
                                   !assets::Busy(assets::Samples());
            if (hud && vanillaUi) {
                uiAssets.Configure(vanillaRoot, &levelRenderer->Blocks());
                hud->RequestReload();
                hudBootstrap = false;
            } else if (!hud) {
                if (vanillaUi)
                    uiAssets.Configure(vanillaRoot, &levelRenderer->Blocks());
                auto candidate = std::make_unique<ui::Hud>();
                candidate->SetJavaStyle(true);
                if (candidate->Initialize(*renderer, vanillaRoot, paths::DataDir(), !vanillaUi)) {
                    candidate->Overlay().SetImageSource(ui::FormImageBytes);
                    hudBootstrap = !candidate->HasLevelUi();
                    hud = std::move(candidate);
                } else {
                    candidate->Shutdown();
                }
            }
        }
    }

    bool HudScreen::PaperDollActive(BedrockSession& live) {
        if (!dollSelf) {
            paperDollTimer = {};
            return false;
        }
        const entity::Entity& self = *dollSelf;
        const auto inv = live.GetInventory();
        uint64_t armor = 1469598103934665603ull;
        for (const auto& piece : inv.armor) {
            for (const char c : piece.name)
                armor = (armor ^ static_cast<uint8_t>(c)) * 1099511628211ull;
            armor = (armor ^ static_cast<uint64_t>(piece.aux + 1)) * 1099511628211ull;
        }
        const auto now = std::chrono::steady_clock::now();
        const double elapsed =
            paperDollTimer.started ? std::chrono::duration<double>(now - paperDollTimer.last).count() : 0.0;
        paperDollTimer.last = now;
        const bool sneaking = self.Has(entity::flag::Sneaking);
        const bool swimming = self.Has(entity::flag::Swimming);
        const bool sprinting = self.Has(entity::flag::Sprinting) && !headUnderwater;
        if (sneaking || swimming || sprinting)
            paperDollTimer.remaining = 1.0;
        else if (live.IsFlying() || self.Has(entity::flag::Gliding))
            paperDollTimer.remaining = 0.35;
        else if (!paperDollTimer.started || paperDollTimer.armor != armor)
            paperDollTimer.remaining = 3.0;
        else
            paperDollTimer.remaining = std::max(0.0, paperDollTimer.remaining - elapsed);
        paperDollTimer.started = true;
        paperDollTimer.armor = armor;
        return paperDollTimer.remaining > 0.0;
    }

    void HudScreen::DrawPaperDoll(FrameContext& f, int fbW, int fbH) {
        const ui::HudControlRect& r = ui::HudPlayerRect();
        if (!hudState.paperDoll || !hudState.visible || !dollSelf || !r.valid || screens.AnyCoveringOpen() ||
            pause.IsOpen() || hudState.mode == ui::GameMode::Spectator)
            return;
        const auto [scale, guiW, guiH] = ui::OverlayViewport(*hud, fbW, fbH);
        // The player renderer is a HUD control. Respect its actual layout and
        // GUI scaling rather than placing the doll at an independent screen
        // coordinate. Constrain it to the viewport when a pack anchors the
        // control partially off-screen.
        const float block = std::clamp(std::min(r.w, r.h * 0.48f), 10.0f, 22.0f);
        const float cx = std::clamp(r.x + r.w * 0.5f, block * 0.6f + 2.0f,
                                    guiW - block * 0.6f - 2.0f);
        // Keep the 3D doll in the HUD player-renderer region, but leave room
        // above its head for the HUD/debug text. Previously the feet were
        // placed at 93% of the control height, putting the head at the very
        // top edge of the display on the vanilla layout.
        const float feetY = std::clamp(r.y + r.h + block * 0.8f,
                                       block * 2.05f + 2.0f, guiH - 2.0f);
        entity::Entity doll = *dollSelf;
        doll.runtimeId = 0xFFFFFFFFFFFF0004ull;
        doll.isSelf = true;
        entity::EntityRenderer::FrameIn din;
        din.time = std::chrono::duration<double>(f.frameStart - loop->startedAt).count();
        din.dt = f.dt;
        din.firstPerson = false;
        din.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
            din.ownLeftHanded = options.hand.leftHanded;
        din.camera = doll.pos;
        const int clip[4] = {0, 0, fbW, fbH};
        entityRenderer.RenderDoll(doll, din, cx * scale, feetY * scale, block * scale, clip, 22.5f, true);
    }

    void HudScreen::Present(FrameContext& f, int fbW, int fbH) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& live = f.live;
        auto& startedAt = loop->startedAt;
        lastGuiW = ui::OverlayViewport(*hud, fbW, fbH).w;
        drawModTint(fbW, fbH);
        // This model belongs to the HUD player-renderer control. Render its
        // 3-D part first, then compose the normal 2-D HUD controls and debug
        // overlay at their normal layers. It does not become a world entity.
        DrawPaperDoll(f, fbW, fbH);
        drawModGfx(fbW, fbH, true);
        hud->Render();
        drawModGfx(fbW, fbH, false);
        const bool creativeUi =
            hudState.mode == ui::GameMode::Creative || PERLIN_ENV("PERLIN_PREVIEW_CREATIVE");
        if (inventory.IsOpen() && dollSelf && !inventory.workbench && !inventory.horse && !inventory.chest &&
            (!creativeUi || inventory.creative.dollVisible)) {
            const auto [scale, guiW, guiH] = ui::OverlayViewport(*hud, fbW, fbH);
            const bool creativeDoll = creativeUi;
            const float px = static_cast<float>(creativeDoll ? (guiW - ui::kCreativeW) / 2
                                                             : (guiW - ui::kInvPanelW) / 2 +
                                                                   (inventory.book ? 77 : 0)),
                        py = static_cast<float>(creativeDoll ? (guiH - ui::kCreativeH) / 2
                                                             : (guiH - ui::kInvPanelH) / 2);
            const float centerX = creativeDoll ? px + 89.0f : px + 51.0f,
                        centerY = creativeDoll ? py + 27.5f : py + 43.0f;
            const float f = std::atan((centerX - inventory.mouseX) / 40.0f),
                        g = std::atan((centerY - inventory.mouseY) / 40.0f);
            entity::Entity doll = *dollSelf;
            doll.runtimeId = 0xFFFFFFFFFFFF0002ull;
            doll.pos = {0.0, 0.0, 0.0};
            doll.yaw = f * 20.0f * 1.0f;
            doll.headYaw = f * 40.0f;
            doll.pitch = -g * 20.0f;
            doll.moveSerial = 1;
            doll.isSelf = true;
            doll.flags &= ~entity::flag::Sneaking;
            entity::EntityRenderer::FrameIn din;
            din.time = std::chrono::duration<double>(frameStart - startedAt).count();
            din.dt = dt;
            din.firstPerson = false;
            din.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
            din.ownLeftHanded = options.hand.leftHanded;
            din.camera = {0, 0, 0};
            const float dollScale = creativeDoll ? 20.0f : 30.0f;
            const int clip[4] = {static_cast<int>((creativeDoll ? px + 73 : px + 26) * scale),
                                 static_cast<int>((creativeDoll ? py + 6 : py + 8) * scale),
                                 (creativeDoll ? 32 : 49) * scale, (creativeDoll ? 43 : 70) * scale};
            entityRenderer.RenderDoll(doll, din, (creativeDoll ? px + 89.0f : px + 50.5f) * scale,
                                      (centerY + (1.8f / 2.0f + 0.0625f) * dollScale) * scale,
                                      dollScale * scale, clip);
        }
        if (inventory.IsOpen() && inventory.horse && live) {
            std::vector<entity::Entity> all;
            live->Entities().Snapshot(all);
            const int64_t mountRuntime = live->GetInventory().horseEntity;
            const entity::Entity* found = nullptr;
            for (const entity::Entity& e : all)
                if (static_cast<int64_t>(e.runtimeId) == mountRuntime ||
                    (live->RidingUid() != 0 && e.uniqueId == live->RidingUid())) {
                    found = &e;
                    break;
                }
            if (found) {
                const auto [scale, guiW, guiH] = ui::OverlayViewport(*hud, fbW, fbH);
                const float px = static_cast<float>((guiW - ui::kInvPanelW) / 2),
                            py = static_cast<float>((guiH - ui::kInvPanelH) / 2);
                entity::Entity doll = *found;
                doll.runtimeId = 0xFFFFFFFFFFFF0003ull;
                doll.pos = {0.0, 0.0, 0.0};
                doll.vehicleUid = 0;
                doll.flags &= ~(entity::flag::Riding | entity::flag::Sneaking);
                const float f = std::atan((px + 52.0f - inventory.mouseX) / 40.0f),
                            g = std::atan((py + 43.0f - inventory.mouseY) / 40.0f);
                doll.yaw = 0.0f + f * 20.0f;
                doll.headYaw = f * 40.0f;
                doll.pitch = -g * 20.0f;
                doll.moveSerial = 1;
                entity::EntityRenderer::FrameIn din;
                din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                din.dt = dt;
                din.firstPerson = false;
                din.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
            din.ownLeftHanded = options.hand.leftHanded;
                din.camera = {0, 0, 0};
                const int clip[4] = {static_cast<int>((px + 26) * scale),
                                     static_cast<int>((py + 17) * scale), 52 * scale, 53 * scale};
                entityRenderer.RenderDoll(doll, din, (px + 52.0f) * scale, (py + 65.0f) * scale,
                                          22.0f * scale, clip);
            }
        }
        float cx, cy, cw, ch;
        if (options.showHud && !replayCtl.Playing() && !screens.AnyCoveringOpen() && !worldCovered() &&
            (camera.mode == PerspectiveMode::FirstPerson || options.thirdPersonCrosshair) &&
            hudState.mode != ui::GameMode::Spectator && !inputManager.Raw().TouchInteract() &&
            hud->Cursor(cx, cy, cw, ch))
            renderer->DrawHudCursor(cx, cy, cw, ch);
    }
}
