#include "client/entity/EntityTracker.hpp"
#include "network/EntityMetadata.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"
#include <bit>
#include <iostream>
using namespace perlin;
using namespace perlin::proto;
int failures = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << __LINE__ << ": " #x "\n";                                                           \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)
int main() {
    namespace flag = entity::flag;
    static_assert(flag::Swimming == FlagsMetadataFlags1::Swimming);
    static_assert(flag::SpinAttack == FlagsMetadataFlags1::SpinAttack);
    static_assert(flag::HasCollision == FlagsMetadataFlags1::HasCollision);
    static_assert(flag::Gravity == FlagsMetadataFlags1::AffectedByGravity);
    static_assert(flag::Eating == FlagsMetadataFlags1::Eating);
    static_assert(FlagsMetadataFlags2::Sleeping == (1ull << 12));
    static_assert(FlagsMetadataFlags2::Blocking == (1ull << 8));
    static_assert(FlagsMetadataFlags2::Crawling == (1ull << 50));
    static_assert(FlagsMetadataFlags2::PushTowardsClosestSpace == (1ull << 45));
    SetEntityDataPacket sent;
    sent.runtimeEntityId = 17;
    const uint64_t flags = FlagsMetadataFlags1::Eating | FlagsMetadataFlags1::Swimming |
                           FlagsMetadataFlags1::Sneaking | FlagsMetadataFlags1::Action;
    MetadataDictionaryEntry lower;
    lower.key = MetadataDictionaryEntryKey::Flags;
    lower.type = MetadataDictionaryEntryType::Long;
    lower.value.emplace<1>(std::bit_cast<int64_t>(flags));
    MetadataDictionaryEntry upper;
    upper.key = MetadataDictionaryEntryKey::FlagsExtended;
    upper.type = MetadataDictionaryEntryType::Long;
    const uint64_t extended = FlagsMetadataFlags2::LayingDown | FlagsMetadataFlags2::Sleeping |
                              FlagsMetadataFlags2::Crawling | FlagsMetadataFlags2::Blocking;
    upper.value.emplace<2>(static_cast<int64_t>(extended));
    sent.metadata = {lower, upper};
    auto bytes = net::Encode(sent);
    net::ReadStream stream(bytes.data(), bytes.size());
    SetEntityDataPacket received;
    CHECK(Read(stream, received));
    CHECK(received.metadata.size() == 2);
    int64_t word = 0;
    entity::EntityTracker tracker;
    tracker.AddPlayer(17, 17, "test", "Test", {}, 0, 0, 0, 0, "minecraft:bow");
    CHECK(net::MetadataInteger(received.metadata[0], word));
    CHECK(static_cast<uint64_t>(word) == flags);
    tracker.SetFlags(17, static_cast<uint64_t>(word));
    CHECK(net::MetadataInteger(received.metadata[1], word));
    CHECK(static_cast<uint64_t>(word) == extended);
    tracker.SetFlagsExtended(17, static_cast<uint64_t>(word));
    entity::Entity entity;
    CHECK(tracker.Find(17, entity));
    CHECK(entity.Has(flag::Swimming) && entity.Has(flag::Sneaking) && entity.Has(flag::UsingItem));
    CHECK(entity.Has(flag::Eating) && entity.LayingDown() && entity.sleeping && entity.crawling &&
          entity.Blocking());
    tracker.SetFlagsExtended(17, FlagsMetadataFlags2::BlockedUsingDamagedShield);
    CHECK(tracker.Find(17, entity));
    CHECK(!entity.sleeping && !entity.crawling && !entity.Blocking() && !entity.LayingDown());
    tracker.SetSleeping(17, true, true);
    tracker.SetFlagsExtended(17, 0);
    CHECK(tracker.Find(17, entity));
    CHECK(entity.sleeping);
    tracker.SetSleeping(17, false, true);
    tracker.SetOffHand(17, "minecraft:shield");
    tracker.SetMainLeft(17, true);
    tracker.Swing(17);
    CHECK(tracker.Find(17, entity));
    CHECK(entity.LeftHand() == "minecraft:bow" && entity.RightHand() == "minecraft:shield" &&
          !entity.swingOffHand);
    tracker.Swing(17, true);
    CHECK(tracker.Find(17, entity));
    CHECK(entity.swingOffHand);
    MetadataDictionaryEntry invalid;
    invalid.value.emplace<3>(1.0f);
    CHECK(!net::MetadataInteger(invalid, word));
    std::cout << (failures ? "FAIL" : "PASS")
              << " entity state: exact metadata words, pose flags, dominant arm and swing hand\n";
    return failures ? 1 : 0;
}
