#include "client/selftest/Check.hpp"
#include "client/systems/player/AmmoRules.hpp"
#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "client/systems/blocks/BlockUse.hpp"
#include "network/Stream.hpp"

#include <string_view>
#include <optional>
#include <vector>

namespace {
    using namespace perlin;

    proto::ItemV4 Item(int16_t id, int32_t stackId, std::string_view projectile = {}, uint8_t count = 1) {
        proto::ItemV4 item;
        item.networkId = id;
        item.count = count;
        item.hasStackId = true;
        item.stackId = stackId;
        proto::ItemExtraDataWithoutBlockingTick extra;
        if (!projectile.empty()) {
            extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
            extra.nbt.emplace();
            extra.nbt->version = 1;
            auto& bytes = extra.nbt->nbt.bytes;
            bytes = {10, 0, 0, 10, 11, 0, 'c', 'h', 'a', 'r', 'g', 'e', 'd', 'I', 't', 'e', 'm',
                     8, 4, 0, 'N', 'a', 'm', 'e', static_cast<uint8_t>(projectile.size()), 0};
            bytes.insert(bytes.end(), projectile.begin(), projectile.end());
            bytes.insert(bytes.end(), {1, 5, 0, 'C', 'o', 'u', 'n', 't', 1,
                                       2, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 5, 0, 0, 0});
        }
        item.extra = std::move(extra);
        return item;
    }

    proto::ItemV4 EnchantedItem(int16_t id, int32_t stackId, int16_t enchant, int16_t level,
                               std::optional<int32_t> damage = std::nullopt,
                               bool stored = false, uint8_t damageType = 3,
                               std::optional<int16_t> secondLevel = std::nullopt) {
        auto item = Item(id, stackId);
        proto::ItemExtraDataWithoutBlockingTick extra;
        extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
        extra.nbt.emplace();
        extra.nbt->version = 1;
        auto& bytes = extra.nbt->nbt.bytes;
        bytes.reserve(128);
        bytes = {10, 0, 0, 9};
        const std::string_view name = stored ? "StoredEnchantments" : "ench";
        bytes.push_back(static_cast<uint8_t>(name.size()));
        bytes.push_back(0);
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.insert(bytes.end(), {10, static_cast<uint8_t>(secondLevel ? 2 : 1), 0, 0, 0});
        const auto appendEnchant = [&](int16_t value) {
            bytes.insert(bytes.end(), {2, 2, 0, 'i', 'd',
                                       static_cast<uint8_t>(enchant), static_cast<uint8_t>(enchant >> 8),
                                       2, 3, 0, 'l', 'v', 'l',
                                       static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), 0});
        };
        appendEnchant(level);
        if (secondLevel)
            appendEnchant(*secondLevel);
        if (damage) {
            bytes.insert(bytes.end(), {damageType, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e'});
            for (int shift = 0; shift < (damageType == 2 ? 16 : 32); shift += 8)
                bytes.push_back(static_cast<uint8_t>(static_cast<uint32_t>(*damage) >> shift));
        }
        bytes.push_back(0);
        item.extra = std::move(extra);
        return item;
    }

    struct Rig {
        BedrockSession session;
        ui::HudState hud;
        std::vector<proto::TransactionUseItem> sentUses;
        bool decoded = true;

        explicit Rig(int protocol) {
            session.BeginReplayMode("ranged-items", 7, protocol);
            hud.mode = ui::GameMode::Survival;
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& body) {
                if (id != proto::InventoryTransactionPacket::kId)
                    return;
                proto::InventoryTransactionPacket packet;
                net::ReadStream stream(body.data(), body.size());
                decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                if (const auto* use = std::get_if<proto::TransactionUseItem>(&packet.transaction.transactionData))
                    sentUses.push_back(*use);
            });
            proto::ItemRegistryPacket registry;
            int16_t id = 10;
            for (const char* name : {"minecraft:crossbow", "minecraft:arrow",
                                     "minecraft:firework_rocket", "minecraft:bow", "minecraft:trident",
                                     "test:bolt", "minecraft:stone"}) {
                proto::ItemstatesEntry entry;
                entry.name = name;
                entry.runtimeId = id++;
                registry.itemstates.push_back(std::move(entry));
            }
            proto::ItemstatesEntry air;
            air.name = "minecraft:air";
            air.runtimeId = 0;
            registry.itemstates.push_back(std::move(air));
            Inject(registry);
        }

