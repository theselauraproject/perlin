#include "client/screen/screens/CreativeCatalogView.hpp"
#include <cassert>
#include <set>
#include <string>
#include <vector>
struct Icon {bool empty = false;bool Empty() const {return empty;}};
struct Group {int category;std::string name;Icon icon;};
struct Item {int group;};
struct Catalog {std::vector<Group> groups;std::vector<Item> items;};
int main() {
    using namespace perlin::creative;
    Catalog c{{{1,"A",{}},{1,"B",{}},{2,"Other",{}}}, {{1},{0},{2},{0},{1},{0}}};
    assert((Category(c,1,std::set<size_t>{}) == std::vector<Cell>{
        {0,CellKind::CollapsedGroup},{1,CellKind::CollapsedGroup}}));
    assert((Category(c,1,std::set<size_t>{0,1}) == std::vector<Cell>{
        {0,CellKind::ExpandedGroup},{1,CellKind::GroupItem},{3,CellKind::GroupItem},
        {5,CellKind::GroupItem},{1,CellKind::ExpandedGroup},
        {0,CellKind::GroupItem},{4,CellKind::GroupItem}}));
    assert((Category(c,2,std::set<size_t>{}) == std::vector<Cell>{{2,CellKind::Item}}));
    // Invalid group IDs are never projected into the wrong category.
    c.items.push_back({-1});
    c.items.push_back({999});
    assert(Category(c,1,std::set<size_t>{0,1}).size() == 7);
}
