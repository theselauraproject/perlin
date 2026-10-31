#include <set>
#include <unordered_map>
#include "util/Env.hpp"
#include <iostream>
#include <cstdlib>
#include "client/entity/EntityQuery.hpp"

#include <algorithm>
#include <cmath>
#include "util/Log.hpp"

namespace perlin::entity {
    using molang::Value;

    namespace {
        float WrapDeg(float a) {
            a = std::fmod(a + 180.0f, 360.0f);
            if (a < 0)
                a += 360.0f;
            return a - 180.0f;
        }

        std::string Short(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (s.starts_with("minecraft:"))
                s = s.substr(10);
            return s;
        }

        int Axis(const molang::Args& a, size_t i = 0) { return a.size() > i ? static_cast<int>(a[i].n) : 0; }

        std::string SlotItem(const Entity& e, const molang::Value& slot, int index) {
            if (slot.isString) {
                const std::string& s = slot.s;
                if (s.contains("offhand")) return e.offHand;
                if (s.contains("armor.head")) return e.armor[0];
                if (s.contains("armor.chest")) return e.armor[1];
                if (s.contains("armor.legs")) return e.armor[2];
                if (s.contains("armor.feet")) return e.armor[3];
                return e.mainHand;
            }
            (void)index;
            return e.mainHand;
        }
    }

