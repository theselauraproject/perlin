#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace perlin;

    proto::ItemV4 Item(int16_t id, uint16_t count, int32_t stackId) {
        proto::ItemV4 item;
        item.networkId = id;
        item.count = count;
        item.hasStackId = true;
        item.stackId = stackId;
        proto::ItemExtraDataWithoutBlockingTick extra;
        extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
        extra.nbt.emplace();
        extra.nbt->version = 1;
        // An unrelated root tag must survive prediction and rollback verbatim.
        extra.nbt->nbt.bytes = {10, 0, 0, 3, 4, 0, 't', 'e', 's', 't', 42, 0, 0, 0, 0};
        item.extra = std::move(extra);
        return item;
    }

    struct Rig {
        selftest::Checker& check;
        BedrockSession session;
        std::vector<proto::ItemStackRequest> requests;
        std::vector<proto::Transaction> transactions;
        std::vector<uint32_t> packetIds;
        std::function<void()> duringUseSend;

        Rig(selftest::Checker& check, int protocol) : check(check) {
            session.BeginReplayMode("item-use-packets", 7, protocol);
            MovementProtocolTestAccess::SetUniqueId(session, 71);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& bytes) {
                packetIds.push_back(id);
                net::ReadStream stream(bytes.data(), bytes.size());
                if (id == proto::ItemStackRequestPacket::kId) {
                    proto::ItemStackRequestPacket packet;
                    const bool ok = proto::Read(stream, packet) && stream.remaining() == 0;
                    this->check.Check(ok, "outgoing inventory request decodes completely");
                    if (ok)
                        requests.insert(requests.end(), packet.requests.begin(), packet.requests.end());
                } else if (id == proto::InventoryTransactionPacket::kId) {
                    proto::InventoryTransactionPacket packet;
                    const bool ok = proto::Read(stream, packet) && stream.remaining() == 0;
                    this->check.Check(ok, "outgoing use transaction decodes completely");
                    if (ok)
                        transactions.push_back(std::move(packet.transaction));
                    if (duringUseSend)
                        duringUseSend();
                }
            });
            proto::ItemRegistryPacket registry;
            int16_t id = 10;
            for (const char* name : {"minecraft:arrow", "minecraft:saddle", "minecraft:wind_charge",
                                     "minecraft:ender_pearl"}) {
                proto::ItemstatesEntry entry;
                entry.name = name;
                entry.runtimeId = id++;
                registry.itemstates.push_back(std::move(entry));
            }
            Inject(registry);
        }

        ~Rig() { session.SetSentTap({}); }

        template<class Packet> bool Inject(const Packet& packet) {
            const net::context::ScopedSettings context({session.ServerProtocol(), -1});
            std::vector<uint8_t> bytes;
            const bool ok = net::EncodeChecked(packet, bytes);
            check.Check(ok, "incoming item fixture encodes for the selected profile");
            if (ok)
                session.InjectIncoming(Packet::kId, bytes);
            return ok;
        }

        void Slot(int window, int slot, const proto::ItemV4& item,
                  std::optional<proto::ContainerSlotType> type = std::nullopt) {
            proto::InventorySlotPacket packet;
            packet.windowId = static_cast<proto::WindowIDVarint>(window);
            packet.slot = static_cast<uint32_t>(slot);
            packet.item = item;
            if (type) {
                packet.container.emplace();
                packet.container->containerId = *type;
            }
            Inject(packet);
        }

        void Instabuild(bool enabled) {
            proto::UpdateAbilitiesPacket packet;
            packet.entityUniqueId = 71;
            proto::AbilityLayers layer;
            layer.type = proto::AbilityLayersType::Base;
            layer.allowed = 1u << 11;
            layer.enabled = enabled ? 1u << 11 : 0;
            packet.abilities.push_back(layer);
            Inject(packet);
            check.Check(session.Instabuild() == enabled, "the incoming layered ability supplies Instabuild to item use");
        }
    };
}

