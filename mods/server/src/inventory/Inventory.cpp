#include "inventory/Inventory.hpp"

#include <unordered_map>
#include "inventory/Recipes.hpp"

#include "world/BlockCatalog.hpp"

#include <algorithm>
#include <map>

namespace perlin::server {
    namespace proto = proto2225;
    using Container = proto::ContainerSlotType;
    using Action = proto::ItemStackRequestActionsEntryTypeId;

    int MaxStack(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        if (!item)
            return 64;
        const std::string& n = item->name;
        const auto ends = [&](const char* suffix) {
            const std::string s = suffix;
            return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0;
        };
        const auto has = [&](const char* s) { return n.contains(s); };
        if (ends("_shulker_box") || n == "minecraft:shulker_box" || ends("_bed"))
            return 1;
        if (!item->block) {
            if (has("_sword") || has("_pickaxe") || has("_axe") || has("_shovel") || has("_hoe") ||
                has("_helmet") || has("_chestplate") || has("_leggings") || has("_boots") ||
                has("_horse_armor") || has("_boat") || has("_raft") || has("_minecart") || has("_bed") ||
                has("_bundle") || has("_harness") || has("potion") || has("_pottery_sherd") && false)
                return 1;
            for (const char* single : {"bow",
                                       "crossbow",
                                       "shield",
                                       "trident",
                                       "mace",
                                       "fishing_rod",
                                       "carrot_on_a_stick",
                                       "warped_fungus_on_a_stick",
                                       "flint_and_steel",
                                       "shears",
                                       "elytra",
                                       "saddle",
                                       "totem_of_undying",
                                       "brush",
                                       "spyglass",
                                       "compass",
                                       "recovery_compass",
                                       "clock",
                                       "map",
                                       "filled_map",
                                       "writable_book",
                                       "written_book",
                                       "enchanted_book",
                                       "bucket",
                                       "water_bucket",
                                       "lava_bucket",
                                       "milk_bucket",
                                       "powder_snow_bucket",
                                       "cod_bucket",
                                       "salmon_bucket",
                                       "pufferfish_bucket",
                                       "tropical_fish_bucket",
                                       "axolotl_bucket",
                                       "tadpole_bucket",
                                       "armor_stand",
                                       "lead",
                                       "name_tag",
                                       "goat_horn",
                                       "music_disc"})
                if (n == std::string("minecraft:") + single)
                    return 1;
            if (has("music_disc") || ends("_banner_pattern") || ends("_smithing_template"))
                return 1;
            if (has("ender_pearl") || has("snowball") || ends("_egg") && !has("spawn") || ends("_sign") ||
                ends("_banner") || n == "minecraft:honey_bottle" || n == "minecraft:bucket")
                return 16;
            return 64;
        }
        if (ends("_sign") || ends("_banner"))
            return 16;
        return 64;
    }

    ItemStack* PlayerInventory::Resolve(Container type, int slot) {
        switch (type) {
        case Container::HotbarAndInventory:
            return slot >= 0 && slot < kMainSlots ? &main[static_cast<size_t>(slot)] : nullptr;
        case Container::Hotbar:
            return slot >= 0 && slot < kHotbar ? &main[static_cast<size_t>(slot)] : nullptr;
        case Container::Inventory:
            return slot >= 0 && slot < kMainSlots - kHotbar ? &main[static_cast<size_t>(slot + kHotbar)]
                                                            : nullptr;
        case Container::Armor:
            return slot >= 0 && slot < 4 ? &armor[static_cast<size_t>(slot)] : nullptr;
        case Container::Offhand:
            return &offhand;
        case Container::Cursor:
            return &cursor;
        case Container::CreativeOutput:
            return &output;
        case Container::CraftingInput:
            return slot >= 28 && slot < 41 ? &craft[static_cast<size_t>(slot - 28)] : nullptr;
        case Container::Container:
        case Container::Barrel:
        case Container::Shulker:
            return containerKind >= 0 && containerKind < 2 && slot >= 0 && slot < static_cast<int>(container.size())
                       ? &container[static_cast<size_t>(slot)]
                       : nullptr;
        case Container::FurnaceIngredient:
        case Container::BlastFurnaceIngredient:
        case Container::SmokerIngredient:
            return containerKind >= 2 && container.size() == 3 ? &container[0] : nullptr;
        case Container::FurnaceFuel:
            return containerKind >= 2 && container.size() == 3 ? &container[1] : nullptr;
        case Container::FurnaceOutput:
            return containerKind >= 2 && container.size() == 3 ? &container[2] : nullptr;
        default:
            return nullptr;
        }
    }

