#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace perlin::creative {
    // UI order is independent of the CreativeItemCategory wire values.
    // It also matches InventoryLeftTabIndex 1..4.
    inline constexpr std::array<int, 4> kTabCategories{1, 3, 4, 2};
    inline constexpr std::array<const char*, 4> kTabTextures{
        "textures/ui/icon_recipe_construction", "textures/ui/icon_recipe_equipment",
        "textures/ui/icon_recipe_item", "textures/ui/icon_recipe_nature"};

    constexpr int TabForCategory(int category) {
        for (size_t tab = 0; tab < kTabCategories.size(); ++tab)
            if (kTabCategories[tab] == category)
                return static_cast<int>(tab);
        return -1;
    }

    enum class CellKind { Item, CollapsedGroup, ExpandedGroup, GroupItem };

    struct Cell {
        // A header indexes catalog.groups; an item indexes catalog.items.
        size_t index = 0;
        CellKind kind = CellKind::Item;

        bool IsHeader() const {
            return kind == CellKind::CollapsedGroup || kind == CellKind::ExpandedGroup;
        }
        bool operator==(const Cell&) const = default;
    };

    template<class Catalog, class Expanded>
    std::vector<Cell> Category(const Catalog& catalog, int category, const Expanded& expanded) {
        std::vector<std::vector<size_t>> members(catalog.groups.size());
        for (size_t i = 0; i < catalog.items.size(); ++i) {
            const auto& item = catalog.items[i];
            if (item.group >= 0 && static_cast<size_t>(item.group) < members.size())
                members[static_cast<size_t>(item.group)].push_back(i);
        }
        std::vector<Cell> result;
        for (size_t i = 0; i < catalog.groups.size(); ++i) {
            const auto& group = catalog.groups[i];
            if (group.category != category)
                continue;
            // Anonymous, empty, single-item and iconless groups do not create
            // expando headers. Neither groups nor their entries are sorted.
            const bool header = !group.name.empty() && members[i].size() > 1 && !group.icon.Empty();
            const bool open = header && expanded.contains(i);
            if (header)
                result.push_back({i, open ? CellKind::ExpandedGroup : CellKind::CollapsedGroup});
            if (!header || open)
                for (const size_t item : members[i])
                    result.push_back({item, header ? CellKind::GroupItem : CellKind::Item});
        }
        return result;
    }

    template<class Catalog, class Matches>
    std::vector<Cell> Search(const Catalog& catalog, Matches&& matches) {
        std::vector<Cell> result;
        for (size_t i = 0; i < catalog.items.size(); ++i)
            if (matches(catalog.items[i]))
                result.push_back({i, CellKind::Item});
        return result;
    }
}
