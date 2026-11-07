#include "client/ui/HudData.hpp"
#include "client/i18n/Lang.hpp"

#include <algorithm>
#include <initializer_list>
#include <cmath>

namespace perlin::ui {
    namespace {
        Json B(bool v) {
            return Json(v);
        }
        Json N(double v) {
            return Json(v);
        }
    }

    Vars EngineRootVariables() {
        Vars v;
        Json xp = Json::MakeArray();
        xp.Push(Json(0.5019608));
        xp.Push(Json(1.0));
        xp.Push(Json(0.1254902));
        v["$experience_text_color"] = xp;
        v["$desktop_screen"] = Json(true);
        v["$pocket_screen"] = Json(false);
        v["$console_screen"] = Json(false);
        v["$gamepad_screen"] = Json(false);
        v["$touch"] = Json(false);
        v["$education_edition"] = Json(false);
        v["$compress_hud_width"] = Json(false);
        v["$is_editor_mode_enabled"] = Json(false);
        v["$is_holographic"] = Json(false);
        v["$is_vr"] = Json(false);
        v["$splitscreen"] = Json(false);
        v["$ps4"] = Json(false);
        v["$xbox"] = Json(false);
        v["$switch"] = Json(false);

        // platform flags set before any screen is built: we pose as the windows (gdk) desktop client
        // so vanilla screens take their desktop branches
        for (const char* on : {"$win10_edition",
                               "$microsoft_os",
                               "$ms_platform",
                               "$is_desktop",
                               "$ignore_edu_pause",
                               "$is_publish",
                               "$is_xboxlive_enabled",
                               "$is_realms_enabled",
                               "$gamepad_supported",
                               "$keyboard_and_mouse_supported",
                               "$can_quit",
                               "$supports_clipboard_set",
                               "$screen_transitions_enabled",
                               "$use_animation",
                               "$new_video_settings",
                               "$display_copyright_info",
                               "$sales_disabled_on_screen",
                               "$unlock_full_game_button_ignored",
                               "$featured_world_ignored",
                               "$edu_feedback_ignored",
                               "$edu_ignore_cloud_storage",
                               "$supports_user_configured_safezone"})
            v[on] = Json(true);
        for (const char* off : {"$is_console",
                                "$console_edition",
                                "$pocket_edition",
                                "$osx_edition",
                                "$apple_os",
                                "$google_os",
                                "$nx_os",
                                "$is_android",
                                "$is_ios",
                                "$is_ps4",
                                "$is_ps5",
                                "$xbox_one",
                                "$thirdpartyconsole",
                                "$is_big_screen",
                                "$is_low_memory_device",
                                "$trial",
                                "$is_new_trial_mode_enabled",
                                "$is_preview_app",
                                "$beta_build",
                                "$pre_release",
                                "$creator_build",
                                "$touch_supported",
                                "$file_picking_supported",
                                "$supports_share",
                                "$is_worldbuilder",
                                "$is_mashup",
                                "$is_secondary_client",
                                "$can_splitscreen",
                                "$is_seeds_enabled",
                                "$supports_add_friend",
                                "$supports_flite_tts",
                                "$platform_tts_exists",
                                "$supports_launching_legacy_version",
                                "$supports_legacy_worlds",
                                "$legacy_worlds_share_storage",
                                "$legacy_worlds_supports_migration",
                                "$auto_fetch_legacy_worlds",
                                "$is_nx_link_unlink_enabled",
                                "$device_must_be_removed_for_xbl_signin",
                                "$requires_xbl_signin_to_play",
                                "$is_editor_playtest_roundtrip",
                                "$built_with_ore_ui_docs_and_tests",
                                "$test_infrastructure_disabled",
                                "$debug_settings",
                                "$show_debug_panel",
                                "$use_smithing_table_2_ui",
                                "$ignore_3rd_party_servers",
                                "$ignore_add_servers",
                                "$content_cards_enabled",
                                "$storage_location_switch_enabled",
                                "$is_holographic",
                                "$pause_menu_icon_settings_layout",
                                "$no_buttons_visible",
                                "$single_button_visible",
                                "$two_buttons_visible",
                                "$three_buttons_visible",
                                "$destructive_two_buttons_visible",
                                "$destructive_three_buttons_visible",
                                "$single_button_checkbox_visible",
                                "$two_buttons_checkbox_visible"})
            v[off] = Json(false);
        for (const char* zero : {"$top_vertical_safezone_size", "$bottom_vertical_safezone_size",
                                 "$left_horizontal_safezone_size", "$right_horizontal_safezone_size"})
            v[zero] = Json(0.0);
        v["$version"] = Json(std::string("1.26.60"));
        v["$store_button_text"] = Json(std::string("menu.store")); // label localizes to "Marketplace"
        return v;
    }