    ItemStack PlayerInventory::MakeStack(int16_t networkId, int count) {
        ItemStack s;
        s.networkId = networkId;
        s.count = static_cast<uint16_t>(count);
        s.stackId = NewStackId();
        return s;
    }

    int PlayerInventory::Give(int16_t networkId, int count) {
        const int limit = MaxStack(networkId);
        for (ItemStack& s : main) {
            if (count <= 0)
                break;
            if (!s.Empty() && s.networkId == networkId && s.count < limit) {
                const int add = std::min<int>(count, limit - s.count);
                s.count = static_cast<uint16_t>(s.count + add);
                s.stackId = NewStackId();
                count -= add;
            }
        }
        for (ItemStack& s : main) {
            if (count <= 0)
                break;
            if (s.Empty()) {
                const int add = std::min(count, limit);
                s = MakeStack(networkId, add);
                count -= add;
            }
        }
        return count;
    }

    int PlayerInventory::Give(const ItemStack& stack) {
        if (stack.Empty())
            return 0;
        if (stack.damage == 0)
            return Give(stack.networkId, stack.count);
        int left = stack.count;
        for (ItemStack& s : main) {
            if (left <= 0)
                break;
            if (s.Empty()) {
                s = MakeStack(stack.networkId, 1);
                s.damage = stack.damage;
                left--;
            }
        }
        return left;
    }

    bool PlayerInventory::TakeHeld(int count) {
        ItemStack& s = main[static_cast<size_t>(selected)];
        if (s.Empty() || s.count < count)
            return false;
        s.count = static_cast<uint16_t>(s.count - count);
        if (s.count == 0)
            s = ItemStack{};
        else
            s.stackId = NewStackId();
        return true;
    }

    int ArmorSlotFor(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        if (!item)
            return -1;
        std::string n = item->name;
        if (n.compare(0, 10, "minecraft:") == 0)
            n.erase(0, 10);
        const auto ends = [&](const char* s) {
            const size_t l = std::char_traits<char>::length(s);
            return n.size() >= l && n.compare(n.size() - l, l, s) == 0;
        };
        if (ends("_helmet") || n == "carved_pumpkin" || ends("_skull") || ends("_head") || n == "skull")
            return 0;
        if (ends("_chestplate") || n == "elytra")
            return 1;
        if (ends("_leggings"))
            return 2;
        if (ends("_boots"))
            return 3;
        return -1;
    }

    proto::ItemV4 ToWire(const ItemStack& stack) {
        proto::ItemV4 out;
        if (stack.Empty())
            return out;
        out.networkId = stack.networkId;
        out.count = stack.count;
        out.hasStackId = true;
        out.stackId = stack.stackId;
        if (const CatalogItem* item = BlockCatalog::Instance().ItemById(stack.networkId); item && item->block)
            out.blockRuntimeId = static_cast<uint32_t>(item->blockHash);
        if (stack.damage > 0) {
            std::vector<uint8_t> nbt = {0x0A, 0x00, 0x00, 0x03, 0x06, 0x00, 'D', 'a', 'm', 'a', 'g', 'e'};
            const uint32_t d = stack.damage;
            for (int i = 0; i < 4; i++)
                nbt.push_back(static_cast<uint8_t>(d >> (8 * i)));
            nbt.push_back(0x00);
            const auto fill = [&](auto& extra) {
                extra.hasNbt = decltype(extra.hasNbt)::True;
                auto& tag = extra.nbt.emplace();
                tag.version = 1;
                tag.nbt.bytes = nbt;
            };
            if (out.networkId == net::context::ShieldItemID())
                fill(out.extra.emplace<1>());
            else
                fill(out.extra.emplace<2>());
        }
        return out;
    }

