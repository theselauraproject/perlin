#pragma once
#include "protocol/Types.hpp"

namespace perlin::inventory {

    inline constexpr int kArmor0 = 36, kOffhand = 40, kCraft0 = 41, kOutput = 45,
                         kCursor = 46, kWork0 = 47, kPlayerSlots = 56, kHorse0 = 56,
                         kHorseCount = 17, kChest0 = 73, kChestCount = 54, kTotal = 127;

    struct ContainerState {
        bool chestOpen = false;
        bool horseOpen = false;
        int furnaceKind = -1;
    };

    constexpr int ResponseAddress(proto::ContainerSlotType type, int slot, const ContainerState& state) {
        using C = proto::ContainerSlotType;
        if (slot < 0 || slot > 255)
            return -1;
        switch (type) {
        case C::HotbarAndInventory:
            return slot < 36 ? slot : -1;
        case C::Hotbar:
            return slot < 9 ? slot : -1;
        case C::Inventory:
            return slot < 27 ? slot + 9 : (slot < 36 ? slot : -1);
        case C::Armor:
            return slot < 4 ? kArmor0 + slot : -1;
        case C::Offhand:
            return slot <= 1 ? kOffhand : -1;
        case C::CraftingInput:
            if (slot >= 28 && slot <= 31)
                return kCraft0 + slot - 28;
            return slot >= 32 && slot <= 40 ? kWork0 + slot - 32 : -1;
        case C::CraftingOutput:
        case C::CreativeOutput:
            return kOutput;
        case C::Cursor:
            return slot == 0 ? kCursor : -1;
        case C::FurnaceIngredient:
        case C::BlastFurnaceIngredient:
        case C::SmokerIngredient:
            return state.furnaceKind >= 0 && slot == 0 ? kChest0 : -1;
        case C::FurnaceFuel:
            return state.furnaceKind >= 0 && slot == 1 ? kChest0 + 1 : -1;
        case C::FurnaceOutput:
            return state.furnaceKind >= 0 && slot == 2 ? kChest0 + 2 : -1;
        case C::HorseEquip:
            return state.horseOpen && slot < 2 ? kHorse0 + slot : -1;
        case C::Container:
            if (state.horseOpen)
                return slot < kHorseCount - 2 ? kHorse0 + 2 + slot : -1;
            [[fallthrough]];
        case C::Shulker:
        case C::Barrel:
            return state.chestOpen && slot < kChestCount ? kChest0 + slot : -1;
        default:
            return -1;
        }
    }

}
