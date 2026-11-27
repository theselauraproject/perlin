#include "network/Stream.hpp"
#include "network/PacketDispatcher.hpp"
#include "protocol/Packets.hpp"

#include <array>
#include <cstdio>
#include <vector>

namespace {
    using namespace perlin;
    int checks = 0, failures = 0;

    void Check(bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL sound wire: %s\n", message);
        }
    }

    template <class P> bool Decode(const std::vector<uint8_t>& bytes, P& packet) {
        net::ReadStream stream(bytes.data(), bytes.size());
        return proto::Read(stream, packet) && stream.remaining() == 0;
    }

    std::vector<uint8_t> PacketBytes(const std::vector<uint8_t>& event, int copies) {
        // Fixed little-endian uint64 handle, with no optional-field flags.
        std::vector<uint8_t> bytes{1, 2, 3, 4, 5, 6, 7, 8};
        for (int i = 0; i < copies; ++i)
            bytes.insert(bytes.end(), event.begin(), event.end());
        return bytes;
    }
}

int main() {
    using namespace perlin;
    using Type = proto::SoundDataUpdateType;
    // Independent wire fixtures: these float bytes and tag positions are not
    // produced by the encoder under test. Native 2193 writes seven complete
    // variants; 2225 writes one uint8-tagged event. Fade is duration, target.
    const std::array<std::vector<uint8_t>, 7> events{
        std::vector<uint8_t>{0},
        std::vector<uint8_t>{1, 0, 0, 0xa0, 0x3f}, // volume 1.25
        std::vector<uint8_t>{2, 0, 0, 0x20, 0x40}, // pitch 2.5
        std::vector<uint8_t>{3, 0, 0, 0, 0x40, 0, 0, 0x80, 0x3e}, // 2s -> .25
        std::vector<uint8_t>{4, 0, 0, 0xc0, 0x3f}, // seek 1.5s
        std::vector<uint8_t>{5},
        std::vector<uint8_t>{6},
    };
    for (int protocol : {2193, 2225}) {
        const net::context::ScopedSettings context({protocol, -1});
        for (size_t i = 0; i < events.size(); ++i) {
            const auto bytes = PacketBytes(events[i], protocol == 2193 ? 7 : 1);
            proto::ClientboundUpdateSoundDataPacket received;
            Check(Decode(bytes, received), "literal event decodes through selected protocol facade");
            Check(received.serverSoundHandle == UINT64_C(0x0807060504030201),
                  "server handle remains a fixed little-endian uint64");
            Check(received.update.type == static_cast<Type>(i), "event kind survives the version bridge");
            if (i == 1)
                Check(received.update.volume == 1.25f, "volume payload is retained");
            if (i == 2)
                Check(received.update.pitch == 2.5f, "pitch payload is retained");
            if (i == 3)
                Check(received.update.duration == 2.0f && received.update.targetVolume == 0.25f,
                      "fade decodes duration before target volume");
            if (i == 4)
                Check(received.update.seconds == 1.5f, "seek payload is retained");

            proto::ClientboundUpdateSoundDataPacket sent;
            sent.serverSoundHandle = UINT64_C(0x0807060504030201);
            sent.update.type = static_cast<Type>(i);
            sent.update.volume = 1.25f;
            sent.update.pitch = 2.5f;
            sent.update.duration = 2.0f;
            sent.update.targetVolume = 0.25f;
            sent.update.seconds = 1.5f;
            Check(net::Encode(sent) == bytes, "encoder matches literal variant layout and float ordering");

            auto truncated = bytes;
            truncated.pop_back();
            proto::ClientboundUpdateSoundDataPacket rejected;
            Check(!Decode(truncated, rejected), "a truncated final event fails decoding");
        }
    }
    {
        const net::context::ScopedSettings context({2193, -1});
        auto bytes = PacketBytes(events[5], 6);
        bytes.insert(bytes.end(), events[1].begin(), events[1].end());
        proto::ClientboundUpdateSoundDataPacket received;
        Check(Decode(bytes, received) && received.update.type == Type::SetVolume &&
                  received.update.volume == 1.25f,
              "the seventh legacy assignment determines the delivered event");
        bytes = PacketBytes(events[1], 6);
        bytes.insert(bytes.end(), events[6].begin(), events[6].end());
        Check(Decode(bytes, received) && received.update.type == Type::Resume,
              "a final legacy event without fields replaces preceding payload events");
        bytes = PacketBytes(events[1], 1);
        Check(!Decode(bytes, received), "2193 requires all seven variant records");

        // The native reader validates the full uint32 index before narrowing.
        // 256 must not become Stop, and 257..262 must not become real controls.
        const std::array<std::vector<uint8_t>, 6> unknownTags{
            std::vector<uint8_t>{7}, std::vector<uint8_t>{0xff, 1},
            std::vector<uint8_t>{0x80, 2}, std::vector<uint8_t>{0x81, 2},
            std::vector<uint8_t>{0x86, 2}, std::vector<uint8_t>{0xff, 0xff, 0xff, 0xff, 0x0f},
        };
        net::PacketDispatcher dispatcher;
        int controls = 0;
        dispatcher.On<proto::ClientboundUpdateSoundDataPacket>([&](const auto&) { ++controls; });
        for (const auto& tag : unknownTags) {
            for (int badCopy = 0; badCopy < 7; ++badCopy) {
                bytes = PacketBytes({}, 0);
                for (int copy = 0; copy < 7; ++copy) {
                    const auto& record = copy == badCopy ? tag : events[1];
                    bytes.insert(bytes.end(), record.begin(), record.end());
                }
                received.serverSoundHandle = 999;
                received.update.type = Type::SetPitch;
                received.update.pitch = 3;
                net::ReadStream stream(bytes.data(), bytes.size());
                Check(!proto::Read(stream, received) && !stream.ok,
                      "an unknown full-width index in any legacy copy fails decoding");
                Check(received.serverSoundHandle == 999 && received.update.type == Type::SetPitch &&
                          received.update.pitch == 3,
                      "invalid legacy indices cannot narrow to a known control or update the destination");
                Check(dispatcher.Dispatch(received.kId, bytes.data(), bytes.size()) ==
                          net::PacketDispatcher::Result::Malformed && controls == 0,
                      "invalid legacy variants never reach a strict sound-control callback");
            }
        }
    }
    std::printf("sound_packets_wire_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
