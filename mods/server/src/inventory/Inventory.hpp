#pragma once
#include "protocol/Packets.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::server {

    struct ItemStack {
        int16_t networkId = 0;
        uint16_t count = 0;
        int32_t stackId = 0;
        uint16_t damage = 0;
        bool Empty() const { return networkId == 0 || count == 0; }
        bool Same(const ItemStack& o) const { return networkId == o.networkId && damage == o.damage; }
    };

    int MaxStack(int16_t networkId);
    int ArmorSlotFor(int16_t networkId);
    int MaxDurability(int16_t networkId);
    int ArmorPoints(int16_t networkId);
    float ArmorToughness(int16_t networkId);
    float KnockbackResistance(int16_t networkId);

    class PlayerInventory {
    public:
        static constexpr int kMainSlots = 36;
        static constexpr int kHotbar = 9;

        std::array<ItemStack, kMainSlots> main;
        std::array<ItemStack, 4> armor;
        ItemStack offhand;
        ItemStack cursor;
        ItemStack output;
        std::array<ItemStack, 13> craft;
        std::vector<ItemStack> container;
        int containerKind = -1;
        int selected = 0;

        ItemStack* Resolve(proto2225::ContainerSlotType type, int slot);

        const ItemStack& Held() const { return main[static_cast<size_t>(selected)]; }
        ItemStack MakeStack(int16_t networkId, int count);
        int Give(int16_t networkId, int count);
        int Give(const ItemStack& stack);
        bool TakeHeld(int count);
        int32_t NewStackId() { return nextStackId++; }

    private:
        int32_t nextStackId = 1;
    };

    struct RequestResult {
        bool ok = false;
        std::vector<std::pair<proto2225::ContainerSlotType, int>> touched;
        std::vector<ItemStack> dropped;
    };

    RequestResult ApplyStackRequest(PlayerInventory& inv, const proto2225::ItemStackRequest& request,
                                    bool creative);

    proto2225::ItemStackResponsesEntry MakeResponse(PlayerInventory& inv, int32_t requestId,
                                                    const RequestResult& result);

    proto2225::ItemV4 ToWire(const ItemStack& stack);
}
