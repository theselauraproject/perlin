#include "client/selftest/SelfTests.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "network/Stream.hpp"

#include <cstdio>

namespace {
    int failures = 0;

    void Check(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    template<class Packet> void Inject(perlin::BedrockSession& session, const Packet& packet) {
        session.InjectIncoming(Packet::kId, perlin::net::Encode(packet));
    }

    perlin::proto::ItemV4 ArmorItem(int16_t networkId, int32_t stackId, uint8_t damage) {
        using namespace perlin::proto;
        ItemV4 item;
        item.networkId = networkId;
        item.count = 1;
        item.hasStackId = true;
        item.stackId = stackId;
        ItemExtraDataWithoutBlockingTick extra;
        extra.hasNbt = ItemExtraDataWithoutBlockingTickHasNbt::True;
        extra.nbt.emplace();
        extra.nbt->version = 1;
        extra.nbt->nbt.bytes = {
            10, 0, 0,
            3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', damage, 0, 0, 0,
            10, 7, 0, 'd', 'i', 's', 'p', 'l', 'a', 'y',
            8, 4, 0, 'N', 'a', 'm', 'e', 5, 0, 'a', 'r', 'm', 'o', 'r', 0, 0};
        extra.canDestroy = {"minecraft:stone"};
        item.extra = std::move(extra);
        return item;
    }

    struct Rig {
        perlin::BedrockSession session;
        std::vector<perlin::proto::ItemStackRequest> requests;
        std::vector<perlin::proto::TransactionUseItem> uses;
        int sends = 0;

        Rig() {
            using namespace perlin;
            session.BeginReplayMode("armor", 7, kBedrockProtocol);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& body) {
                using namespace proto;
                ++sends;
                net::ReadStream stream(body.data(), body.size());
                if (id == ItemStackRequestPacket::kId) {
                    ItemStackRequestPacket packet;
                    Check(Read(stream, packet) && stream.remaining() == 0, "armor move request encodes fully");
                    requests.insert(requests.end(), packet.requests.begin(), packet.requests.end());
                } else if (id == InventoryTransactionPacket::kId) {
                    InventoryTransactionPacket packet;
                    Check(Read(stream, packet) && stream.remaining() == 0, "armor use transaction encodes fully");
                    if (const auto* use = std::get_if<TransactionUseItem>(&packet.transaction.transactionData))
                        uses.push_back(*use);
                }
            });
            proto::ItemRegistryPacket registry;
            const char* names[]{"minecraft:iron_helmet", "minecraft:iron_chestplate",
                                "minecraft:iron_leggings", "minecraft:iron_boots"};
            for (int slot = 0; slot < 4; slot++) {
                proto::ItemstatesEntry item;
                item.name = names[slot];
                item.runtimeId = static_cast<int16_t>(10 + slot);
                registry.itemstates.push_back(std::move(item));
            }
            Inject(session, registry);
        }

        ~Rig() { session.SetSentTap({}); }

        void Equip(uint64_t runtimeId = 7) {
            perlin::proto::MobArmorEquipmentPacket packet;
            packet.runtimeEntityId = runtimeId;
            packet.helmet = ArmorItem(10, 101, 7);
            packet.chestplate = ArmorItem(11, 102, 8);
            packet.leggings = ArmorItem(12, 103, 9);
            packet.boots = ArmorItem(13, 104, 10);
            Inject(session, packet);
        }

        void Damage(int slot, int16_t value) {
            perlin::proto::PlayerArmorDamagePacket packet;
            perlin::proto::ArmorDamageEntry entry;
            entry.armorSlot = static_cast<perlin::proto::ArmorDamageEntryArmorSlot>(slot);
            entry.damage = value;
            packet.entries.push_back(entry);
            Inject(session, packet);
        }
    };
}