namespace perlin::selftest {
    int ItemUsePackets() {
        Checker t("item_use_packets_test");
        const float eye[3] = {0, 2, 0};
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            for (int16_t itemId : {int16_t{12}, int16_t{13}}) {
                const std::string name = itemId == 12 ? "minecraft:wind_charge" : "minecraft:ender_pearl";
                const int duration = itemId == 12 ? 10 : 20;
                for (int scenario = 0; scenario < 4; ++scenario) {
                    const bool offhand = scenario == 3;
                    const bool instabuild = scenario == 2;
                    const uint16_t count = scenario == 1 || instabuild ? 1 : 3;
                    Rig rig(t, protocol);
                    rig.Instabuild(instabuild);
                    const auto before = Item(itemId, count, 61);
                    rig.Slot(offhand ? 119 : 0, offhand ? 0 : 4, before);
                    rig.session.SelectHotbarSlot(4);
                    rig.session.SetUseOffhand(offhand);
                    t.Check(rig.session.SendUseItemInAir(eye, false), "immediate throwable air use sends its inventory transaction");
                    t.Check(rig.transactions.size() == 1, "an immediate use produces one complex transaction");
                    if (rig.transactions.empty())
                        continue;
                    const auto& transaction = rig.transactions.back();
                    const auto* use = std::get_if<proto::TransactionUseItem>(&transaction.transactionData);
                    // 944 has no hand field; the facade's default is MainHand. Offhand selection in
                    // that profile is still checked through its real container119 inventory action.
                    const auto wireHand = offhand && protocol >= 2193
                                              ? proto::TransactionUseItemHand::OffHand
                                              : proto::TransactionUseItemHand::MainHand;
                    t.Check(use && use->heldItem.networkId == itemId && use->heldItem.count == count &&
                                use->hotbarSlot == (offhand ? 0 : 4) &&
                                use->hand == wireHand &&
                                use->clientPrediction == proto::TransactionUseItemClientPrediction::Failure &&
                                use->clientCooldownState == proto::TransactionUseItemClientCooldownState::Off,
                            "air-use fields retain the pre-use stack, profile's hand field and native prediction/cooldown states");
                    const int remaining = count - (instabuild ? 0 : 1);
                    const auto inventory = rig.session.GetInventory();
                    t.Check((offhand ? inventory.offhand.count : inventory.hotbar[4].count) == remaining &&
                                rig.session.ItemCooldownTicks(name) == (remaining ? duration : 0),
                            "local use consumes only without Instabuild and starts cooldown only for a surviving stack");
                    t.Check(transaction.actions.size() == (instabuild ? 0u : 1u),
                            "only an actual count change adds an inventory action");
                    if (protocol == 2193)
                        t.Check(use && !use->heldItem.hasStackId && !use->heldItem.stackId,
                                "matched2193 client held-item descriptor omits its network stack ID on the wire");
                    if (!instabuild && !transaction.actions.empty()) {
                        const auto& action = transaction.actions[0];
                        t.Check(action.sourceType == proto::TransactionActionsEntrySourceType::Container &&
                                    action.windowId == (offhand ? 119 : 0) && !action.flags &&
                                    action.slot == (offhand ? 0u : 4u) && action.oldItem.count == count &&
                                    action.newItem.count == remaining &&
                                    action.newItem.networkId == (remaining ? itemId : 0),
                                "the count change is encoded as the exact old/new main-hand or offhand container action");
                        if (protocol == 2193)
                            t.Check(!action.oldItem.hasStackId && !action.oldItem.stackId &&
                                        !action.newItem.hasStackId && !action.newItem.stackId,
                                    "matched2193 client action descriptors omit IDs while raw prediction retains them");
                    }
                    const int32_t requestId = transaction.legacy.legacyRequestId;
                    if (remaining && !instabuild) {
                        const auto& legacy = transaction.legacy.legacyTransactions;
                        t.Check(requestId < 0 && requestId % 2 == 0 && legacy && legacy->size() == 1 &&
                                    (*legacy)[0].containerId == (offhand ? 34 : 29) &&
                                    (*legacy)[0].changedSlots.size() == 1 &&
                                    (*legacy)[0].changedSlots[0].slotId == (offhand ? 0 : 4),
                                "a surviving predicted stack records its negative-even legacy ID and exact changed slot");
                        if (offhand)
                            rig.session.InvClick(BedrockSession::kInvOffhand, false);
                        else
                            rig.session.DropInventorySlot(4, false);
                        t.Check(!rig.requests.empty() && !rig.requests.back().actions.empty() &&
                                    rig.requests.back().actions[0].source.stackId == requestId,
                                "the next ordinary inventory request references the surviving legacy-predicted stack");
                    } else {
                        t.Check(requestId == 0 && !transaction.legacy.legacyTransactions,
                                "an empty resulting stack or unchanged Instabuild use has no legacy changed-slot metadata");
                    }
                    t.Check(std::find(rig.packetIds.begin(), rig.packetIds.end(),
                                      proto::ClientStartItemCooldownPacket::kId) == rig.packetIds.end(),
                            "local cooldown prediction does not echo a server-only cooldown packet");
                }
            }
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            rig.Slot(0, 0, Item(13, 4, 41));
            t.Check(rig.session.SendUseItemInAir(eye, false), "initial pearl use starts prediction");
            const int32_t firstId = rig.transactions.back().legacy.legacyRequestId;
            t.Check(rig.session.SendUseItemInAir(eye, false) && rig.transactions.back().actions.empty() &&
                        rig.transactions.back().legacy.legacyRequestId == 0 &&
                        rig.session.GetInventory().hotbar[0].count == 3 &&
                        rig.session.ItemCooldownTicks("minecraft:ender_pearl") == 20,
                    "a cooldown-blocked attempt is still sent but neither consumes nor resets cooldown");
            rig.Slot(0, 1, Item(10, 1, 42));
            rig.session.SelectHotbarSlot(1);
            rig.session.SendUseItemInAir(eye, false);
            rig.Inject(proto::ClientStartItemCooldownPacket{"ender_pearl", 0});
            rig.session.SelectHotbarSlot(0);
            rig.session.SendUseItemInAir(eye, false);
            t.Check(rig.transactions.back().legacy.legacyRequestId == firstId - 6,
                    "blocked and non-consuming ordinary air calls both advance the process-wide legacy scope counter");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            proto::StartGamePacket start;
            start.runtimeEntityId = 7;
            start.entityId = 71;
            start.serverAuthoritativeInventory = false;
            rig.Inject(start);
            rig.Slot(0, 0, Item(13, 3, 61));
            t.Check(rig.session.SendUseItemInAir(eye, false) && !rig.transactions.empty() &&
                        rig.transactions.back().actions.size() == 1 &&
                        rig.transactions.back().legacy.legacyRequestId < 0 &&
                        rig.transactions.back().legacy.legacyRequestId % 2 == 0,
                    "ordinary legacy-inventory mode still decorates a surviving predicted stack with a legacy request ID");
        }
        for (int correction = 0; correction < 6; ++correction) {
            Rig rig(t, 2193);
            const auto original = Item(13, 3, 51);
            rig.Slot(0, 4, original);
            rig.session.SelectHotbarSlot(4);
            rig.duringUseSend = [&] {
                switch (correction) {
                case 0: rig.Slot(0, 4, Item(13, 5, 72)); break;
                case 1: rig.Slot(0, 4, original); break;
                case 2: rig.Slot(0, 9, Item(10, 8, 73)); break;
                case 3: rig.Inject(proto::ClientStartItemCooldownPacket{"minecraft:ender_pearl", 7}); break;
                case 4: rig.Inject(proto::ClientStartItemCooldownPacket{"wind_charge", 7}); break;
                case 5: rig.session.BeginReplayMode("new-use-world", 7, 2193); break;
                }
            };
            t.Check(rig.session.SendUseItemInAir(eye, false), "use can receive an authoritative update during its send callback");
            rig.duringUseSend = {};
            const int expectedCount = correction == 0 ? 5 : correction == 1 || correction == 5 ? 3 : 2;
            const int expectedCooldown = correction == 3 ? 7 : correction == 5 ? 0 : 20;
            t.Check(rig.session.GetInventory().hotbar[4].count == expectedCount &&
                        rig.session.ItemCooldownTicks("minecraft:ender_pearl") == expectedCooldown,
                    "server slot/category/reset updates win without unrelated updates discarding valid local prediction");
        }
        {
            Rig rig(t, 2193);
            rig.Slot(0, 0, Item(13, 3, 81));
            MovementProtocolTestAccess::ReplayTransport(rig.session, false);
            t.Check(!rig.session.SendUseItemInAir(eye, false) &&
                        rig.session.GetInventory().hotbar[0].count == 3 &&
                        rig.session.ItemCooldownTicks("minecraft:ender_pearl") == 0,
                    "a failed send does not commit a throwable decrement or cooldown");
            MovementProtocolTestAccess::ReplayTransport(rig.session, true);
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            proto::EntityEventPacket event;
            event.eventId = proto::EntityEventPacketEventId::ChargedItem;
            event.runtimeEntityId = 8;
            rig.Inject(event);
            t.Check(!rig.session.TakeChargedItemUseReset(),
                    "another actor's charge completion cannot reset local item use");
            event.runtimeEntityId = 7;
            rig.Inject(event);
            t.Check(rig.session.TakeChargedItemUseReset() && !rig.session.TakeChargedItemUseReset(),
                    "local charge completion is a consume-once input reset event");
            rig.Inject(event);
            rig.session.BeginReplayMode("next-item-session", 0, protocol);
            t.Check(!rig.session.TakeChargedItemUseReset(), "a new replay session clears an old charge-completion event");
            event.runtimeEntityId = 0;
            rig.Inject(event);
            t.Check(!rig.session.TakeChargedItemUseReset(),
                    "charge completion before an established local player cannot reset input");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            proto::ContainerOpenPacket open;
            open.windowId = static_cast<proto::WindowID>(1);
            open.windowType = proto::WindowType::Horse;
            rig.Inject(open);
            const auto saddle = Item(11, 1, 51);
            rig.Slot(1, 0, saddle, proto::ContainerSlotType::HorseEquip);
            rig.Slot(0, 0, Item(10, 3, 27));
            t.Check(rig.session.GetInventory().horse[0].count == 1 &&
                        rig.session.GetInventory().hotbar[0].count == 3,
                    "horse equipment and held stack are present before drop prediction");
            t.Check(rig.session.SendDropSelected(1), "dropping a selected nonempty stack sends a request");
            for (size_t attempt = 0; attempt < 3; ++attempt) {
                t.Check(rig.requests.size() == attempt + 1,
                        "a rejected drop retries each supported address form once");
                if (rig.requests.size() <= attempt)
                    break;
                proto::ItemStackResponsePacket reject;
                proto::ItemStackResponsesEntry response;
                response.status = proto::ItemStackResponsesEntryStatus::Error;
                response.requestId = rig.requests[attempt].requestId;
                reject.responses.push_back(response);
                rig.Inject(reject);
            }
            t.Check(rig.session.GetInventory().hotbar[0].count == 3 &&
                        rig.session.GetInventory().horse[0].count == 1,
                    "rejected drops restore the selected stack and preserve visible horse equipment");
            const size_t before = rig.requests.size();
            rig.session.InvClick(BedrockSession::kInvHorse0, false);
            t.Check(rig.requests.size() == before + 1 && !rig.requests.back().actions.empty() &&
                        rig.requests.back().actions[0].source.stackId == 51,
                    "rejected drops preserve the horse's raw item and stack ID for its next real request");
            rig.session.InvClick(0, false);
            const float eye[3] = {0, 2, 0};
            t.Check(rig.session.SendUseItemInAir(eye, false), "the restored horse item can be inspected on the use wire path");
            const proto::TransactionUseItem* use = rig.transactions.empty()
                                                       ? nullptr
                                                       : std::get_if<proto::TransactionUseItem>(
                                                             &rig.transactions.back().transactionData);
            const auto* extra = use ? std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&use->heldItem.extra)
                                    : nullptr;
            t.Check(use && use->heldItem.networkId == 11 && use->heldItem.count == 1 && extra && extra->nbt &&
                        extra->nbt->nbt.bytes == std::get<proto::ItemExtraDataWithoutBlockingTick>(saddle.extra).nbt->nbt.bytes,
                    "drop rollback preserves unrelated horse NBT through later selection and item use");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(t, protocol);
            rig.Slot(0, 0, Item(12, 40, 71));
            rig.Slot(0, 1, Item(12, 30, 72));
            rig.session.InvClick(0, false);
            rig.session.InvClick(1, false);
            const auto view = rig.session.GetInventory();
            t.Check(view.hotbar[0].Empty() && view.hotbar[1].count == 64 && view.cursor.count == 6,
                    "wind charges merge to their native stack limit of64 and retain the excess on the cursor");
            t.Check(rig.requests.size() == 2 && rig.requests.back().actions.size() == 1 &&
                        rig.requests.back().actions[0].count == 34 &&
                        rig.requests.back().actions[0].source.stackId == 71 &&
                        rig.requests.back().actions[0].destination.stackId == 72,
                    "wind-charge merge sends the exact count and original source/destination stack IDs");
            rig.Slot(0, 1, Item(12, 32, 99));
            rig.session.InvClick(1, true);
            t.Check(rig.session.GetInventory().hotbar[1].count == 33 &&
                        rig.session.GetInventory().cursor.count == 5 && rig.requests.size() == 3 &&
                        rig.requests.back().actions[0].destination.stackId == 99,
                    "a server slot correction supplies the count and stack ID used by the next inventory action");
        }
        return t.Finish();
    }
}
