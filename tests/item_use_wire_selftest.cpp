#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <vector>

namespace {
    using namespace perlin;
    int checks = 0, failures = 0;
    void Check(bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL item-use wire: %s\n", message);
        }
    }
    bool Decode(const std::vector<uint8_t>& bytes, proto::InventoryTransactionPacket& packet) {
        net::ReadStream stream(bytes.data(), bytes.size());
        return proto::Read(stream, packet) && stream.remaining() == 0;
    }
}

int main() {
    using namespace perlin;
    using Source = proto::TransactionActionsEntrySourceType;
    struct WindowCase {
        int8_t window;
        std::vector<uint8_t> bytes;
    };
    // Literal ZigZag32 window bytes from the old source-tagged action layout. Both items are empty.
    const std::array windows{
        WindowCase{0, {0}}, WindowCase{119, {0xee, 1}}, WindowCase{120, {0xf0, 1}},
        WindowCase{127, {0xfe, 1}}, WindowCase{-100, {0xc7, 1}}, WindowCase{-128, {0xff, 1}},
    };
    for (int protocol : {944, 975}) {
        const net::context::ScopedSettings context({protocol, -1});
        for (const auto& test : windows) {
            std::vector<uint8_t> bytes{0, 0, 1, 0}; // legacy0, normal, one Container action
            bytes.insert(bytes.end(), test.bytes.begin(), test.bytes.end());
            bytes.insert(bytes.end(), {4, 0, 0}); // slot4, old air, new air
            proto::InventoryTransactionPacket packet;
            const bool decoded = Decode(bytes, packet);
            Check(decoded && packet.transaction.actions.size() == 1, "literal container action decodes completely");
            if (!decoded || packet.transaction.actions.size() != 1)
                continue;
            const auto& action = packet.transaction.actions.front();
            Check(action.sourceType == Source::Container && action.windowId == test.window &&
                      !action.flags && action.slot == 4,
                  "container address survives the inventoryId/windowId bridge without invented flags");
            Check(!packet.transaction.legacy.legacyTransactions,
                  "zero legacy request ID preserves absent changed-slot metadata");
            proto::InventoryTransactionPacket outgoing;
            auto& sent = outgoing.transaction.actions.emplace_back();
            sent.sourceType = Source::Container;
            sent.windowId = test.window;
            sent.slot = 4;
            Check(net::Encode(outgoing) == bytes, "outgoing container action matches independently specified wire bytes");
        }
        for (uint8_t flags : {uint8_t{0}, uint8_t{5}}) {
            const std::vector<uint8_t> bytes{0, 0, 1, 2, flags, 4, 0, 0};
            proto::InventoryTransactionPacket packet;
            const bool decoded = Decode(bytes, packet);
            Check(decoded && packet.transaction.actions.size() == 1, "literal WorldInteraction action decodes");
            if (!decoded || packet.transaction.actions.size() != 1)
                continue;
            Check(!packet.transaction.actions[0].windowId && packet.transaction.actions[0].flags == flags,
                  "world-action flags are present even when zero, without a container address");
            proto::InventoryTransactionPacket outgoing;
            auto& action = outgoing.transaction.actions.emplace_back();
            action.sourceType = Source::WorldInteraction;
            action.flags = flags;
            action.slot = 4;
            Check(net::Encode(outgoing) == bytes, "outgoing world-action flag bytes remain unchanged");
        }
        for (uint8_t source : {uint8_t{1}, uint8_t{3}}) {
            const std::vector<uint8_t> bytes{0, 0, 1, source, 4, 0, 0};
            proto::InventoryTransactionPacket packet;
            const bool decoded = Decode(bytes, packet);
            Check(decoded && packet.transaction.actions.size() == 1, "literal source without source-specific fields decodes");
            if (decoded && packet.transaction.actions.size() == 1)
                Check(!packet.transaction.actions[0].windowId && !packet.transaction.actions[0].flags,
                      "Global/Creative actions do not invent optional source fields");
        }
        for (const std::vector<uint8_t>& bytes : {
                 std::vector<uint8_t>{7, 0, 0, 0},
                 std::vector<uint8_t>{7, 1, 34, 1, 0, 0, 0}}) {
            proto::InventoryTransactionPacket packet;
            const bool decoded = Decode(bytes, packet);
            Check(decoded && packet.transaction.legacy.legacyRequestId == -4 &&
                      packet.transaction.legacy.legacyTransactions.has_value(),
                  "nonzero request IDs preserve present empty and nonempty changed-slot lists");
            if (decoded)
                Check(net::Encode(packet) == bytes, "negative-even changed-slot lists keep their literal wire representation");
        }
    }
    // The old TransactionUseItem has no hand byte. Keeping this separate from current-profile
    // expectations prevents a facade default from being mistaken for a wire-supported hand field.
    {
        const net::context::ScopedSettings context({944, -1});
        std::vector<uint8_t> bytes{0, 2, 0, 1, 0, 0, 0, 0, 0xfe, 3, 0, 0};
        bytes.resize(bytes.size() + 24 + 3, 0); // player/click float3, block ID, prediction, cooldown
        proto::InventoryTransactionPacket packet;
        auto& transaction = packet.transaction;
        transaction.transactionType = proto::TransactionTransactionType::ItemUse;
        auto& use = transaction.transactionData.emplace<proto::TransactionUseItem>();
        use.actionType = proto::TransactionUseItemActionType::ClickAir;
        use.face = 255;
        use.hand = proto::TransactionUseItemHand::OffHand;
        Check(net::Encode(packet) == bytes, "944 air use has no invented hand byte");
        proto::InventoryTransactionPacket received;
        Check(Decode(bytes, received), "literal old-profile air use is fully consumed");
    }
    std::printf("item_use_wire_selftest: %s (%d checks, %d failures)\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