    bool HudData::Global(const std::string& name, Json& out) const {
        const bool survival = s.Survival();
        const bool spectator = s.mode == GameMode::Spectator;
        const bool hud = s.visible;
        const auto set = [&](Json v) {
            out = std::move(v);
            return true;
        };

        if (name == "#hud_visible")
            return set(B(hud));
        if (name == "#hud_visible_centered" || name == "#hud_visible_centered_gui_elements")
            return set(B(hud));
        if (name == "#hud_visible_centered_touch" || name == "#hud_visible_not_centered" ||
            name == "#hotbar_visible_not_centered" || name == "#hotbar_visible_not_centered_resizable")
            return set(B(false));
        if (name == "#hud_alpha")
            return set(N(1.0));
        if (name == "#hud_propagate_alpha")
            return set(B(false));
        if (name == "#hotbar_visible")
            return set(B(hud));
        if (name == "#hotbar_with_xp_bar")
            return set(B(hud && survival && !s.locatorActive));
        if (name == "#hotbar_no_xp_bar")
            return set(B(hud && !survival && !spectator && !s.locatorActive));
        if (name == "#hotbar_with_locator_bar")
            return set(B(hud && !spectator && s.locatorActive));
        if (name == "#show_survival_ui")
            return set(B(hud && survival));
        if (name == "#is_armor_visible")
            return set(B(hud && survival && s.armor > 0));
        if (name == "#is_not_riding_bubbles")
            return set(B(true));
        if (name == "#is_riding_bubbles" || name == "#survival_horse_hearts" ||
            name == "#creative_horse_hearts" || name == "#horse_hearts_touch")
            return set(B(false));
        if (name == "#is_spectator_mode")
            return set(B(spectator));
        if (name == "#paper_doll_visible")
            return set(B(false));
        if (name == "#exp_progress")
            return set(N(std::clamp(s.xpProgress, 0.0f, 1.0f)));
        if (name == "#level_number")
            return set(Json(std::to_string(s.xpLevel)));
        if (name == "#level_number_visible")
            return set(B(survival && s.xpLevel > 0));
        if (name == "#boss_hud_padding" || name == "#boss_hud_touch_padding")
            return set(B(false));
        if (name == "#boss_grid_dimension") {
            Json a = Json::MakeArray();
            a.Push(N(1));
            a.Push(N(static_cast<double>(s.bossBars.size())));
            return set(std::move(a));
        }
        if (name == "#hotbar_grid_dimensions") {
            Json a = Json::MakeArray();
            a.Push(N(9));
            a.Push(N(1));
            return set(std::move(a));
        }
        if (name == "#item_text")
            return set(Json(s.itemName));
        if (name == "#item_name_alpha")
            return set(N(s.itemNameAlpha));
        if (name == "#actionbar_alpha")
            return set(N(s.actionBarAlpha));
        if (name == "#title_alpha")
            return set(N(s.titleAlpha));
        if (name == "#hud_title_text_string")
            return set(Json(s.title));
        if (name == "#hud_subtitle_text_string")
            return set(Json(s.subtitle));
        if (name == "#tooltip_visible")
            return set(B(!s.itemName.empty()));
        if (name == "#status_effects_visible")
            return set(B(hud && !s.effects.empty()));
        if (name == "#scoreboard_sidebar_visible")
            return set(B(hud && s.sidebarVisible));
        if (name == "#objective_sidebar_name")
            return set(Json(s.sidebarTitle));
        if (name == "#scoreboard_sidebar_size")
            return set(N(static_cast<double>(s.sidebar.size())));
        if (name == "#objective_background_opacity")
            return set(N(0.3));
        if (name == "#scoreboard_objective_background_opacity")
            return set(N(0.4));
        if (name == "#interact_visible")
            return set(B(false));
        if (name == "#hud_text_background_alpha")
            return set(N(0.5));
        return false;
    }

    int HudData::CollectionSize(const std::string& collection) const {
        if (collection == "boss_bars")
            return static_cast<int>(s.bossBars.size());
        if (collection == "scoreboard_players" || collection == "scoreboard_scores")
            return static_cast<int>(s.sidebar.size());
        return collection == "hotbar_items" ? 9 : 0;
    }

