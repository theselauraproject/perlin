#include "client/systems/multiplayer/InventorySlots.hpp"

#include <cstdio>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

int main() {
    using C = proto::ContainerSlotType;
    using inventory::ResponseAddress;
    const inventory::ContainerState closed;
    const inventory::ContainerState chest{true, false, -1};
    const inventory::ContainerState horse{false, true, -1};
    CHECK(ResponseAddress(C::Hotbar, 0, closed) == 0);
    CHECK(ResponseAddress(C::Hotbar, 8, closed) == 8);
    CHECK(ResponseAddress(C::Hotbar, 9, closed) == -1);
    CHECK(ResponseAddress(C::Hotbar, 40, closed) == -1);
    CHECK(ResponseAddress(C::HotbarAndInventory, 35, closed) == 35);
    CHECK(ResponseAddress(C::HotbarAndInventory, 36, closed) == -1);
    CHECK(ResponseAddress(C::Inventory, 0, closed) == 9);
    CHECK(ResponseAddress(C::Inventory, 26, closed) == 35);
    CHECK(ResponseAddress(C::Inventory, 35, closed) == 35);
    CHECK(ResponseAddress(C::Inventory, 36, closed) == -1);
    CHECK(ResponseAddress(C::Armor, 0, closed) == inventory::kArmor0);
    CHECK(ResponseAddress(C::Armor, 3, closed) == inventory::kArmor0 + 3);
    CHECK(ResponseAddress(C::Armor, 4, closed) == -1);
    CHECK(ResponseAddress(C::Armor, 255, closed) == -1);
    CHECK(ResponseAddress(C::Offhand, 0, closed) == inventory::kOffhand);
    CHECK(ResponseAddress(C::Offhand, 1, closed) == inventory::kOffhand);
    CHECK(ResponseAddress(C::Offhand, 2, closed) == -1);
    CHECK(ResponseAddress(C::Cursor, 0, closed) == inventory::kCursor);
    CHECK(ResponseAddress(C::Cursor, 1, closed) == -1);
    CHECK(ResponseAddress(C::CraftingInput, 27, closed) == -1);
    CHECK(ResponseAddress(C::CraftingInput, 28, closed) == inventory::kCraft0);
    CHECK(ResponseAddress(C::CraftingInput, 31, closed) == inventory::kCraft0 + 3);
    CHECK(ResponseAddress(C::CraftingInput, 32, closed) == inventory::kWork0);
    CHECK(ResponseAddress(C::CraftingInput, 40, closed) == inventory::kWork0 + 8);
    CHECK(ResponseAddress(C::CraftingInput, 41, closed) == -1);
    CHECK(ResponseAddress(C::CreativeOutput, 50, closed) == inventory::kOutput);

    for (const C type : {C::Container, C::Shulker, C::Barrel}) {
        CHECK(ResponseAddress(type, 0, closed) == -1);
        for (int slot = 0; slot < inventory::kChestCount; ++slot)
            CHECK(ResponseAddress(type, slot, chest) == inventory::kChest0 + slot);
        CHECK(ResponseAddress(type, inventory::kChestCount, chest) == -1);
    }
    CHECK(ResponseAddress(C::HorseEquip, 0, closed) == -1);
    CHECK(ResponseAddress(C::HorseEquip, 0, horse) == inventory::kHorse0);
    CHECK(ResponseAddress(C::HorseEquip, 1, horse) == inventory::kHorse0 + 1);
    CHECK(ResponseAddress(C::HorseEquip, 2, horse) == -1);
    for (int slot = 0; slot < inventory::kHorseCount - 2; ++slot)
        CHECK(ResponseAddress(C::Container, slot, horse) == inventory::kHorse0 + 2 + slot);
    CHECK(ResponseAddress(C::Container, inventory::kHorseCount - 2, horse) == -1);

    for (const C type : {C::FurnaceIngredient, C::BlastFurnaceIngredient, C::SmokerIngredient}) {
        CHECK(ResponseAddress(type, 0, closed) == -1);
        for (int kind = 0; kind < 3; ++kind) {
            const inventory::ContainerState furnace{false, false, kind};
            CHECK(ResponseAddress(type, 0, furnace) == inventory::kChest0);
            CHECK(ResponseAddress(type, 1, furnace) == -1);
            CHECK(ResponseAddress(C::FurnaceFuel, 1, furnace) == inventory::kChest0 + 1);
            CHECK(ResponseAddress(C::FurnaceOutput, 2, furnace) == inventory::kChest0 + 2);
            CHECK(ResponseAddress(C::FurnaceFuel, 0, furnace) == -1);
            CHECK(ResponseAddress(C::FurnaceOutput, 0, furnace) == -1);
        }
    }
    CHECK(ResponseAddress(C::FurnaceFuel, 1, closed) == -1);
    CHECK(ResponseAddress(C::FurnaceOutput, 2, closed) == -1);
    CHECK(ResponseAddress(static_cast<C>(255), 0, chest) == -1);
    CHECK(ResponseAddress(C::Hotbar, -1, closed) == -1);
    CHECK(ResponseAddress(C::Hotbar, 256, closed) == -1);
    std::printf(failures ? "%d inventory slot checks failed\n" : "inventory_slots_selftest: ok\n", failures);
    return failures ? 1 : 0;
}
