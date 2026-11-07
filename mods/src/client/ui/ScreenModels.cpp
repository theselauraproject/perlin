#include "client/ui/ScreenModels.hpp"

#include <algorithm>

namespace perlin::ui {
    namespace {
        Json Dimension(int columns, int rows) {
            Json d = Json::MakeArray();
            d.Push(Json(static_cast<double>(columns)));
            d.Push(Json(static_cast<double>(rows)));
            return d;
        }

        const char* PermissionTexture(int level) {
            switch (level) {
            case 2:
                return "textures/ui/permissions_op_crown";
            case 0:
                return "textures/ui/permissions_visitor_hand";
            default:
                return "textures/ui/permissions_member_star";
            }
        }
    }

    ScreenData PauseScreenData(const PauseModel& m) {
        ScreenData d;
        d.Set("#dressing_room_button_visible", true);
        d.Set("#store_button_visible", true);
        d.Set("#can_change_skin", true);
        // services we don't provide (store, realms stories, xbox invites, screenshots, achievements)
        for (const char* off : {"#store_button_secondary_visible",
                                "#store_button_secondary_enabled",
                                "#store_error_tooltip_visible",
                                "#achievements_visible",
                                "#feedback_button_visible",
                                "#screenshot_button_visible",
                                "#invite_button_visible",
                                "#invite_button_enabled",
                                "#legacy_invite_button_visible",
                                "#legacy_invite_button_enabled",
                                "#permissions_button_visible",
                                "#dressing_room_controller_button_visible",
                                "#friends_drawer_button_visible",
                                "#debug_drawer_button_visible",
                                "#realms_hub_enabled",
                                "#realms_stories_button_primary_visible",
                                "#realms_stories_button_secondary_visible",
                                "#realms_unread_story_count_visible",
                                "#is_using_gamepad",
                                "#is_trial_version",
                                "#pause_annoucement_visible",
                                "#profile_button_b_visible",
                                "#profile_button_x_icon_visible",
                                "#list_has_display_objective",
                                "#disconnected_from_adhoc_label_visible",
                                "#disconnected_from_crossplatform_multiplayer",
                                "#disconnected_from_multiplayer",
                                "#disconnected_from_third_party_label_visible",
                                "#disconnected_from_xbox_live_label_visible",
                                "#gamerpic_visible",
                                "#edu_keyboard_helper_visible"})
            d.Set(off, false);
        d.Set("#store_button_secondary_disabled", true);
        d.Set("#is_signed_in_xbl", true);
        d.Set("#playername", m.playerName);
        d.Set("#playername_visible", !m.playerName.empty());
        d.Set("#gamertag", m.playerName);
        d.Set("#players_list_title", m.listTitle);
        d.Set("#ip_address_text", std::string());
        d.Set("#realms_unread_story_count", std::string());
        d.Set("#realms_access_button_text", std::string());

        auto& rows = d.Collection("players_collection");
        for (const PausePlayer& p : m.players) {
            ScreenData::Row row;
            row["#gamertag"] = Json(p.name);
            row["#button_enabled"] = Json(true);
            row["#button_disabled"] = Json(false);
            row["#gamerpic_visible"] = Json(false);
            row["#local_icon_visible"] = Json(p.local);
            row["#is_platform_icon_visible"] = Json(false);
            row["#player_permission_level_icon_texture"] = Json(std::string(PermissionTexture(p.permission)));
            row["#texture"] = Json(std::string());
            row["#texture_source"] = Json(std::string());
            rows.push_back(std::move(row));
        }
        d.Set("#players_grid_dimension", Dimension(1, static_cast<int>(m.players.size())));
        return d;
    }

    namespace {
        ScreenData::Row SlotRow(const HudItem& item, bool selected) {
            ScreenData::Row row;
            const bool has = !item.Empty();
            row["#item_id_aux"] = Json(has ? 1.0 : 0.0);
            row["#item_renderer_data"] = Json(has ? 1.0 : 0.0);
            row["#inventory_stack_count"] =
                Json(has && item.count > 1 ? std::to_string(item.count) : std::string());
            row["#stack_count_visible"] = Json(has && item.count > 1);
            row["#item_durability_visible"] = Json(has && item.maxDamage > 0 && item.aux > 0);
            row["#item_durability_total_amount"] = Json(static_cast<double>(item.maxDamage));
            row["#item_durability_current_amount"] =
                Json(static_cast<double>(std::max(0, item.maxDamage - item.aux)));
            row["#item_storage_visible"] = Json(false);
            row["#is_selected_slot"] = Json(selected);
            row["#item_lock"] = Json(false);
            row["#item_lock_in_slot"] = Json(false);
            row["#item_lock_in_inventory"] = Json(false);
            row["#hover_text"] =
                Json(has ? (item.customName.empty() ? item.name : item.customName) : std::string());
            row["#is_hovered"] = Json(false);
            return row;
        }
    }

    ScreenData ContainerScreenData(const std::vector<HudItem>& container,
                                   const std::vector<HudItem>& inventory, int selectedHotbar) {
        ScreenData d;
        for (size_t i = 0; i < container.size(); i++)
            d.Collection("container_items").push_back(SlotRow(container[i], false));
        for (size_t i = 9; i < inventory.size(); i++)
            d.Collection("inventory_items").push_back(SlotRow(inventory[i], false));
        for (size_t i = 0; i < 9 && i < inventory.size(); i++)
            d.Collection("hotbar_items")
                .push_back(SlotRow(inventory[i], static_cast<int>(i) == selectedHotbar));
        d.Set("#is_using_gamepad", false);
        d.Set("#pocket_stack_splitting_overlay_visible", false);
        d.Set("#stack_splitting_bar_visible", false);
        d.Set("#show_hovered_selected_inventory_slot", false);
        return d;
    }
}