    bool HudData::CollectionValue(const std::string& collection, int index, const std::string& binding,
                                  Json& out) const {
        const auto set = [&](Json v) {
            out = std::move(v);
            return true;
        };
        if (collection == "boss_bars") {
            if (binding == "#bar_visible")
                return set(B(s.visible && index >= 0 && static_cast<size_t>(index) < s.bossBars.size()));
            if (index < 0 || static_cast<size_t>(index) >= s.bossBars.size())
                return false;
            const auto& bar = s.bossBars[static_cast<size_t>(index)];
            if (binding == "#bossName")
                return set(Json(bar.title));
            if (binding == "#progress_percentage")
                return set(N(std::clamp(bar.progress, 0.0f, 1.0f)));
            return false;
        }
        if (collection == "scoreboard_players" || collection == "scoreboard_scores") {
            if (index < 0 || static_cast<size_t>(index) >= s.sidebar.size())
                return false;
            const HudSidebarRow& row = s.sidebar[static_cast<size_t>(index)];
            if (binding == "#player_name_sidebar" || binding == "#player_name")
                return set(Json(row.name));
            if (binding == "#player_score_sidebar" || binding == "#player_score")
                return set(Json(std::to_string(row.score)));
            return false;
        }
        if (collection != "hotbar_items" || index < 0 || index >= 9)
            return false;
        const HudItem& item = s.hotbar[index];
        if (binding == "#slot_selected")
            return set(B(index == s.selectedSlot));
        if (binding == "#hotbar_visible")
            return set(B(s.visible));
        if (binding == "#item_id_aux")
            return set(N(item.Empty() ? 0 : 1));
        if (binding == "#inventory_stack_count")
            return set(Json(!item.Empty() && item.count > 1 ? std::to_string(item.count) : std::string()));
        if (binding == "#stack_count_visible")
            return set(B(!item.Empty() && item.count > 1));
        if (binding == "#item_durability_visible")
            return set(B(!item.Empty() && item.maxDamage > 0 && item.aux > 0));
        if (binding == "#item_durability_total_amount")
            return set(N(item.maxDamage));
        if (binding == "#item_durability_current_amount")
            return set(N(std::max(0, item.maxDamage - item.aux)));
        if (binding == "#item_storage_visible")
            return set(B(false));
        if (binding == "#item_storage_total_amount" || binding == "#item_storage_current_amount")
            return set(N(0));
        if (binding == "#inactive_item")
            return set(B(false));
        if (binding == "#item_renderer_data")
            return set(N(item.Empty() ? 0 : 1));
        return false;
    }

    std::vector<FactoryItem> HudData::Factory(const std::string& name) const {
        std::vector<FactoryItem> items;
        if (name == "player_list_factory" || name == "player_score_factory") {
            const bool players = name == "player_list_factory";
            for (size_t i = 0; i < s.sidebar.size(); i++)
                items.push_back({players ? "scoreboard_sidebar_player" : "scoreboard_sidebar_score",
                                 {},
                                 players ? "scoreboard_players" : "scoreboard_scores",
                                 static_cast<int>(i)});
        } else if (name == "item_text_factory") {
            if (s.visible && !s.itemName.empty() && s.itemNameAlpha > 0.0f)
                items.push_back({"item_text", {}});
        } else if (name == "hud_actionbar_text_factory") {
            if (s.visible && !s.actionBar.empty() && s.actionBarAlpha > 0.0f) {
                FactoryItem f{"hud_actionbar_text", {}};
                f.vars["$actionbar_text"] = Json(s.actionBar);
                items.push_back(std::move(f));
            }
        } else if (name == "hud_title_text_factory") {
            if (s.visible && !s.title.empty() && s.titleAlpha > 0.0f) {
                FactoryItem f{"hud_title_text", {}};
                f.vars["$title_alpha"] = Json(0.6);
                items.push_back(std::move(f));
            }
        }
        return items;
    }