namespace perlin::selftest {
    int ArmorPackets() {
        failures = 0;
        {
            Rig rig;
            rig.Equip();
            Check(rig.session.GetInventory().armor[1].damage == 8, "equipment packet installs armor NBT");
            const int sendsBeforeDamage = rig.sends;
            rig.Damage(1, 35);
            rig.Damage(1, 35);
            rig.Damage(0, 12);
            Check(rig.session.GetInventory().armor[0].damage == 12 &&
                      rig.session.GetInventory().armor[1].damage == 35 &&
                      rig.session.GetInventory().armor[2].damage == 9,
                  "authoritative armor damage replaces the selected slots without accumulation");
            Check(rig.sends == sendsBeforeDamage, "receiving authoritative armor damage sends no echo");

            rig.session.InjectIncoming(proto::PlayerArmorDamagePacket::kId, {1, 2});
            Check(rig.session.GetInventory().armor[1].damage == 35,
                  "truncated armor update does not change equipment");

            proto::InventorySlotPacket offhand;
            offhand.windowId = proto::WindowIDVarint::Offhand;
            offhand.item = ArmorItem(10, 199, 21);
            Inject(rig.session, offhand);
            rig.Damage(4, 70);
            rig.Damage(-1, 70);
            rig.Damage(255, 70);
            Check(rig.session.GetInventory().offhand.damage == 21 &&
                      rig.session.GetInventory().armor[0].damage == 12,
                  "body and invalid armor slots cannot alias offhand or another player slot");
            rig.Equip(999);
            Check(rig.session.GetInventory().armor[1].damage == 35,
                  "another actor's equipment does not replace local armor");

            rig.session.InvClick(BedrockSession::kInvArmor0 + 1, false);
            rig.session.InvClick(0, false);
            Check(rig.requests.size() == 2 && rig.requests[0].actions.size() == 1 &&
                      rig.requests[0].actions[0].source.slotType.containerId == proto::ContainerSlotType::Armor &&
                      rig.requests[0].actions[0].source.slot == 1 &&
                      rig.requests[0].actions[0].source.stackId == 102,
                  "moving equipment uses the raw network stack installed by MobArmorEquipment");
            const float eye[]{0, 65, 0};
            Check(rig.session.SendUseItemInAir(eye, false), "moved armor can participate in a later transaction");
            Check(rig.uses.size() == 1, "later item-use transaction is captured");
            if (rig.uses.size() == 1) {
                const auto& held = rig.uses[0].heldItem;
                const auto* extra = std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&held.extra);
                Check(held.networkId == 11 && held.count == 1 && held.hasStackId && held.stackId == 102,
                      "armor update preserves identity and network stack ID in subsequent wire data");
                Check(extra && extra->nbt && ParseItemNbt(extra->nbt->nbt.bytes).damage == 35 &&
                          ParseItemNbt(extra->nbt->nbt.bytes).customName == "armor" &&
                          extra->canDestroy == std::vector<std::string>{"minecraft:stone"},
                      "subsequent transaction carries corrected damage and unrelated NBT intact");
            }
        }
        {
            Rig rig;
            rig.Equip();
            rig.Damage(0, 0);
            Check(rig.session.GetInventory().armor[0].damage == 0 &&
                      rig.session.GetInventory().armor[0].count == 1,
                  "zero absolute damage repairs armor without deleting it");
            rig.Damage(0, 32767);
            Check(rig.session.GetInventory().armor[0].damage == 32767 &&
                      rig.session.GetInventory().armor[0].count == 1,
                  "absolute armor update does not synthesize item breakage at a local durability limit");
            proto::MobArmorEquipmentPacket clear;
            clear.runtimeEntityId = 7;
            Inject(rig.session, clear);
            rig.Damage(0, 5);
            Check(rig.session.GetInventory().armor[0].Empty(), "damage does not populate an empty armor slot");
            rig.session.InvClick(BedrockSession::kInvArmor0, false);
            Check(rig.requests.empty(), "clearing equipment clears its raw inventory stack too");
        }
        {
            Rig rig;
            rig.Equip();
            proto::StartGamePacket start;
            start.runtimeEntityId = 7;
            start.serverAuthoritativeInventory = false;
            Inject(rig.session, start);
            rig.Damage(0, 88);
            Check(rig.session.GetInventory().armor[0].damage == 7,
                  "authoritative armor update is inactive in legacy inventory mode");
            start.serverAuthoritativeInventory = true;
            Inject(rig.session, start);
            rig.Damage(0, 88);
            Check(rig.session.GetInventory().armor[0].damage == 88,
                  "enabling stack-request inventory enables authoritative armor updates");
        }
        if (failures == 0)
            std::puts("armor_packets_test: ok");
        return failures == 0 ? 0 : 1;
    }
}
