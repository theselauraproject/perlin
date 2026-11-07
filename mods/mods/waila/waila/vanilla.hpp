#pragma once
#include <waila/api.hpp>

#include <mod_settings.hpp>

#include <cmath>

namespace waila::vanilla {
    inline std::string namespace_of(std::string_view id) {
        const size_t colon = id.find(':');
        return colon == std::string_view::npos ? "minecraft" : std::string(id.substr(0, colon));
    }

    inline std::string mod_title(std::string_view ns) {
        std::string out;
        bool start = true;
        for (const char c : ns) {
            if (c == '_' || c == '-') {
                out += ' ';
                start = true;
            } else {
                out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                start = false;
            }
        }
        return out;
    }

    inline std::string item_for_block(std::string_view name) {
        static constexpr std::pair<std::string_view, std::string_view> kMap[] = {
            {"minecraft:wheat", "minecraft:wheat_seeds"},
            {"minecraft:carrots", "minecraft:carrot"},
            {"minecraft:potatoes", "minecraft:potato"},
            {"minecraft:beetroot", "minecraft:beetroot_seeds"},
            {"minecraft:melon_stem", "minecraft:melon_seeds"},
            {"minecraft:pumpkin_stem", "minecraft:pumpkin_seeds"},
            {"minecraft:cocoa", "minecraft:cocoa_beans"},
            {"minecraft:sweet_berry_bush", "minecraft:sweet_berries"},
            {"minecraft:nether_wart", "minecraft:nether_wart"},
            {"minecraft:reeds", "minecraft:sugar_cane"},
            {"minecraft:lit_furnace", "minecraft:furnace"},
            {"minecraft:lit_blast_furnace", "minecraft:blast_furnace"},
            {"minecraft:lit_smoker", "minecraft:smoker"},
            {"minecraft:lit_redstone_ore", "minecraft:redstone_ore"},
            {"minecraft:lit_deepslate_redstone_ore", "minecraft:deepslate_redstone_ore"},
            {"minecraft:lit_redstone_lamp", "minecraft:redstone_lamp"},
            {"minecraft:redstone_wire", "minecraft:redstone"},
            {"minecraft:unpowered_repeater", "minecraft:repeater"},
            {"minecraft:powered_repeater", "minecraft:repeater"},
            {"minecraft:unpowered_comparator", "minecraft:comparator"},
            {"minecraft:powered_comparator", "minecraft:comparator"},
            {"minecraft:daylight_detector_inverted", "minecraft:daylight_detector"},
            {"minecraft:tripwire", "minecraft:string"},
            {"minecraft:water", "minecraft:water_bucket"},
            {"minecraft:flowing_water", "minecraft:water_bucket"},
            {"minecraft:lava", "minecraft:lava_bucket"},
            {"minecraft:flowing_lava", "minecraft:lava_bucket"},
            {"minecraft:standing_sign", "minecraft:oak_sign"},
            {"minecraft:wall_sign", "minecraft:oak_sign"},
            {"minecraft:bamboo_sapling", "minecraft:bamboo"},
            {"minecraft:fire", "minecraft:flint_and_steel"},
            {"minecraft:noteblock", "minecraft:noteblock"},
            {"minecraft:mob_spawner", "minecraft:spawner"},
        };
        for (const auto& [from, to] : kMap)
            if (name == from)
                return std::string(to);
        return std::string(name);
    }

    inline constexpr std::string_view kTiers[] = {"wooden", "stone", "iron", "diamond", "netherite"};

    inline std::string_view tool_name(Tool t) {
        switch (t) {
        case Tool::Pickaxe:
            return "pickaxe";
        case Tool::Axe:
            return "axe";
        case Tool::Shovel:
            return "shovel";
        case Tool::Hoe:
            return "hoe";
        case Tool::Sword:
            return "sword";
        default:
            return "";
        }
    }

