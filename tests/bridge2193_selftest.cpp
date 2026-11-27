#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <array>
#include <cstdio>
#include <variant>
#include <vector>

namespace {
    using namespace perlin;

    int checks = 0, failures = 0;
    void Check(bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL bridge2193: %s\n", message);
        }
    }

    template <class P> bool Decode(const std::vector<uint8_t>& bytes, P& packet) {
        net::ReadStream stream(bytes.data(), bytes.size());
        return proto::Read(stream, packet) && stream.remaining() == 0;
    }
}

int main() {
    using namespace perlin;
    net::context::SetProtocol(2193);
    struct FlagsCase {
        uint64_t word;
        std::vector<uint8_t> bytes;
    };
    // Literal SetActorData payloads: actor 7, one FlagsExtended entry,
    // two empty property lists, tick 1. Values are signed-varint words.
    const std::array cases{
        FlagsCase{uint64_t{1}, {7, 1, 92, 7, 0, 2, 0, 0, 1}},
        FlagsCase{uint64_t{1} << 1, {7, 1, 92, 7, 0, 4, 0, 0, 1}},
        FlagsCase{uint64_t{1} << 12, {7, 1, 92, 7, 0, 0x80, 0x40, 0, 0, 1}},
        FlagsCase{uint64_t{1} << 45,
                  {7, 1, 92, 7, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x10, 0, 0, 1}},
        FlagsCase{uint64_t{1} << 63,
                  {7, 1, 92, 7, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                   0xff, 1, 0, 0, 1}},
    };
    for (const auto& test : cases) {
        proto::SetEntityDataPacket packet;
        const bool decoded = Decode(test.bytes, packet);
        Check(decoded && packet.runtimeEntityId == 7 && packet.metadata.size() == 1,
              "literal extended flags decode through the 2193 facade");
        const int64_t* word = decoded && packet.metadata.size() == 1
                                  ? std::get_if<2>(&packet.metadata.front().value)
                                  : nullptr;
        Check(word && static_cast<uint64_t>(*word) == test.word,
              "laying down, sneezing, sleeping, escape and friction bits are not shifted or lost");
        proto::SetEntityDataPacket outgoing;
        outgoing.runtimeEntityId = 7;
        outgoing.tick = 1;
        proto::MetadataDictionaryEntry entry;
        entry.key = proto::MetadataDictionaryEntryKey::FlagsExtended;
        entry.type = proto::MetadataDictionaryEntryType::Long;
        entry.value.emplace<2>(static_cast<int64_t>(test.word));
        outgoing.metadata.push_back(entry);
        Check(net::Encode(outgoing) == test.bytes,
              "outgoing 2193 metadata preserves the literal extended bit word");
    }
    for (int64_t actorId : {int64_t{70}, int64_t{-70}}) {
        proto::ClientMovementPredictionSyncPacket packet;
        packet.entityUniqueId = actorId;
        // Zero flag word plus twelve zero float32 values precede ActorUniqueID.
        std::vector<uint8_t> expected(49, 0);
        expected.push_back(actorId > 0 ? 0x8c : 0x8b);
        expected.push_back(1);
        expected.push_back(0);
        Check(net::Encode(packet) == expected,
              "prediction sync serializes a signed unique ID in protocol 2193");
        proto::ClientMovementPredictionSyncPacket received;
        Check(Decode(expected, received) && received.entityUniqueId == actorId,
              "literal positive and negative prediction-sync IDs survive 2193 decoding");
    }
    net::context::SetProtocol(2225);
    std::printf("bridge2193_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