    int MaxDurability(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        if (!item || item->block)
            return 0;
        std::string n = item->name;
        if (n.starts_with("minecraft:"))
            n.erase(0, 10);
        const auto ends = [&](const char* s) { return n.ends_with(s); };
        const auto tier = [&](int wood, int stone, int iron, int gold, int diamond, int netherite, int copper) {
            if (n.starts_with("wooden_"))
                return wood;
            if (n.starts_with("stone_"))
                return stone;
            if (n.starts_with("iron_"))
                return iron;
            if (n.starts_with("golden_"))
                return gold;
            if (n.starts_with("diamond_"))
                return diamond;
            if (n.starts_with("netherite_"))
                return netherite;
            if (n.starts_with("copper_"))
                return copper;
            return 0;
        };
        if (ends("_sword") || ends("_pickaxe") || ends("_axe") || ends("_shovel") || ends("_hoe") || ends("_spear"))
            return tier(59, 131, 250, 32, 1561, 2031, 190);
        const auto armor = [&](int base) {
            if (n.starts_with("leather_"))
                return 5 * base;
            if (n.starts_with("chainmail_"))
                return 15 * base;
            if (n.starts_with("iron_"))
                return 15 * base;
            if (n.starts_with("copper_"))
                return 11 * base;
            if (n.starts_with("golden_"))
                return 7 * base;
            if (n.starts_with("diamond_"))
                return 33 * base;
            if (n.starts_with("netherite_"))
                return 37 * base;
            return 0;
        };
        if (ends("_helmet"))
            return n == "turtle_helmet" ? 275 : armor(11);
        if (ends("_chestplate"))
            return armor(16);
        if (ends("_leggings"))
            return armor(15);
        if (ends("_boots"))
            return armor(13);
        static const std::unordered_map<std::string, int> kOther = {
            {"bow", 384}, {"crossbow", 465}, {"shield", 336}, {"trident", 250}, {"mace", 500},
            {"fishing_rod", 384}, {"flint_and_steel", 64}, {"shears", 238}, {"elytra", 432},
            {"carrot_on_a_stick", 25}, {"warped_fungus_on_a_stick", 100}, {"brush", 64}, {"wolf_armor", 64}};
        if (const auto it = kOther.find(n); it != kOther.end())
            return it->second;
        return 0;
    }

    int ArmorPoints(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        if (!item)
            return 0;
        std::string n = item->name;
        if (n.starts_with("minecraft:"))
            n.erase(0, 10);
        if (n == "turtle_helmet")
            return 2;
        int slot = -1;
        if (n.ends_with("_helmet"))
            slot = 0;
        else if (n.ends_with("_chestplate"))
            slot = 1;
        else if (n.ends_with("_leggings"))
            slot = 2;
        else if (n.ends_with("_boots"))
            slot = 3;
        if (slot < 0)
            return 0;
        static const std::pair<const char*, std::array<int, 4>> kTable[] = {
            {"leather_", {1, 3, 2, 1}},  {"chainmail_", {2, 5, 4, 1}}, {"iron_", {2, 6, 5, 2}},
            {"copper_", {2, 4, 3, 1}},   {"golden_", {2, 5, 3, 1}},    {"diamond_", {3, 8, 6, 3}},
            {"netherite_", {3, 8, 6, 3}}};
        for (const auto& [prefix, points] : kTable)
            if (n.starts_with(prefix))
                return points[static_cast<size_t>(slot)];
        return 0;
    }

    float ArmorToughness(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        if (!item || ArmorPoints(networkId) == 0)
            return 0;
        if (item->name.contains("netherite_"))
            return 3.0f;
        if (item->name.contains("diamond_"))
            return 2.0f;
        return 0;
    }