    inline Tool tool_of(std::string_view item) {
        const std::string_view s = short_name(item);
        const auto ends = [&](std::string_view tail) {
            return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
        };
        if (ends("_pickaxe"))
            return Tool::Pickaxe;
        if (ends("_axe"))
            return Tool::Axe;
        if (ends("_shovel"))
            return Tool::Shovel;
        if (ends("_hoe"))
            return Tool::Hoe;
        if (ends("_sword"))
            return Tool::Sword;
        if (s == "shears")
            return Tool::Shears;
        return Tool::None;
    }

    inline int tier_of(std::string_view item) {
        const std::string_view s = short_name(item);
        if (s.starts_with("wooden") || s.starts_with("golden"))
            return 1;
        if (s.starts_with("stone"))
            return 2;
        if (s.starts_with("iron"))
            return 3;
        if (s.starts_with("diamond") || s.starts_with("netherite"))
            return 4;
        return 0;
    }

    inline std::optional<json::Value> tag_of(const game::BlockTarget& block) {
        const BlockEntityInfo& be = block.entity();
        if (!be.present || be.nbt.empty())
            return std::nullopt;
        return json::parse(be.nbt);
    }

    inline std::string note_name(int note) {
        static constexpr const char* kNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                 "F#", "G",  "G#", "A",  "A#", "B"};
        const int n = std::clamp(note, 0, 24) + 6;
        return std::format("{}{}", kNames[n % 12], 3 + n / 12);
    }

    struct StateReadout {
        std::string_view pattern, state, label;
        int max;
        enum class Show : uint8_t { Fraction, Percent, Level, Flag, DelayTicks, Mode } show;
    };

    inline constexpr StateReadout kCrops[] = {
        {"minecraft:wheat", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:carrots", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:potatoes", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:beetroot", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:melon_stem", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:pumpkin_stem", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:sweet_berry_bush", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:torchflower_crop", "growth", "Growth", 7, StateReadout::Show::Percent},
        {"minecraft:pitcher_crop", "growth", "Growth", 4, StateReadout::Show::Percent},
        {"minecraft:nether_wart", "age", "Growth", 3, StateReadout::Show::Percent},
        {"minecraft:cocoa", "age", "Growth", 2, StateReadout::Show::Percent},
    };

    inline constexpr StateReadout kRedstone[] = {
        {"minecraft:redstone_wire", "redstone_signal", "Power", 15, StateReadout::Show::Level},
        {"minecraft:daylight_detector*", "redstone_signal", "Power", 15, StateReadout::Show::Level},
        {"minecraft:*_repeater", "repeater_delay", "Delay", 3, StateReadout::Show::DelayTicks},
        {"minecraft:*_comparator", "output_subtract_bit", "Mode", 1, StateReadout::Show::Mode},
    };

    inline constexpr StateReadout kDetails[] = {
        {"minecraft:beehive", "honey_level", "Honey", 5, StateReadout::Show::Fraction},
        {"minecraft:bee_nest", "honey_level", "Honey", 5, StateReadout::Show::Fraction},
        {"minecraft:composter", "composter_fill_level", "Level", 8, StateReadout::Show::Fraction},
        {"minecraft:*cauldron", "fill_level", "Fill", 6, StateReadout::Show::Fraction},
        {"minecraft:respawn_anchor", "respawn_anchor_charge", "Charges", 4, StateReadout::Show::Fraction},
        {"minecraft:farmland", "moisturized_amount", "Moisture", 7, StateReadout::Show::Fraction},
        {"minecraft:end_portal_frame", "end_portal_eye_bit", "Eye of Ender", 1, StateReadout::Show::Flag},
        {"minecraft:snow_layer", "height", "Layers", 7, StateReadout::Show::Fraction},
    };

    inline void show_readout(Tooltip& tip, const BlockInfo& block, const StateReadout& r) {
        const auto raw = block.state(r.state);
        if (!raw)
            return;
        const int value = block.state_int(r.state);
        switch (r.show) {
        case StateReadout::Show::Percent:
            if (value >= r.max)
                tip.line(Rgba::white, "{}: §aMature", r.label);
            else
                tip.line(Rgba::white, "{}: {}%", r.label, value * 100 / r.max);
            break;
        case StateReadout::Show::Fraction:
            tip.line(Rgba::white, "{}: {}/{}", r.label, r.state == "height" ? value + 1 : value,
                     r.state == "height" ? r.max + 1 : r.max);
            break;
        case StateReadout::Show::Level:
            tip.line(Rgba::white, "{}: §c{}", r.label, value);
            break;
        case StateReadout::Show::DelayTicks:
            tip.line(Rgba::white, "{}: {} tick{}", r.label, value + 1, value == 0 ? "" : "s");
            break;
        case StateReadout::Show::Mode:
            tip.line(Rgba::white, "{}: {}", r.label, value ? "Subtraction" : "Comparison");
            break;
        case StateReadout::Show::Flag:
            tip.line(Rgba::white, "{}: {}", r.label, value ? "Yes" : "No");
            break;
        }
    }

    struct FlagLabel {
        EntityFlag flag;
        std::string_view label;
    };
    inline constexpr FlagLabel kFlags[] = {
        {EntityFlag::Baby, "Baby"},         {EntityFlag::Sheared, "Sheared"},
        {EntityFlag::Saddled, "Saddled"},   {EntityFlag::Angry, "Angry"},
        {EntityFlag::Charged, "Charged"},   {EntityFlag::Powered, "Powered"},
        {EntityFlag::Ignited, "Ignited"},   {EntityFlag::Sitting, "Sitting"},
        {EntityFlag::InLove, "In love"},    {EntityFlag::Leashed, "Leashed"},
        {EntityFlag::Chested, "Has chest"}, {EntityFlag::Dancing, "Dancing"},
        {EntityFlag::Pregnant, "Pregnant"}, {EntityFlag::Elder, "Elder"},
    };

    inline void install(Registry& r) {
        r.block(
            Stage::Head, always,
            [](Tooltip& tip, const BlockContext& c) {
                tip.set_title(c.block.display_name);
                tip.set_icon(ItemIcon{item_for_block(c.block.block.name)});
            },
            "*", -100);

        r.block(
            Stage::Head, settings::registry_name,
            [](Tooltip& tip, const BlockContext& c) { tip.row().text(c.block.block.name, Rgba::gray); }, "*",
            50);

        r.block(
            Stage::Head, settings::harvest_tool,
            [](Tooltip& tip, const BlockContext& c) {
                const BlockProps& p = c.block.props;
                if (!p.known || (c.creative() && !settings::harvest_creative))
                    return;
                Row* row = &tip.title();
                if (settings::harvest_new_line)
                    row = &tip.row(Stage::Body);
                if (p.hardness < 0.0f) {
                    if (settings::harvest_show_unbreakable)
                        row->right_aligned().text("Unbreakable", Rgba::red);
                    return;
                }
                if (p.tool == Tool::None)
                    return;
                const bool required = p.harvestLevel > 0;
                if (!required && !settings::harvest_effective_tool)
                    return;

                const Item& held = c.held();
                const Tool held_tool = held.Empty() ? Tool::None : tool_of(held.name);
                bool ok = held_tool == p.tool;
                if (ok && required && p.tool == Tool::Pickaxe)
                    ok = tier_of(held.name) >= p.harvestLevel;

                std::vector<std::string> icons;
                if (p.tool == Tool::Shears) {
                    icons.push_back("minecraft:shears");
                } else {
                    const int first =
                        p.tool == Tool::Pickaxe && required ? std::clamp(p.harvestLevel, 1, 4) - 1 : 0;
                    for (int tier = first; tier < 5; tier++)
                        icons.push_back(std::format("minecraft:{}_{}", kTiers[static_cast<size_t>(tier)],
                                                    tool_name(p.tool)));
                }
                const std::string& icon = icons[static_cast<size_t>(c.now_ms / 1000) % icons.size()];
                row->right_aligned().item(icon, 10.0f).gap(-4.0f).mark(ok, 5.0f, 2.5f);
            },
            "*", 10);

        r.block(Stage::Body, settings::crop_progress, [](Tooltip& tip, const BlockContext& c) {
            for (const StateReadout& e : kCrops)
                if (glob(e.pattern, c.block.block.name))
                    show_readout(tip, c.block.block, e);
        });
        r.block(Stage::Body, settings::redstone, [](Tooltip& tip, const BlockContext& c) {
            for (const StateReadout& e : kRedstone)
                if (glob(e.pattern, c.block.block.name))
                    show_readout(tip, c.block.block, e);
        });
        r.block(Stage::Body, settings::block_details, [](Tooltip& tip, const BlockContext& c) {
            for (const StateReadout& e : kDetails)
                if (glob(e.pattern, c.block.block.name))
                    show_readout(tip, c.block.block, e);
        });

        r.block(
            Stage::Body, settings::note_block,
            [](Tooltip& tip, const BlockContext& c) {
                if (auto tag = tag_of(c.block))
                    tip.line(Rgba::white, "Note: {}", note_name(static_cast<int>((*tag)["note"].integer())));
            },
            "minecraft:noteblock");

        r.block(
            Stage::Body, settings::spawner,
            [](Tooltip& tip, const BlockContext& c) {
                if (auto tag = tag_of(c.block)) {
                    const std::string type(((*tag)["EntityIdentifier"]).string());
                    if (!type.empty())
                        tip.line(Rgba::white, "Spawns: {}", game::display_name(NameKind::Entity, type));
                }
            },
            "minecraft:mob_spawner");

        r.block(
            Stage::Body, settings::jukebox,
            [](Tooltip& tip, const BlockContext& c) {
                if (auto tag = tag_of(c.block)) {
                    const std::string disc(((*tag)["RecordItem"]["Name"]).string());
                    if (!disc.empty())
                        tip.line(Rgba::white, "Record: {}", game::display_name(NameKind::Item, disc));
                }
            },
            "minecraft:jukebox");

        r.block(
            Stage::Body, settings::sign_text,
            [](Tooltip& tip, const BlockContext& c) {
                const BlockEntityInfo& be = c.block.entity();
                if (!be.present)
                    return;
                std::string_view text = be.frontText;
                while (!text.empty()) {
                    const size_t nl = text.find('\n');
                    const std::string_view line = text.substr(0, nl);
                    if (!line.empty())
                        tip.line(std::string(line), Rgba::gray);
                    if (nl == std::string_view::npos)
                        break;
                    text.remove_prefix(nl + 1);
                }
            },
            "*sign*");

        r.block(
            Stage::Body, settings::waxed,
            [](Tooltip& tip, const BlockContext& c) {
                if (c.block.block.name.find("waxed_") != std::string::npos || c.block.entity().waxed)
                    tip.line("Waxed", Rgba::gold);
            },
            "*");

        r.block(Stage::Body, settings::item_storage, [](Tooltip& tip, const BlockContext& c) {
            const auto tag = tag_of(c.block);
            if (!tag || !(*tag)["Items"].is_array())
                return;
            const int limit = c.details ? settings::storage_detailed.get() : settings::storage_normal.get();
            constexpr size_t kPerLine = 9;
            Row* row = nullptr;
            int shown = 0, hidden = 0;
            size_t in_row = 0;
            for (const json::Value& item : (*tag)["Items"].array()) {
                const std::string name(item["Name"].string());
                const int count = static_cast<int>(item["Count"].integer(1));
                if (name.empty() || count <= 0)
                    continue;
                if (shown >= limit) {
                    hidden++;
                    continue;
                }
                if (!row || in_row == kPerLine) {
                    row = &tip.row();
                    in_row = 0;
                }
                row->item(name, 16.0f, count).gap(2.0f);
                in_row++;
                shown++;
            }
            if (hidden > 0)
                tip.line(Rgba::gray, "and {} more", hidden);
        });

        r.block(
            Stage::Body, settings::coordinates,
            [](Tooltip& tip, const BlockContext& c) {
                tip.line(Rgba::gray, "Location: {}, {}, {}", c.block.pos.x, c.block.pos.y, c.block.pos.z);
            },
            "*", 90);
        r.block(
            Stage::Body, settings::distance,
            [](Tooltip& tip, const BlockContext& c) {
                tip.line(Rgba::gray, "Distance: {:.1f} blocks", c.block.distance);
            },
            "*", 91);
        r.block(
            Stage::Body, settings::block_face,
            [](Tooltip& tip, const BlockContext& c) { tip.line(Rgba::gray, "Face: {}", c.block.face); }, "*",
            92);
        r.block(
            Stage::Body, settings::block_states,
            [](Tooltip& tip, const BlockContext& c) {
                for (const auto& [key, value] : c.block.block.states)
                    tip.line(Rgba::gray, "{}: {}", key, value);
            },
            "*", 93);
        r.block(
            Stage::Body, settings::debug_data,
            [](Tooltip& tip, const BlockContext& c) {
                const BlockProps& p = c.block.props;
                tip.line(Rgba::dark_gray, "runtime id {}, biome {}", c.block.block.runtimeId,
                         c.block.block.biome);
                tip.line(Rgba::dark_gray, "hardness {}, tool {}, level {}", p.hardness, p.tool,
                         p.harvestLevel);
                const BlockEntityInfo& be = c.block.entity();
                if (be.present)
                    tip.line(Rgba::dark_gray, "{}",
                             be.nbt.size() > 160 ? be.nbt.substr(0, 157) + "..." : be.nbt);
            },
            "*", 100);

        r.block(Stage::Tail, settings::mod_name, [](Tooltip& tip, const BlockContext& c) {
            tip.row().text("§9§o" + mod_title(namespace_of(c.block.block.name)));
        });

        r.entity(
            Stage::Head, always,
            [](Tooltip& tip, const EntityContext& c) {
                const Entity& e = c.entity;
                if (e.isItem) {
                    tip.set_title(game::display_name(NameKind::Item, e.itemName));
                    tip.set_icon(ItemIcon{e.itemName, 16.0f, e.itemCount});
                    return;
                }
                if (e.isPlayer && !e.name.empty())
                    tip.set_title(e.name);
                else if (!e.nameTag.empty())
                    tip.set_title(e.nameTag);
                else
                    tip.set_title(c.target.display_name);
            },
            "*", -100);

        r.entity(
            Stage::Head, settings::registry_name,
            [](Tooltip& tip, const EntityContext& c) { tip.row().text(c.entity.type, Rgba::gray); }, "*", 50);

        r.entity(Stage::Body, settings::entity_health, [](Tooltip& tip, const EntityContext& c) {
            const Entity& e = c.entity;
            if (e.isItem || e.health < 0.0f)
                return;
            const float max = e.maxHealth > 0.0f ? e.maxHealth : std::max(20.0f, e.health);
            const auto style = settings::health_style.get();
            const bool hearts =
                std::ceil(max / 2.0f) <= static_cast<float>(settings::health_max_hearts.get());
            Row& row = tip.row();
            if (hearts && style != settings::HealthStyle::Number)
                row.hearts(e.health, max, settings::hearts_per_line.get());
            if (!hearts || style != settings::HealthStyle::Hearts) {
                if (!row.empty())
                    row.gap(4.0f);
                row.text(Rgba::red, "{:.0f}/{:.0f}", e.health, max);
            }
        });

        r.entity(Stage::Body, settings::entity_armor, [](Tooltip& tip, const EntityContext& c) {
            const float points = c.attribute("minecraft:armor");
            if (points > 0.0f)
                tip.row().armor(points);
        });

        r.entity(Stage::Body, settings::entity_owner, [](Tooltip& tip, const EntityContext& c) {
            const Entity& e = c.entity;
            if (e.ownerUid == 0) {
                if (e.flags.has(EntityFlag::Tamed))
                    tip.line("Tamed", Rgba::white);
                return;
            }
            std::string owner;
            if (auto players = game::players())
                for (const PlayerEntry& p : *players)
                    if (p.uid == e.ownerUid)
                        owner = p.name;
            if (owner.empty())
                if (auto all = game::entities())
                    for (const Entity& other : *all)
                        if (other.uniqueId == e.ownerUid)
                            owner = other.isPlayer && !other.name.empty()
                                        ? other.name
                                        : game::display_name(NameKind::Entity, other.type);
            tip.row().text("Owner: ", Rgba::gray).text(owner.empty() ? std::string("Unknown") : owner);
        });

        r.entity(Stage::Body, settings::entity_state, [](Tooltip& tip, const EntityContext& c) {
            if (c.entity.isItem)
                return;
            std::string joined;
            for (const FlagLabel& f : kFlags) {
                if (!c.entity.flags.has(f.flag))
                    continue;
                if (!joined.empty())
                    joined += ", ";
                joined += f.label;
            }
            if (!joined.empty())
                tip.line(std::move(joined), Rgba::gray);
        });

        const auto horse = [](Tooltip& tip, const EntityContext& c) {
            const float speed = c.attribute("minecraft:movement");
            const float jump = c.attribute("minecraft:horse.jump_strength");
            if (speed > 0.0f)
                tip.line(Rgba::white, "Speed: {:.2f} blocks/s", speed * 43.17f);
            if (jump > 0.0f) {
                const double j = jump;
                const double height =
                    -0.1817584952 * j * j * j + 3.689713992 * j * j + 2.128599134 * j - 0.343930367;
                tip.line(Rgba::white, "Jump height: {:.2f} blocks", height);
            }
        };
        for (const char* type : {"minecraft:horse", "minecraft:donkey", "minecraft:mule",
                                 "minecraft:skeleton_horse", "minecraft:zombie_horse"})
            r.entity(Stage::Body, settings::horse_stats, horse, type);

        r.entity(
            Stage::Body, settings::entity_items,
            [](Tooltip& tip, const EntityContext& c) {
                const Entity& e = c.entity;
                for (const std::string* held : {&e.mainHand, &e.offHand}) {
                    if (held->empty())
                        continue;
                    tip.row().item(*held, 8.0f).gap(2.0f).text(game::display_name(NameKind::Item, *held));
                }
            },
            "*", 60);

        r.entity(
            Stage::Body, settings::coordinates,
            [](Tooltip& tip, const EntityContext& c) {
                tip.line(Rgba::gray, "Location: {:.1f}, {:.1f}, {:.1f}", c.entity.x, c.entity.y, c.entity.z);
            },
            "*", 90);
        r.entity(
            Stage::Body, settings::distance,
            [](Tooltip& tip, const EntityContext& c) {
                tip.line(Rgba::gray, "Distance: {:.1f} blocks", c.target.distance);
            },
            "*", 91);

        r.entity(
            Stage::Body, settings::debug_data,
            [](Tooltip& tip, const EntityContext& c) {
                tip.line(Rgba::dark_gray, "id {} / {}", c.entity.runtimeId, c.entity.uniqueId);
                for (const Attribute& a : c.entity.attributes)
                    tip.line(Rgba::dark_gray, "{}: {:.2f} ({:.2f}..{:.2f})", a.name, a.value, a.min, a.max);
                for (const auto& [key, value] : c.entity.properties)
                    tip.line(Rgba::dark_gray, "{} = {}", key, value);
                for (const auto& [key, text] : c.entity.propertyText)
                    tip.line(Rgba::dark_gray, "{} = {}", key, text);
            },
            "*", 100);

        r.entity(Stage::Tail, settings::mod_name, [](Tooltip& tip, const EntityContext& c) {
            tip.row().text("§9§o" + mod_title(namespace_of(c.entity.type)));
        });
    }
}
