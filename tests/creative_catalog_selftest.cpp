#include "client/screen/screens/CreativeCatalogView.hpp"
#include "client/selftest/Check.hpp"

#include <set>
#include <string>
#include <vector>

namespace {
    struct Icon {
        bool empty = false;
        bool Empty() const { return empty; }
    };
    struct Group {
        int category;
        std::string name;
        Icon icon;
    };
    struct Item {
        int group;
        int id;
    };
    struct Catalog {
        std::vector<Group> groups;
        std::vector<Item> items;
    };
}

int main() {
    using namespace perlin;
    using K = creative::CellKind;
    selftest::Checker t("creative_catalog_selftest");
    t.Equal(creative::kTabCategories, std::array<int, 4>{1, 3, 4, 2},
            "display order differs from the packet category enum");
    for (int tab = 0; tab < 4; ++tab)
        t.Equal(creative::TabForCategory(creative::kTabCategories[static_cast<size_t>(tab)]), tab,
                "category lookup is the inverse of the display order");
    for (int category : {-1, 0, 5, 6, 7, 255})
        t.Equal(creative::TabForCategory(category), -1, "non-tab categories do not alias a visible tab");

    Catalog catalog{
        {{1, "zeta", {}}, {2, "nature", {}}, {1, "", {}}, {1, "alpha", {}},
         {1, "single", {}}, {1, "iconless", {true}}, {1, "empty", {}}},
        {{3, 800}, {0, 900}, {2, 111}, {1, 400}, {0, 3}, {3, 77},
         {2, 222}, {4, 18}, {5, 16}, {5, 4}, {-1, 999}, {99, 998}}};
    std::set<size_t> expanded;
    const std::vector<creative::Cell> collapsed{
        {0, K::CollapsedGroup}, {2, K::Item}, {6, K::Item}, {3, K::CollapsedGroup},
        {7, K::Item}, {8, K::Item}, {9, K::Item}};
    t.Equal(creative::Category(catalog, 1, expanded), collapsed,
            "server group order precedes entry interleaving and group names are not alphabetized");
    expanded.insert(0);
    const std::vector<creative::Cell> firstOpen{
        {0, K::ExpandedGroup}, {1, K::GroupItem}, {4, K::GroupItem}, {2, K::Item}, {6, K::Item},
        {3, K::CollapsedGroup}, {7, K::Item}, {8, K::Item}, {9, K::Item}};
    t.Equal(creative::Category(catalog, 1, expanded), firstOpen,
            "expansion inserts members in packet order without grid-row padding");
    expanded.insert(3);
    const auto bothOpen = creative::Category(catalog, 1, expanded);
    t.Check(bothOpen.size() == 11 && bothOpen[5] == creative::Cell{3, K::ExpandedGroup} &&
                bothOpen[6] == creative::Cell{0, K::GroupItem} &&
                bothOpen[7] == creative::Cell{5, K::GroupItem},
            "multiple named groups expand independently");
    t.Equal(catalog.items[bothOpen[1].index].id, 900,
            "projection indexes retain the original noncontiguous entry identity");
    t.Equal(creative::Category(catalog, 2, expanded), std::vector<creative::Cell>{{3, K::Item}},
            "a named single-entry group is an ordinary item");
    t.Check(creative::Category(catalog, 3, expanded).empty(), "empty categories remain empty");
    expanded.erase(0);
    t.Equal(creative::Category(catalog, 1, expanded).front(), creative::Cell{0, K::CollapsedGroup},
            "collapsing one group does not alter the other group's expansion");
    const auto search = creative::Search(catalog, [](const Item& item) { return item.id >= 800; });
    t.Equal(search, std::vector<creative::Cell>{{0, K::Item}, {1, K::Item}, {10, K::Item}, {11, K::Item}},
            "search projection retains received item order and leaves visibility to its predicate");
    t.Check(creative::Category(Catalog{}, 1, expanded).empty(), "an empty packet has no stale rows");
    return t.Finish();
}