    float KnockbackResistance(int16_t networkId) {
        const CatalogItem* item = BlockCatalog::Instance().ItemById(networkId);
        return item && ArmorPoints(networkId) > 0 && item->name.contains("netherite_") ? 0.1f : 0.0f;
    }

    namespace {
        bool StackIdMatches(const ItemStack& s, int32_t quoted) {
            return quoted < 0 || s.Empty() || quoted == 0 || quoted == s.stackId;
        }

        bool Move(ItemStack& src, ItemStack& dst, int count) {
            if (count <= 0 || src.Empty() || src.count < count)
                return false;
            if (!dst.Empty()) {
                if (!dst.Same(src) || dst.count + count > MaxStack(dst.networkId))
                    return false;
                dst.count = static_cast<uint16_t>(dst.count + count);
            } else {
                dst.networkId = src.networkId;
                dst.damage = src.damage;
                dst.count = static_cast<uint16_t>(count);
            }
            src.count = static_cast<uint16_t>(src.count - count);
            if (src.count == 0)
                src = ItemStack{};
            return true;
        }
    }

    RequestResult ApplyStackRequest(PlayerInventory& inv, const proto::ItemStackRequest& request,
                                    bool creative) {
        RequestResult result;
        PlayerInventory work = inv;
        std::vector<std::pair<Container, int>> touched;
        const CraftRecipe* crafted = nullptr;
        int craftTimes = 0;
        std::vector<std::string> consumed;

        const auto note = [&](const proto::StackRequestSlotInfo& info) {
            const auto key = std::make_pair(info.slotType.containerId, static_cast<int>(info.slot));
            if (info.slotType.containerId != Container::CreativeOutput &&
                std::find(touched.begin(), touched.end(), key) == touched.end())
                touched.push_back(key);
        };

        for (const proto::ItemStackRequestActionsEntry& a : request.actions) {
            switch (a.typeId) {
            case Action::Take:
            case Action::Place: {
                ItemStack* src = work.Resolve(a.source.slotType.containerId, a.source.slot);
                ItemStack* dst = work.Resolve(a.destination.slotType.containerId, a.destination.slot);
                if (!src || !dst || src == dst || !StackIdMatches(*src, a.source.stackId))
                    return result;
                if (a.destination.slotType.containerId == Container::FurnaceOutput)
                    return result;
                if (a.destination.slotType.containerId == Container::Armor &&
                    (ArmorSlotFor(src->networkId) != static_cast<int>(a.destination.slot) || a.count != 1 ||
                     !dst->Empty()))
                    return result;
                if (!Move(*src, *dst, a.count))
                    return result;
                note(a.source);
                note(a.destination);
                break;
            }
            case Action::Swap: {
                ItemStack* src = work.Resolve(a.source.slotType.containerId, a.source.slot);
                ItemStack* dst = work.Resolve(a.destination.slotType.containerId, a.destination.slot);
                if (!src || !dst || src == dst || !StackIdMatches(*src, a.source.stackId) ||
                    !StackIdMatches(*dst, a.destination.stackId))
                    return result;
                const auto fits = [](const ItemStack& s, int slot) {
                    return s.Empty() || (ArmorSlotFor(s.networkId) == slot && s.count == 1);
                };
                if (a.destination.slotType.containerId == Container::Armor && !fits(*src, a.destination.slot))
                    return result;
                if (a.source.slotType.containerId == Container::Armor && !fits(*dst, a.source.slot))
                    return result;
                std::swap(*src, *dst);
                note(a.source);
                note(a.destination);
                break;
            }
            case Action::Drop:
            case Action::Destroy: {
                ItemStack* src = work.Resolve(a.source.slotType.containerId, a.source.slot);
                if (!src || !StackIdMatches(*src, a.source.stackId) || src->Empty() || src->count < a.count ||
                    a.count == 0)
                    return result;
                if (a.typeId == Action::Drop) {
                    ItemStack out = *src;
                    out.count = a.count;
                    result.dropped.push_back(out);
                }
                src->count = static_cast<uint16_t>(src->count - a.count);
                if (src->count == 0)
                    *src = ItemStack{};
                note(a.source);
                break;
            }
            case Action::CraftCreative: {
                if (!creative)
                    return result;
                const CatalogItem* item = BlockCatalog::Instance().ItemById(static_cast<int16_t>(a.itemId));
                if (!item)
                    return result;
                work.output = work.MakeStack(item->networkId, MaxStack(item->networkId));
                break;
            }
            case Action::CraftRecipe:
            case Action::CraftRecipeAuto: {
                crafted = Recipes::Instance().ByNetId(a.recipeNetworkId);
                if (!crafted)
                    return result;
                craftTimes = std::max<int>(1, a.timesCrafted);
                work.output = work.MakeStack(crafted->outputId, crafted->outputCount * craftTimes);
                break;
            }
            case Action::Consume: {
                ItemStack* src = work.Resolve(a.source.slotType.containerId, a.source.slot);
                if (!src || src->Empty() || src->count < a.count || a.count == 0)
                    return result;
                const CatalogItem* item = BlockCatalog::Instance().ItemById(src->networkId);
                for (int k = 0; k < a.count; k++)
                    consumed.push_back(item ? item->name : std::string());
                src->count = static_cast<uint16_t>(src->count - a.count);
                if (src->count == 0)
                    *src = ItemStack{};
                note(a.source);
                break;
            }
            case Action::Optional:
            case Action::NonImplemented:
            case Action::ResultsDeprecated:
                break;
            default:
                return result;
            }
        }
        if (crafted) {
            std::vector<std::string> need;
            for (int t = 0; t < craftTimes; t++)
                for (const auto& in : crafted->input)
                    if (!in.item.empty())
                        need.push_back(in.item);
            if (need.size() != consumed.size())
                return result;
            std::vector<bool> used(consumed.size(), false);
            for (const std::string& n : need) {
                bool found = false;
                for (size_t k = 0; k < consumed.size() && !found; k++)
                    if (!used[k] && Recipes::Matches(n, consumed[k]))
                        used[k] = found = true;
                if (!found)
                    return result;
            }
        }
        work.output = ItemStack{};
        for (const auto& [type, slot] : touched)
            if (ItemStack* s = work.Resolve(type, slot); s && !s->Empty())
                s->stackId = work.NewStackId();
        inv = work;
        result.ok = true;
        result.touched = std::move(touched);
        return result;
    }

