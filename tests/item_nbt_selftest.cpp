#include "client/selftest/Check.hpp"
#include "client/systems/multiplayer/ItemNbtInfo.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

int main() {
    using perlin::ParseItemNbt;
    perlin::selftest::Checker test("item_nbt_selftest");
    // Literal ItemStackBase::_setChargedItem/save layout: chargedItem is a
    // compound, Name a string, Count a byte, and projectile Damage a short.
    const std::vector<uint8_t> arrow{
        10, 0, 0,
        10, 11, 0, 'c', 'h', 'a', 'r', 'g', 'e', 'd', 'I', 't', 'e', 'm',
        8, 4, 0, 'N', 'a', 'm', 'e', 15, 0,
        'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':', 'a', 'r', 'r', 'o', 'w',
        1, 5, 0, 'C', 'o', 'u', 'n', 't', 1,
        2, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 5, 0,
        0, 0};
    const auto parsed = ParseItemNbt(arrow);
    test.Check(parsed.ok && parsed.chargedItemName == "minecraft:arrow" &&
                   parsed.chargedItemCount == 1,
               "saved charged projectile is decoded from direct Name and Count tags");
    test.Check(parsed.customName.empty() && !parsed.hasDamage && parsed.damage == -1,
               "nested projectile name and damage do not become weapon metadata");

    const std::vector<uint8_t> network{
        10, 0,
        10, 11, 'c', 'h', 'a', 'r', 'g', 'e', 'd', 'I', 't', 'e', 'm',
        8, 4, 'N', 'a', 'm', 'e', 25,
        'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':',
        'f', 'i', 'r', 'e', 'w', 'o', 'r', 'k', '_', 'r', 'o', 'c', 'k', 'e', 't',
        1, 5, 'C', 'o', 'u', 'n', 't', 1, 0, 0};
    const auto rocket = ParseItemNbt(network);
    test.Check(rocket.ok && rocket.chargedItemName == "minecraft:firework_rocket" &&
                   rocket.chargedItemCount == 1,
               "network NBT fallback decodes a complete charged firework root");

    auto named = arrow;
    named.pop_back();
    const std::vector<uint8_t> rootFields{
        3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 17, 0, 0, 0,
        10, 7, 0, 'd', 'i', 's', 'p', 'l', 'a', 'y',
        8, 4, 0, 'N', 'a', 'm', 'e', 3, 0, 'b', 'o', 'w', 0, 0};
    named.insert(named.end(), rootFields.begin(), rootFields.end());
    const auto weapon = ParseItemNbt(named);
    test.Check(weapon.ok && weapon.hasDamage && weapon.damage == 17 && weapon.customName == "bow" &&
                   weapon.chargedItemName == "minecraft:arrow",
               "root durability and display name remain distinct from charged item metadata");

    constexpr std::string_view countTag = "Count";
    const auto countName = std::search(arrow.begin(), arrow.end(), countTag.begin(), countTag.end());
    test.Check(countName != arrow.end(), "literal charged fixture has its Count tag");
    if (countName != arrow.end()) {
        const size_t offset = static_cast<size_t>(countName - arrow.begin());
        auto empty = arrow;
        empty[offset + 5] = 0;
        test.Check(ParseItemNbt(empty).ok && ParseItemNbt(empty).chargedItemCount == 0,
                   "zero count remains an empty charged stack");
        auto missing = arrow;
        missing.erase(missing.begin() + offset - 3, missing.begin() + offset + 6);
        test.Check(ParseItemNbt(missing).ok && ParseItemNbt(missing).chargedItemCount == 0,
                   "a name alone does not imply a loaded projectile");
        auto wrongType = arrow;
        wrongType[offset - 3] = 2;
        wrongType.insert(wrongType.begin() + offset + 6, 0);
        test.Check(ParseItemNbt(wrongType).ok && ParseItemNbt(wrongType).chargedItemCount == 0,
                   "a non-byte Count is not treated as saved projectile count");
    }
    auto nested = arrow;
    nested.insert(nested.begin() + 3,
                  {10, 5, 0, 'o', 't', 'h', 'e', 'r'});
    nested.push_back(0);
    test.Check(ParseItemNbt(nested).ok && ParseItemNbt(nested).chargedItemName.empty(),
               "a chargedItem inside an unrelated nested compound is not weapon charge state");
    for (size_t length = 0; length < arrow.size(); ++length) {
        const std::vector<uint8_t> truncated(arrow.begin(), arrow.begin() + length);
        const auto value = ParseItemNbt(truncated);
        // The three-byte prefix is itself a complete empty network-NBT root.
        // Every other truncation must fail both representations.
        test.Check((length == 3 || !value.ok) && value.chargedItemName.empty() &&
                       value.chargedItemCount == 0,
                   "truncated item data cannot expose a partially decoded loaded projectile");
    }
    auto trailing = arrow;
    trailing.push_back(1);
    test.Check(!ParseItemNbt(trailing).ok, "the metadata parser consumes exactly one complete NBT root");
    return test.Finish();
}
