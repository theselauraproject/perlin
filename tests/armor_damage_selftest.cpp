#include "client/systems/multiplayer/ItemDamage.hpp"
#include "client/systems/multiplayer/ItemNbtInfo.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"

#include <cstdio>
#include <limits>

namespace {
    int failures = 0;

    void Check(bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    const std::vector<uint8_t> kDamage7{
        10, 0, 0, 3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 7, 0, 0, 0, 0};
    const std::vector<uint8_t> kDamageMinus2{
        10, 0, 0, 3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 254, 255, 255, 255, 0};

    template<class Extra> void CheckExtraPreservation(Extra extra, const char* message) {
        using namespace perlin;
        extra.canPlaceOn = {"minecraft:stone", "custom:block"};
        extra.canDestroy = {"minecraft:oak_log"};
        extra.nbt.emplace();
        extra.nbt->version = 1;
        extra.nbt->nbt.bytes = kDamage7;
        extra.hasNbt = decltype(extra.hasNbt)::True;
        if constexpr (requires { extra.blockingTick; })
            extra.blockingTick = -123456789;
        proto::ItemV4 item;
        item.networkId = 12;
        item.count = 1;
        item.metadata = 99;
        item.hasStackId = true;
        item.stackId = 8102;
        item.blockRuntimeId = 321;
        item.extra = extra;
        Check(SetItemDamage(item, -2), message);
        const auto* after = std::get_if<Extra>(&item.extra);
        Check(item.networkId == 12 && item.count == 1 && item.metadata == 99 && item.hasStackId &&
                  item.stackId == 8102 && item.blockRuntimeId == 321,
              "damage update preserves item identity, count, auxiliary data, and stack network ID");
        Check(after && after->canPlaceOn == extra.canPlaceOn && after->canDestroy == extra.canDestroy &&
                  after->nbt && after->nbt->version == 1 && after->nbt->nbt.bytes == kDamageMinus2,
              "damage update preserves extra-data restrictions and NBT version");
        if constexpr (requires { extra.blockingTick; })
            Check(after && after->blockingTick == extra.blockingTick, "shield blocking tick is preserved");
    }
}

int main() {
    using namespace perlin;
    {
        // Matched 1.26.51 writes signed varints for cause/damage and an
        // unsigned varlong for the slot mask. Current official metadata agrees.
        const std::vector<uint8_t> wire{2, 24, 31};
        proto::HurtArmorPacket packet;
        packet.cause = 1;
        packet.damage = 12;
        packet.armorSlots = 31;
        Check(net::Encode(packet) == wire, "HurtArmor slot mask uses unsigned varlong encoding");
        for (int version : {2193, 2225}) {
            net::context::SetProtocol(version);
            net::PacketDispatcher dispatcher;
            bool received = false;
            dispatcher.On<proto::HurtArmorPacket>([&](const proto::HurtArmorPacket& value) {
                received = value.cause == 1 && value.damage == 12 && value.armorSlots == 31;
            });
            dispatcher.Dispatch(proto::HurtArmorPacket::kId, wire.data(), wire.size());
            Check(received, "matched and current protocol decode the literal HurtArmor mask fixture");
        }
        net::context::SetProtocol(0);
    }
    {
        const std::vector<uint8_t> wire{2, 2, 44, 1, 6, 254, 255};
        proto::PlayerArmorDamagePacket packet;
        packet.entries = {{proto::ArmorDamageEntryArmorSlot::Chestplate, 300},
                          {proto::ArmorDamageEntryArmorSlot::Boots, -2}};
        Check(net::Encode(packet) == wire,
              "armor update encodes signed varint slots followed by little-endian signed shorts");
        for (int version : {2193, 2225}) {
            net::context::SetProtocol(version);
            net::PacketDispatcher dispatcher;
            bool received = false;
            dispatcher.On<proto::PlayerArmorDamagePacket>([&](const proto::PlayerArmorDamagePacket& value) {
                received = value.entries.size() == 2 &&
                           value.entries[0].armorSlot == proto::ArmorDamageEntryArmorSlot::Chestplate &&
                           value.entries[0].damage == 300 &&
                           value.entries[1].armorSlot == proto::ArmorDamageEntryArmorSlot::Boots &&
                           value.entries[1].damage == -2;
            });
            dispatcher.Dispatch(proto::PlayerArmorDamagePacket::kId, wire.data(), wire.size());
            Check(received, "matched and current protocol decode the literal multi-slot damage fixture");
        }
        net::context::SetProtocol(0);
    }
    {
        std::vector<uint8_t> bytes;
        Check(SetItemNbtDamage(bytes, 7) && bytes == kDamage7,
              "missing NBT creates a little-endian root compound with an int Damage tag");
        Check(SetItemNbtDamage(bytes, -2) && bytes == kDamageMinus2,
              "signed 16-bit server damage is sign-extended into the int tag");
        Check(ParseItemNbt(bytes).hasDamage && ParseItemNbt(bytes).damage == -2,
              "negative Damage is distinguishable from an absent tag");
        Check(SetItemNbtDamage(bytes, -2) && bytes == kDamageMinus2,
              "authoritative damage replacement is idempotent");
        Check(SetItemNbtDamage(bytes, std::numeric_limits<int16_t>::min()) &&
                  ParseItemNbt(bytes).damage == std::numeric_limits<int16_t>::min(),
              "minimum signed damage is retained without clamping");
        Check(SetItemNbtDamage(bytes, std::numeric_limits<int16_t>::max()) &&
                  ParseItemNbt(bytes).damage == std::numeric_limits<int16_t>::max(),
              "maximum signed damage is retained without breaking the item");
    }
    {
        // Nested Damage and an opaque byte array must not be rewritten.
        std::vector<uint8_t> bytes{
            10, 4, 0, 'r', 'o', 'o', 't',
            10, 3, 0, 't', 'a', 'g',
            3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 123, 0, 0, 0, 0,
            2, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 9, 0,
            7, 4, 0, 'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 255, 3, 10,
            8, 4, 0, 'N', 'a', 'm', 'e', 3, 0, 'b', 'o', 'w', 0};
        const std::vector<uint8_t> expected{
            10, 4, 0, 'r', 'o', 'o', 't',
            10, 3, 0, 't', 'a', 'g',
            3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 123, 0, 0, 0, 0,
            3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 7, 0, 0, 0,
            7, 4, 0, 'd', 'a', 't', 'a', 4, 0, 0, 0, 0, 255, 3, 10,
            8, 4, 0, 'N', 'a', 'm', 'e', 3, 0, 'b', 'o', 'w', 0};
        Check(SetItemNbtDamage(bytes, 7) && bytes == expected,
              "only root Damage changes; unknown tags, nested tags, root name, and tag order survive");
    }
    {
        std::vector<uint8_t> bytes{10, 0, 0, 8, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 1, 0, 'x',
                                   2, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 2, 0, 0};
        Check(SetItemNbtDamage(bytes, 7) && bytes == kDamage7,
              "Damage is replaced with one int tag even if its old type or duplicate tags differ");
    }
    {
        const std::vector<std::vector<uint8_t>> malformed{
            {3, 0, 0, 0, 0, 0, 0},
            {10, 0, 0},
            {10, 0, 0, 0, 42},
            {10, 0, 0, 3, 6, 0, 'D', 'a', 'm', 'a', 'g', 'e', 7, 0},
            {10, 0, 0, 7, 0, 0, 255, 255, 255, 255, 0},
            {10, 0, 0, 13, 0, 0, 0},
        };
        for (const auto& source : malformed) {
            auto bytes = source;
            Check(!SetItemNbtDamage(bytes, 7) && bytes == source,
                  "malformed item NBT is rejected without partially replacing data");
        }
    }
    CheckExtraPreservation(proto::ItemExtraDataWithBlockingTick{}, "shield extra data accepts damage");
    CheckExtraPreservation(proto::ItemExtraDataWithoutBlockingTick{}, "ordinary extra data accepts damage");
    {
        proto::ItemV4 item;
        Check(!SetItemDamage(item, 7) && std::holds_alternative<std::monostate>(item.extra),
              "an empty armor slot does not acquire an item or NBT");
        item.networkId = 16;
        item.count = 1;
        net::context::SetShieldItemID(17);
        Check(SetItemDamage(item, 7) &&
                  std::get<proto::ItemExtraDataWithoutBlockingTick>(item.extra).nbt->nbt.bytes == kDamage7,
              "ordinary item with no extra data acquires the correct variant");
        item.extra = {};
        item.networkId = 17;
        Check(SetItemDamage(item, 7) &&
                  std::get<proto::ItemExtraDataWithBlockingTick>(item.extra).nbt->nbt.bytes == kDamage7,
              "shield with no extra data acquires its blocking-tick variant");
        net::context::SetShieldItemID(-1);
    }
    {
        proto::ItemV4 item;
        item.networkId = 1;
        item.count = 1;
        proto::ItemExtraDataWithoutBlockingTick extra;
        extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
        extra.nbt.emplace();
        extra.nbt->version = 1;
        extra.nbt->nbt.bytes = {10, 0, 0, 3};
        item.extra = extra;
        Check(!SetItemDamage(item, 3) &&
                  std::get<proto::ItemExtraDataWithoutBlockingTick>(item.extra).nbt->nbt.bytes ==
                      extra.nbt->nbt.bytes,
              "malformed raw NBT is retained when an authoritative update cannot be applied");
    }
    if (failures == 0)
        std::puts("armor_damage_selftest: all checks passed");
    return failures == 0 ? 0 : 1;
}