    proto::ItemStackResponsesEntry MakeResponse(PlayerInventory& inv, int32_t requestId,
                                                const RequestResult& result) {
        proto::ItemStackResponsesEntry entry;
        entry.requestId = requestId;
        entry.status = result.ok ? proto::ItemStackResponsesEntryStatus::Ok
                                 : proto::ItemStackResponsesEntryStatus::Error;
        if (!result.ok)
            return entry;
        std::vector<proto::ItemStackResponsesEntryContainersEntry> containers;
        for (const auto& [type, slot] : result.touched) {
            const ItemStack* s = inv.Resolve(type, slot);
            if (!s)
                continue;
            auto it = std::find_if(containers.begin(), containers.end(),
                                   [&](const auto& c) { return c.slotType.containerId == type; });
            if (it == containers.end()) {
                proto::ItemStackResponsesEntryContainersEntry c;
                c.slotType.containerId = type;
                containers.push_back(std::move(c));
                it = containers.end() - 1;
            }
            proto::ItemStackResponsesEntryContainersEntrySlotsEntry slotEntry;
            slotEntry.slot = static_cast<uint8_t>(slot);
            slotEntry.hotbarSlot = static_cast<uint8_t>(slot);
            slotEntry.count = s->Empty() ? 0 : static_cast<uint8_t>(std::min<int>(s->count, 255));
            slotEntry.itemStackId = s->Empty() ? 0 : s->stackId;
            it->slots.push_back(std::move(slotEntry));
        }
        entry.containers = std::move(containers);
        return entry;
    }
}