        ~Rig() { session.SetSentTap({}); }

        template<class Packet> void Inject(const Packet& packet) {
            const net::context::ScopedSettings context({session.ServerProtocol(), -1});
            session.InjectIncoming(Packet::kId, net::Encode(packet));
        }

        void Slot(uint32_t slot, const proto::ItemV4& item, bool offhand = false) {
            proto::InventorySlotPacket packet;
            packet.windowId = offhand ? proto::WindowIDVarint::Offhand : proto::WindowIDVarint::Inventory;
            packet.slot = slot;
            packet.item = item;
            Inject(packet);
        }

        bool Ammo(std::string weapon = "minecraft:crossbow") {
            return detail::HasAmmo(hud, &session, weapon);
        }
    };
}

namespace perlin::selftest {
    int RangedItem() {
        Checker test("ranged_item_test");
        const int previousProtocol = net::context::ProtocolSlot().load(std::memory_order_relaxed);
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(protocol);
            rig.Slot(0, Item(10, 101));
            test.Check(!rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "an unloaded survival crossbow without inventory ammo cannot start charging");
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "unloaded crossbow startup remains separate from the inventory ammo check");
            for (const std::string_view projectile : {"minecraft:arrow", "minecraft:firework_rocket"}) {
                const auto loaded = Item(10, 101, projectile);
                rig.Slot(0, loaded);
                const auto view = rig.session.GetInventory().hotbar[0];
                test.Check(view.name == "minecraft:crossbow" && view.chargedItemName == projectile &&
                               view.chargedItemCount == 1 && view.customName.empty() && view.damage == 0,
                           "incoming slot metadata preserves charged projectile separately from the crossbow");
                test.Check(rig.Ammo() && rig.session.HeldItemIsChargedCrossbow(),
                           "a loaded crossbow can fire without a spare inventory projectile");
                test.Check(!rig.session.CanStartUsingHeldItem(false),
                           "a loaded crossbow fires without beginning another held charge");
                const float eye[]{0, 65, 0};
                const size_t before = rig.sentUses.size();
                test.Check(rig.session.SendUseItemInAir(eye, false) && rig.decoded &&
                               rig.sentUses.size() == before + 1,
                           "loaded crossbow use reaches a fully encoded inventory transaction");
                if (rig.sentUses.size() == before + 1) {
                    const auto& held = rig.sentUses.back().heldItem;
                    const auto* extra = std::get_if<proto::ItemExtraDataWithoutBlockingTick>(&held.extra);
                    const bool netId = protocol == 2193 ? !held.hasStackId && !held.stackId
                                                       : held.hasStackId && held.stackId == 101;
                    test.Check(held.networkId == 10 && netId &&
                                   extra && extra->nbt &&
                                   ParseItemNbt(extra->nbt->nbt.bytes).chargedItemName == projectile,
                               "the transaction retains loaded projectile NBT and the profile's descriptor ID inclusion");
                }
            }
            rig.Slot(0, Item(10, 101));
            test.Check(!rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "an authoritative unloaded replacement clears previous charge readiness");
            rig.Slot(9, Item(11, 102));
            test.Check(rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "a main-inventory arrow still permits charging an unloaded crossbow");
            rig.Slot(9, {});
            rig.Slot(0, Item(10, 101, "test:missing"));
            test.Check(!rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "unregistered loaded-item metadata does not resolve a charged stack");
            for (const std::string_view name : {"test:bolt", "minecraft:stone"}) {
                rig.Slot(0, Item(10, 101, name));
                test.Check(rig.Ammo() && rig.session.HeldItemIsChargedCrossbow() &&
                               !rig.session.CanStartUsingHeldItem(false),
                           "registered non-air charged items are recognized independently of shooting semantics");
            }
            rig.Slot(0, Item(10, 101, "minecraft:air"));
            test.Check(!rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "air never becomes a loaded projectile despite being in the item registry");
            rig.Slot(0, Item(13, 103, "minecraft:arrow"));
            test.Check(!rig.Ammo("minecraft:bow") && !rig.session.HeldItemIsChargedCrossbow(),
                       "chargedItem metadata on a bow does not bypass its ammo requirement");
            rig.Slot(0, Item(10, 101));
            rig.Slot(0, Item(10, 104, "minecraft:arrow"), true);
            test.Check(!rig.Ammo(), "an offhand loaded crossbow does not mark the selected unloaded crossbow ready");
            rig.session.SetUseOffhand(true);
            test.Check(rig.Ammo() && rig.session.HeldItemIsChargedCrossbow(),
                       "an offhand use queries the same loaded item selected by its transaction path");
            rig.Slot(0, {}, true);
            test.Check(!rig.Ammo() && !rig.session.HeldItemIsChargedCrossbow(),
                       "clearing the authoritative offhand stack clears its loaded readiness");
            rig.session.SetUseOffhand(false);
            for (int level : {0, 1, 2, 3}) {
                rig.Slot(0, EnchantedItem(10, 105, 35, static_cast<int16_t>(level)));
                const auto view = rig.session.GetInventory().hotbar[0];
                test.Check(view.enchants.size() == 1 && view.enchants[0] == std::pair{35, level} &&
                               rig.session.HeldItemEnchantLevel(35) == level &&
                               blockuse::VanillaUseTicks(view.name, false,
                                                        rig.session.HeldItemEnchantLevel(35)) ==
                                   25 - 5 * level,
                           "authoritative Quick Charge enchantment supplies the native crossbow duration");
            }
            rig.Slot(0, EnchantedItem(10, 105, 35, 3, std::nullopt, true));
            test.Check(rig.session.HeldItemEnchantLevel(35) == 0,
                       "stored enchantments cannot change the crossbow's active use duration");
            rig.Slot(0, EnchantedItem(10, 105, 35, 1, std::nullopt, false, 3, 3));
            test.Check(rig.session.HeldItemEnchantLevel(35) == 1,
                       "duplicate active enchantments use the first list entry rather than the maximum");
            rig.Slot(0, EnchantedItem(10, 105, 0x123, -1));
            test.Check(rig.session.HeldItemEnchantLevel(35) == -1 &&
                           blockuse::VanillaUseTicks("minecraft:crossbow", false,
                                                    rig.session.HeldItemEnchantLevel(35)) == 25,
                       "native enchant loading narrows ids to a byte and preserves signed short levels");
            rig.Slot(0, {}, true);
            rig.session.SetUseOffhand(true);
            test.Check(rig.session.HeldItemEnchantLevel(35) == 0,
                       "an empty selected offhand cannot inherit main-hand enchantments");
            rig.session.SetUseOffhand(false);
            rig.Slot(0, Item(14, 106));
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "an undamaged trident without Riptide can start use while dry");
            rig.Slot(0, EnchantedItem(14, 106, 30, 3));
            test.Check(!rig.session.CanStartUsingHeldItem(false) &&
                           rig.session.CanStartUsingHeldItem(true),
                       "Riptide requires water or rain before held use begins");
            rig.Slot(0, EnchantedItem(14, 106, 30, 0));
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "a zero Riptide level does not impose the wet-start restriction");
            rig.Slot(0, EnchantedItem(14, 106, 30, 3, std::nullopt, true));
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "stored enchantment metadata does not activate Riptide on a trident");
            for (const auto& [damage, allowed] :
                 {std::pair{249, true}, std::pair{250, false}, std::pair{251, false},
                  std::pair{-1, true}, std::pair{65535, true}, std::pair{65536, true}}) {
                rig.Slot(0, EnchantedItem(14, 106, 30, 0, damage));
                test.Check(rig.session.CanStartUsingHeldItem(false) == allowed,
                           "trident startup uses the native signed-short Damage comparison at250");
            }
            rig.Slot(0, EnchantedItem(14, 106, 30, 0, 250, false, 2));
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "a non-IntTag Damage does not satisfy the native trident damage getter");
            rig.Slot(0, Item(14, 106));
            rig.Slot(0, EnchantedItem(14, 107, 30, 1), true);
            test.Check(rig.session.CanStartUsingHeldItem(false),
                       "an offhand Riptide enchantment does not restrict the main-hand trident");
            rig.session.SetUseOffhand(true);
            test.Check(!rig.session.CanStartUsingHeldItem(false) &&
                           rig.session.CanStartUsingHeldItem(true) &&
                           rig.session.HeldItemEnchantLevel(30) == 1,
                       "offhand trident startup uses that hand's active enchantment");
        }
        net::context::SetProtocol(previousProtocol);
        return test.Finish();
    }
}