    uint64_t HudSignature(const HudState& s) {
        uint64_t h = 1469598103934665603ull;
        const auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        const auto mixf = [&](float v) {
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 256.0f))));
        };
        const auto mixq = [&](float v) {
            mix(static_cast<uint64_t>(static_cast<int64_t>(std::lround(v * 24.0f))));
        };
        const auto mixs = [&](const std::string& t) {
            for (const char c : t)
                mix(static_cast<uint8_t>(c));
            mix(0xff);
        };
        mix(i18n::Lang::Get().Version());
        mix(s.bossBars.size());
        for (const auto& bar : s.bossBars) {
            mixs(bar.title);
            mixf(bar.progress);
            mix(static_cast<uint64_t>(bar.color));
            mix(static_cast<uint64_t>(bar.overlay));
        }
        mix(s.sidebarVisible);
        mixs(s.sidebarTitle);
        for (const HudSidebarRow& r : s.sidebar) {
            mixs(r.name);
            mix(static_cast<uint64_t>(static_cast<int64_t>(r.score)));
        }
        mix(s.visible);
        mix(static_cast<uint64_t>(s.mode));
        mixf(s.xpProgress);
        mix(s.locatorActive);
        for (const HudEffect& e : s.effects) {
            mix(static_cast<uint64_t>(e.id));
            mix(static_cast<uint64_t>(e.amplifier));
            mix(static_cast<uint64_t>(e.ticks < 0 ? 999999 : e.ticks / 20));
            mix(e.ambient);
        }
        for (const LocatorMarker& m : s.locator) {
            mixf(m.angle * 4.0f);
            mix(static_cast<uint64_t>(m.pitch + 2));
            mix(m.color);
            mix(static_cast<uint64_t>(std::lround(m.distance / 8.0f)));
        }
        mix(static_cast<uint64_t>(s.xpLevel));
        mix(static_cast<uint64_t>(s.armor));
        mix(static_cast<uint64_t>(s.selectedSlot));
        for (const HudItem& it : s.hotbar) {
            mixs(it.name);
            mix(static_cast<uint64_t>(it.count));
            mix(static_cast<uint64_t>(it.aux));
            mix(it.blockRuntimeId);
            mix(it.enchanted);
            mixs(it.customName);
        }
        mixs(s.itemName);
        mix(s.burning);
        if (s.burning)
            mix(static_cast<uint64_t>(s.fireFrame));
        mixq(s.itemNameAlpha);
        mixs(s.actionBar);
        mixq(s.actionBarAlpha);
        mixs(s.title);
        mixs(s.subtitle);
        mixq(s.titleAlpha);
        for (const HudState::SubtitleLine& l : s.subtitles) {
            mixs(l.text);
            mix(static_cast<uint64_t>(l.direction + 2));
            mixq(l.brightness);
        }
        const bool animated = s.health + s.absorption <= 4.0f || s.regeneration ||
                              s.healthBlinkUntil > static_cast<int>(s.tick) || s.saturation <= 0.0f;
        if (animated)
            mix(s.tick);
        mixf(s.health);
        mixf(s.displayHealth);
        mixf(s.absorption);
        mixf(s.maxHealth);
        mix((s.poison ? 1u : 0u) | (s.wither ? 2u : 0u) | (s.freezing ? 4u : 0u) |
            (s.hungerEffect ? 8u : 0u) | (s.inWater ? 16u : 0u) | (s.regeneration ? 32u : 0u));
        mix(static_cast<uint64_t>(s.food));
        mix(static_cast<uint64_t>(s.air));
        mixf(s.guiW);
        mixf(s.guiH);
        mix(s.chatOpen);
        mix(s.tabOpen);
        if (s.tabOpen)
            for (const auto& t : s.tabEntries) {
                mixs(t.name);
                mixs(t.headKey);
            }
        mix(s.sidebarVisible);
        if (s.sidebarVisible) {
            mixs(s.sidebarTitle);
            for (const auto& row : s.sidebar) {
                mixs(row.name);
                mix(static_cast<uint64_t>(static_cast<int64_t>(row.score)));
            }
        }
        mix(s.debug);
        if (s.debug) {
            for (const std::string& l : s.debugLeft)
                mixs(l);
            for (const std::string& l : s.debugRight)
                mixs(l);
        }
        for (const auto& l : s.chatLines) {
            mixs(l.text);
            mixq(l.alpha);
            mix(static_cast<uint64_t>(l.row));
        }
        if (s.chatOpen) {
            mixs(s.chatInput);
            mix(static_cast<uint64_t>(s.chatCaret));
            mix(static_cast<uint64_t>(s.chatSelStart) | (static_cast<uint64_t>(s.chatSelEnd) << 32));
            mix(s.chatCaretVisible);
            for (const std::string& g : s.chatSuggestions)
                mixs(g);
            mix(static_cast<uint64_t>(s.chatSuggestSelected + 1));
            mix(static_cast<uint64_t>(s.chatSuggestStart));
            mixs(s.chatHint);
            mixs(s.chatGhost);
            mix(static_cast<uint64_t>(s.chatSuggestMore[0]) |
                (static_cast<uint64_t>(s.chatSuggestMore[1]) << 1));
            mix(static_cast<uint64_t>(s.chatScroll));
            mix(static_cast<uint64_t>(s.chatTotalLines));
        }
        return h;
    }
}
