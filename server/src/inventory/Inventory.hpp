#pragma once
#include "protocol/Packets.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::server {

    // blocks only for now: identity is the item id, nothing else varies between two stacks of it
    struct ItemStack {
        int16_t networkId = 0;
        uint16_t count = 0;
        // server assigned; the client quotes it back so a stale request can be told from a current one
        int32_t stackId = 0;
        bool Empty() const { return networkId == 0 || count == 0; }
        bool Same(const ItemStack& o) const { return networkId == o.networkId; }
    };

    int MaxStack(int16_t networkId);
    // the armor slot an item is worn in (0 head, 1 chest, 2 legs, 3 feet), -1 when it is not wearable
    int ArmorSlotFor(int16_t networkId);

    class PlayerInventory {
    public:
        static constexpr int kMainSlots = 36;
        static constexpr int kHotbar = 9;

        std::array<ItemStack, kMainSlots> main;
        std::array<ItemStack, 4> armor;
        ItemStack offhand;
        ItemStack cursor;
        // creative menu result, only lives for the length of one request
        ItemStack output;
        int selected = 0;

        // null when the client names a container this server does not model
        ItemStack* Resolve(proto2225::ContainerSlotType type, int slot);

        const ItemStack& Held() const { return main[static_cast<size_t>(selected)]; }
        ItemStack MakeStack(int16_t networkId, int count);
        // first stacks that already hold the item, then empty slots, hotbar first; the count that did not fit
        int Give(int16_t networkId, int count);
        // takes from the held slot; false if it holds less
        bool TakeHeld(int count);
        int32_t NewStackId() { return nextStackId++; }

    private:
        int32_t nextStackId = 1;
    };

    struct RequestResult {
        bool ok = false;
        // the containers and slots a response must report, as the client addressed them
        std::vector<std::pair<proto2225::ContainerSlotType, int>> touched;
        // stacks the request threw out of the inventory; the caller puts them in the world
        std::vector<ItemStack> dropped;
    };

    // runs every action of a request against `inv`; on failure `inv` is left as it was
    RequestResult ApplyStackRequest(PlayerInventory& inv, const proto2225::ItemStackRequest& request,
                                    bool creative);

    proto2225::ItemStackResponsesEntry MakeResponse(PlayerInventory& inv, int32_t requestId,
                                                    const RequestResult& result);

    proto2225::ItemV4 ToWire(const ItemStack& stack);
}