    bool EvalEntityQuery(const QueryContext& c, const std::string& name, const molang::Args& args, Value& out) {
        if (!c.e)
            return false;
        const Entity& e = *c.e;
        const auto num = [&](double v) { out = Value::Num(v); return true; };
        const auto yes = [&](bool b) { return num(b ? 1.0 : 0.0); };
        const auto has = [&](uint64_t f) { return yes(e.Has(f)); };

        static const struct { const char* name; uint64_t bit; } kFlags[] = {
            {"is_baby", flag::Baby}, {"is_sneaking", flag::Sneaking}, {"is_sprinting", flag::Sprinting},
            {"is_swimming", flag::Swimming}, {"is_gliding", flag::Gliding}, {"is_riding", flag::Riding},
            {"is_on_fire", flag::Onfire}, {"is_onfire", flag::Onfire}, {"is_invisible", flag::Invisible},
            {"is_angry", flag::Angry}, {"is_tamed", flag::Tamed}, {"is_sitting", flag::Sitting}, {"is_sited", flag::Sitting},
            {"is_sheared", flag::Sheared}, {"is_charged", flag::Charged}, {"is_powered", flag::Powered},
            {"is_ignited", flag::Ignited}, {"is_saddled", flag::Saddled}, {"is_using_item", flag::UsingItem},
            {"is_tempted", flag::Tempted}, {"is_in_love", flag::InLove}, {"is_critical", flag::Critical},
            {"is_wall_climbing", flag::WallClimbing}, {"is_resting", flag::Resting}, {"is_interested", flag::Interested},
            {"is_orphaned", flag::Orphaned}, {"is_leashed", flag::Leashed}, {"is_elder", flag::Elder},
            {"is_chested", flag::Chested}, {"is_stackable", flag::Stackable}, {"is_idling", flag::Idling},
            {"is_silent", flag::Silent}, {"is_fire_immune", flag::FireImmune}, {"is_dancing", flag::Dancing},
            {"is_enchanted", flag::Enchanted}, {"is_transforming", flag::Transforming}, {"is_bribed", flag::Bribed},
            {"is_pregnant", flag::Pregnant}, {"is_laying_egg", flag::LayingEgg}, {"is_eating", flag::Eating},
            {"has_collision", flag::HasCollision}, {"has_gravity", flag::Gravity},
            {"is_lingering", flag::Linger}, {"is_rearing", flag::Rearing},
        };
        static const std::unordered_map<std::string, uint64_t> kFlagMap = [] {
            std::unordered_map<std::string, uint64_t> m;
            for (const auto& f : kFlags)
                m.emplace(f.name, f.bit);
            return m;
        }();
        if (const auto f = kFlagMap.find(name); f != kFlagMap.end())
            return f->second ? has(f->second) : yes(false);

        if (name == "is_laying_down") return yes(e.Has(flag::LayingDown));
        if (name == "is_alive") return yes(e.health != 0);
        if (name == "is_on_ground") return yes(e.onGround);
        if (name == "is_jumping") return yes(!e.onGround && c.verticalSpeed > 0.5f);
        if (name == "is_moving") return yes(e.Has(flag::Moving) || c.groundSpeed > 0.1f);
        if (name == "is_first_person") return yes(c.firstPerson);
        if (name == "is_on_screen") return yes(c.onScreen);
        if (name == "is_in_water" || name == "is_in_contact_with_water") return yes(c.inWater);
        if (name == "is_in_water_or_rain") return yes(c.inWater || c.inRain);
        if (name == "is_in_ui") return yes(false);
        if (name == "is_spectator") return yes(false);
        if (name == "is_sleeping") return yes(false);
        if (name == "is_levitating") return yes(!e.onGround && !e.Has(flag::Gravity) && e.flags != 0);
        if (name == "is_persona_or_premium_skin") return yes(false);
        if (name == "has_head_gear") return yes(!e.armor[0].empty());
        if (name == "has_cape") return yes(e.skin && e.skin->capeWidth > 0);
        if (name == "has_target") return yes(e.targetUid != 0 && e.targetUid != -1);
        if (name == "facing_target_to_range_attack" || name == "is_facing_target_to_range_attack")
            return yes((e.flagsExtended & flag::FacingTargetToRangeAttack) != 0);
        if (name == "is_charging") return has(flag::Charging);
        if (name == "has_rider" || name == "has_owner" || name == "is_attached_to_entity") return yes(e.Has(flag::Tamed) && name == "has_owner");
        if (name == "is_shield_powered" || name == "is_blocking" || name == "blocking") return yes(c.blocking);
        if (name == "is_roaring" || name == "is_stunned" || name == "is_delayed_attacking" ||
            name == "is_playing_dead" || name == "is_casting" || name == "is_celebrating" || name == "is_celebrating_special" ||
            name == "is_croaking" || name == "is_digging" || name == "is_emerging" || name == "is_emoting" ||
            name == "is_ghost" || name == "is_grazing" || name == "is_admiring" || name == "is_avoiding_block" ||
            name == "is_avoiding_mobs" || name == "is_breeding_stock" || name == "is_carrying_block" || name == "is_eating_mob" ||
            name == "is_illager_captain" || name == "is_jump_goal_jumping" || name == "is_rolling" || name == "is_scared" ||
            name == "is_shaking" || name == "is_stalking" || name == "is_standing" || name == "out_of_control" ||
            name == "is_selected_item" || name == "has_biome_tag" || name == "has_block_property" ||
            name == "block_neighbor_has_any_tag" || name == "item_is_charged" || name == "show_bottom" ||
            name == "is_trading" || name == "is_sneezing")
            return yes(false);

        if (name == "modified_move_speed") return num(c.limbAmount);
        if (name == "modified_distance_moved" || name == "walk_distance") return num(c.limbSwing);
        if (name == "ground_speed") return num(c.groundSpeed);
        if (name == "vertical_speed") return num(c.verticalSpeed);
        if (name == "delta_time") return num(c.dt);
        if (name == "life_time") return num(c.life);
        if (name == "anim_time") return num(c.animTime);
        if (name == "frame_alpha") return num(c.partial);
        if (name == "time_stamp") return num(std::floor(c.now * 20.0));
        if (name == "yaw_speed") return num(c.yawSpeed);
        if (name == "average_frame_time" || name == "maximum_frame_time" || name == "minimum_frame_time") return num(c.dt);
        if (name == "time_of_day") return num(c.timeOfDay);
        if (name == "day") return num(c.day);
        if (name == "moon_phase") return num(std::fmod(c.day, 8.0));
        if (name == "moon_brightness") {
            static const double kBright[8] = {1.0, 0.75, 0.5, 0.25, 0.0, 0.25, 0.5, 0.75};
            return num(kBright[static_cast<int>(std::fmod(c.day, 8.0)) & 7]);
        }
        if (name == "position") return num(Axis(args) == 0 ? c.pos.x : Axis(args) == 1 ? c.pos.y : c.pos.z);
        if (name == "position_delta") return num(Axis(args) == 0 ? c.dx : Axis(args) == 1 ? c.dy : c.dz);
        if (name == "movement_direction") {
            const double l = std::sqrt(c.dx * c.dx + c.dy * c.dy + c.dz * c.dz);
            if (l < 1e-6) return num(0);
            return num((Axis(args) == 0 ? c.dx : Axis(args) == 1 ? c.dy : c.dz) / l);
        }
        if (name == "distance_from_camera") return num(glm::length(c.pos - c.camera));
        if (name == "camera_rotation") return num(Axis(args) == 0 ? c.cameraPitch : c.cameraYaw);
        if (name == "rotation_to_camera") {
            const glm::dvec3 d = c.camera - (c.pos + glm::dvec3(0.0, 1.0, 0.0));
            const double horiz = std::sqrt(d.x * d.x + d.z * d.z);
            if (Axis(args) == 0)
                return num(-std::atan2(d.y, horiz) * 180.0 / 3.14159265358979);
            return num(std::atan2(-d.x, d.z) * 180.0 / 3.14159265358979);
        }
        if (name == "camera_distance_range_lerp") {
            const double d = glm::length(c.pos - c.camera);
            const double lo = args.size() > 0 ? args[0].n : 0, hi = args.size() > 1 ? args[1].n : 1;
            return num(hi == lo ? 0.0 : std::clamp((d - lo) / (hi - lo), 0.0, 1.0));
        }
        if (name == "head_x_rotation" || name == "target_x_rotation" || name == "body_x_rotation" || name == "eye_target_x_rotation")
            return num(c.pitch);
        if (name == "head_y_rotation" || name == "target_y_rotation" || name == "eye_target_y_rotation")
            return num(WrapDeg(c.headYaw - c.bodyYaw));
        if (name == "body_y_rotation") return num(WrapDeg(c.bodyYaw));
        if (name == "head_roll_angle" || name == "shake_angle" || name == "tail_angle" || name == "wing_flap_position" ||
            name == "wing_flap_speed" || name == "roll_counter" || name == "sneeze_counter" || name == "unhappy_counter" ||
            name == "shield_blocking_bob" || name == "surface_particle_color" || name == "spellcolor" || name == "lod_index" ||
            name == "invulnerable_ticks" || name == "hurt_direction" || name == "texture_frame_index")
            return num(name == "hurt_direction" ? 0 : 0);
        if (name == "hurt_time") return num(c.hurtTime);
        if (name == "sit_amount") return num(c.sitAmount);
        if (name == "swim_amount") return num(c.swimAmount);
        if (name == "lie_amount") return num(c.lieAmount);
        if (name == "swell_amount") return num(c.swellAmount);
        if (name == "standing_scale") return num(90.0 / 63.0 - (90.0 / 63.0 - 1.0) * c.standAmount);
        if (name == "sleep_rotation") return num(0);
        if (name == "health") return num(e.health < 0 ? 20.0 : e.health);
        if (name == "max_health") return num(e.health < 0 ? 20.0 : std::max<double>(e.health, 20.0));
        if (name == "variant") return num(e.variant);
        if (name == "mark_variant") return num(e.markVariant);
        if (name == "skin_id") return num(0);
        if (name == "cape_flap_amount") return num(c.capeFlap);
        if (name == "armor_texture_slot") return num(0);
        if (name == "armor_color_slot") return num(1);
        if (name == "armor_material_slot") return num(0);
        if (name == "has_armor_slot") return yes(Axis(args) >= 0 && Axis(args) < 4 && !e.armor[Axis(args)].empty());
        if (name == "equipment_count") {
            int n = 0;
            for (const std::string& a : e.armor)
                n += a.empty() ? 0 : 1;
            return num(n);
        }
        if (name == "player_level" || name == "max_trade_tier" || name == "scoreboard" || name == "structural_integrity" ||
            name == "remaining_durability" || name == "heightmap" || name == "total_emitter_count" || name == "cardinal_block_face_placed_on" ||
            name == "block_face" || name == "current_squad_target" || name == "previous_squad_block" || name == "get_actor_info_id")
            return num(0);
        if (name == "cardinal_facing_2d" || name == "cardinal_facing" || name == "cardinal_player_facing") {
            const float y = WrapDeg(c.bodyYaw);
            if (y >= -45 && y < 45) return num(3);
            if (y >= 45 && y < 135) return num(4);
            if (y >= -135 && y < -45) return num(5);
            return num(2);
        }

        if (name == "model_scale") return num(e.scale > 0.0f ? e.scale : 1.0f);
        if (name == "death_ticks") return num(e.dying ? c.deathTicks : 0.0f);
        if (name == "swelling_dir") return num(e.Has(flag::Ignited) ? 1.0 : -1.0);
        if (name == "fuse_time") return num(e.Has(flag::Ignited) ? c.swellAmount * 30.0 : -1.0);
        if (name == "ticks_since_last_kinetic_weapon_hit") return num(-1);
        if (name == "owner_identifier") { out = Value::Str(""); return true; }
        if (name == "life_span") return num(22);
        if (name == "is_crawling") return yes(e.crawling);
        if (name == "is_in_lava" || name == "is_searching" || name == "is_sniffing" ||
            name == "is_sonic_boom" || name == "is_shaking_wetness" || name == "has_dash_cooldown" || name == "has_player_rider" ||
            name == "can_damage_nearby_mobs" || name == "is_riding_any_entity_of_type" || name == "has_any_leashed_entity_of_type" ||
            name == "timer_flag_1" || name == "timer_flag_2" || name == "timer_flag_3")
            return yes(false);
        if (name == "kinetic_weapon_delay" || name == "kinetic_weapon_dismount_duration" || name == "kinetic_weapon_knockback_duration" ||
            name == "kinetic_weapon_damage_duration" || name == "key_frame_lerp_time" || name == "shake_time" ||
            name == "heartbeat_phase" || name == "overlay_alpha" || name == "previous_squish_value" ||
            name == "current_squish_value" || name == "time_since_last_vibration_detection" || name == "trade_tier" ||
            name == "surface_particle_texture_coordinate" || name == "surface_particle_texture_size" ||
            name == "bone_origin" || name == "bone_rotation" || name == "bone_aabb")
            return num(0);

        if (name == "is_item_equipped") {
            const std::string item = args.empty() ? e.mainHand : (args[0].n == 1.0 ? e.offHand : e.mainHand);
            return yes(!item.empty());
        }
        if (name == "main_hand_item_max_duration" || name == "item_max_use_duration") return num(c.maxUseTicks);
        if (name == "main_hand_item_use_duration" || name == "item_in_use_duration")
            return num(c.using_ ? std::max(0.0f, c.maxUseTicks - c.usedTicks) : 0.0f);
        if (name == "item_remaining_use_duration")
            return num(c.using_ ? std::max(0.0f, c.maxUseTicks - c.usedTicks) / 20.0f : 0.0f);
        if (name == "get_equipped_item_name") {
            bool off = false;
            if (!args.empty())
                off = args[0].isString ? args[0].s.contains("off") : args[0].n == 1.0;
            out = Value::Str(Short(off ? e.offHand : e.mainHand));
            return true;
        }
        if (name == "is_item_name_any") {
            if (args.size() < 2)
                return yes(false);
            const std::string item = Short(SlotItem(e, args[0], Axis(args, 1)));
            for (size_t i = 2; i < args.size(); i++)
                if (args[i].isString && Short(args[i].s) == item)
                    return yes(true);
            return yes(false);
        }
        if (name == "equipped_item_any_tag" || name == "equipped_item_all_tags" || name == "equipped_item_is_attachable")
            return yes(false);

        if (name == "get_name") { out = Value::Str(e.name); return true; }
        if (name == "is_name_any") {
            for (const auto& a : args)
                if (a.isString && a.s == e.name)
                    return yes(true);
            return yes(false);
        }
        if (name == "has_any_family") {
            const std::string t = Short(e.type);
            for (const auto& a : args)
                if (a.isString && (Short(a.s) == t || (e.isPlayer && Short(a.s) == "player")))
                    return yes(true);
            return yes(false);
        }
        if (name == "is_owner_identifier_any") return yes(false);

        if (name == "property") {
            if (!args.empty() && args[0].isString) {
                std::string key = args[0].s;
                if (const auto text = e.propText.find(key); text != e.propText.end()) {
                    out = Value::Str(text->second);
                    return true;
                }
                const auto it = e.props.find(key);
                if (it != e.props.end())
                    return num(it->second);
                const size_t colon = key.find(':');
                if (colon != std::string::npos) {
                    const auto it2 = e.props.find(key.substr(colon + 1));
                    if (it2 != e.props.end())
                        return num(it2->second);
                }
                if (key.contains("climate_variant")) { out = Value::Str("temperate"); return true; }
            }
            return num(0);
        }
        if (name == "has_property") {
            return yes(!args.empty() && args[0].isString && (e.props.count(args[0].s) || e.propText.count(args[0].s)));
        }

        if (name == "in_range") return yes(args.size() >= 3 && args[0].n >= args[1].n && args[0].n <= args[2].n);
        if (name == "approx_eq") {
            for (size_t i = 1; i < args.size(); i++)
                if (std::fabs(args[i].n - args[0].n) > 1e-5)
                    return yes(false);
            return yes(true);
        }
        if (name == "all") {
            for (size_t i = 1; i < args.size(); i++)
                if (args[i].isString ? args[i].s != args[0].s : args[i].n != args[0].n)
                    return yes(false);
            return yes(true);
        }
        if (name == "any") {
            for (size_t i = 1; i < args.size(); i++)
                if (args[i].isString ? args[i].s == args[0].s : args[i].n == args[0].n)
                    return yes(true);
            return yes(false);
        }
        if (name == "count") return num(static_cast<double>(args.size()));
        if (name == "all_animations_finished") return yes(c.allFinished);
        if (name == "any_animation_finished") return yes(c.anyFinished);
        if (name == "log" || name == "debug_output") return num(0);
        if (name == "get_default_bone_pivot") {
            double value = 0;
            if (c.bonePivot && args.size() >= 2 && args[0].isString && c.bonePivot(args[0].s, static_cast<int>(args[1].n), value))
                return num(value);
            return num(0);
        }
        if (name == "get_root_locator_offset" || name == "get_locator_offset" ||
            name == "bone_orientation_matrix" || name == "bone_orientation_trs" || name == "get_animation_frame" ||
            name == "combine_player_texture")
            return num(0);
        if (PERLIN_ENV("PERLIN_MOLANG_LOG")) {
            static std::set<std::string> logged;
            if (logged.insert(name).second)
                Log("molang", "unhandled query.{} ({})", name, e.type);
        }
        return false;
    }
}
