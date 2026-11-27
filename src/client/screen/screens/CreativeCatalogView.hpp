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
        // Bedrock exposes group order separately from item order. Keep groups in
        // their wire order, and preserve the original item order *within* each
        // group. Use one packed index allocation instead of constructing a
        // separate growing vector for every group on each UI frame.
        const size_t count = catalog.groups.size();
        std::vector<size_t> offsets(count + 1, 0);
        for (const auto& item : catalog.items)
            if (item.group >= 0 && static_cast<size_t>(item.group) < count)
                ++offsets[static_cast<size_t>(item.group) + 1];
        for (size_t i = 1; i < offsets.size(); ++i)
            offsets[i] += offsets[i - 1];
        std::vector<size_t> members(offsets.back());
        std::vector<size_t> write(offsets.begin(), offsets.end() - 1);
        for (size_t i = 0; i < catalog.items.size(); ++i) {
            const int group = catalog.items[i].group;
            if (group >= 0 && static_cast<size_t>(group) < count)
                members[write[static_cast<size_t>(group)]++] = i;
        }
        std::vector<Cell> result;
        for (size_t g = 0; g < count; ++g) {
            const auto& group = catalog.groups[g];
            if (group.category != category)
                continue;
            const size_t start = offsets[g], end = offsets[g + 1];
            const bool header = !group.name.empty() && end - start > 1 && !group.icon.Empty();
            const bool open = header && expanded.contains(g);
            if (header)
                result.push_back({g, open ? CellKind::ExpandedGroup : CellKind::CollapsedGroup});
            if (!header || open)
                for (size_t j = start; j < end; ++j)
                    result.push_back({members[j], header ? CellKind::GroupItem : CellKind::Item});
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
