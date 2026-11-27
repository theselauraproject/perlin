#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

namespace {
    using namespace perlin;
    int checks = 0, failures = 0;
    int profile = 0;
    void Check(bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL chunk wire [%d]: %s\n", profile, message);
        }
    }
    template <class Packet> bool Decode(const std::vector<uint8_t>& bytes, Packet& packet) {
        net::ReadStream stream(bytes.data(), bytes.size());
        return proto::Read(stream, packet) && stream.remaining() == 0;
    }
    template <class Packet> void Encodes(const Packet& packet, const std::vector<uint8_t>& expected,
                                         const char* message) {
        std::vector<uint8_t> bytes;
        Check(net::EncodeChecked(packet, bytes) && bytes == expected, message);
    }
    constexpr uint64_t kHash = UINT64_C(0x0123456789abcdef);
    constexpr uint8_t kHashBytes[] = {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};

    proto::LevelChunkPacket Column() {
        proto::LevelChunkPacket packet;
        packet.x = -2;
        packet.z = 3;
        packet.dimension = 1;
        return packet;
    }

    void Columns() {
        // Coordinates use signed ZigZag32. Ordinary counts have no request-limit
        // byte or presence flag in the old wire layout.
        const std::vector<uint8_t> ordinary{3, 6, 2, 2, 0, 2, 0xaa, 0xbb};
        auto packet = Column();
        packet.subChunkCount = 2;
        packet.payload = {0xaa, 0xbb};
        Encodes(packet, ordinary, "ordinary column matches the literal old layout");
        auto decoded = Column();
        decoded.highestSubchunkCount = 91;
        decoded.blobs = {kHash};
        Check(Decode(ordinary, decoded), "literal ordinary column is fully decoded");
        Check(decoded.x == -2 && decoded.z == 3 && decoded.dimension == 1 &&
                  decoded.subChunkCount == 2 && decoded.payload == packet.payload,
              "ordinary column coordinates, count and payload survive the bridge");
        Check(!decoded.highestSubchunkCount && decoded.blobs.empty(),
              "ordinary column does not invent request mode or retain old cache metadata");

        for (int32_t limit : {-1, 0, 291, 65535}) {
            // Mojang's legacy request-system guide specifies UINT32_MAX for
            // unlimited requests, UINT32_MAX-1 followed by a uint16 index for
            // bounded requests. Current request mode is the present optional.
            std::vector<uint8_t> bytes{3, 6, 2,
                                       static_cast<uint8_t>(limit < 0 ? 0xff : 0xfe),
                                       0xff, 0xff, 0xff, 0x0f};
            if (limit >= 0) {
                bytes.push_back(static_cast<uint8_t>(limit));
                bytes.push_back(static_cast<uint8_t>(limit >> 8));
            }
            bytes.insert(bytes.end(), {0, 0}); // cache disabled, empty payload
            auto sent = Column();
            sent.highestSubchunkCount = limit; // no sentinel required in the current facade
            Encodes(sent, bytes, "present request limit selects the exact legacy sentinel and uint16 bytes");
            auto received = Column();
            Check(Decode(bytes, received) && received.highestSubchunkCount == limit,
                  "literal legacy request mode preserves bounded and unlimited limits");
        }
        for (int32_t limit : {-2, 65536, std::numeric_limits<int32_t>::max()}) {
            auto sent = Column();
            sent.highestSubchunkCount = limit;
            std::vector<uint8_t> bytes;
            Check(!net::EncodeChecked(sent, bytes),
                  "unrepresentable current request limits are rejected instead of narrowed to uint16");
        }
        for (uint32_t count : {UINT32_C(0xfffffffe), UINT32_C(0xffffffff)}) {
            auto sent = Column();
            sent.subChunkCount = count;
            std::vector<uint8_t> bytes;
            Check(!net::EncodeChecked(sent, bytes),
                  "ordinary facade counts cannot silently turn into legacy request sentinels");
        }

        std::vector<uint8_t> cached{3, 6, 2, 1, 1, 2};
        cached.insert(cached.end(), std::begin(kHashBytes), std::end(kHashBytes));
        cached.insert(cached.end(), {8, 7, 6, 5, 4, 3, 2, 1, 3, 0x11, 0x22, 0x33});
        auto sent = Column();
        sent.subChunkCount = 1;
        sent.cacheEnabled = true;
        sent.blobs = {kHash, UINT64_C(0x0102030405060708)};
        sent.payload = {0x11, 0x22, 0x33};
        Encodes(sent, cached, "cached column emits both literal little-endian blob hashes");
        auto received = Column();
        Check(Decode(cached, received) && received.cacheEnabled && received.blobs == sent.blobs &&
                  received.payload == sent.payload && !received.highestSubchunkCount,
              "cached column retains hash order and ordinary-column mode");
        sent.subChunkCount = 0;
        sent.blobs.clear();
        sent.payload.clear();
        Encodes(sent, {3, 6, 2, 0, 1, 0, 0}, "enabled cache with an empty hash list retains its count byte");
        auto truncated = cached;
        truncated.resize(10);
        Check(!Decode(truncated, received), "truncated cache hashes fail decoding");
    }

    proto::SubchunkPacket Reply(bool cache, bool air, bool heightmaps) {
        proto::SubchunkPacket packet;
        packet.cacheEnabled = cache;
        packet.dimension = 1;
        packet.origin = {-2, -4, 3};
        auto& entry = packet.entries.emplace_back();
        entry.dx = -1;
        entry.dy = 2;
        entry.dz = -3;
        entry.result = air ? proto::SubChunkEntryEntryResult::SuccessAllAir
                           : proto::SubChunkEntryEntryResult::Success;
        if (!air)
            entry.payload = std::vector<uint8_t>{0xaa, 0xbb};
        if (cache)
            entry.blobId = kHash;
        if (heightmaps) {
            entry.heightmapType = entry.renderHeightmapType = proto::HeightMapDataType::HasData;
            entry.heightmap.emplace();
            entry.renderHeightmap.emplace();
            for (size_t row = 0; row < 16; ++row) {
                for (size_t col = 0; col < 16; ++col) {
                    const auto value = static_cast<uint8_t>(row * 16 + col);
                    (*entry.heightmap)[row].push_back(std::bit_cast<int8_t>(value));
                    (*entry.renderHeightmap)[row].push_back(std::bit_cast<int8_t>(static_cast<uint8_t>(255 - value)));
                }
            }
        }
        return packet;
    }

    std::vector<uint8_t> ReplyBytes(bool cache, bool air, bool heightmaps) {
        if (profile == 2168) {
            // This transitional profile already has the new fixed-int origin,
            // varuint entry count and option flags, but maps are still 256 bytes.
            std::vector<uint8_t> bytes{static_cast<uint8_t>(cache), 2,
                                       0xfe, 0xff, 0xff, 0xff, 0xfc, 0xff, 0xff, 0xff, 3, 0, 0, 0,
                                       1, 0xff, 2, 0xfd, static_cast<uint8_t>(air ? 6 : 1),
                                       static_cast<uint8_t>(!air)};
            if (!air)
                bytes.insert(bytes.end(), {2, 0xaa, 0xbb});
            bytes.insert(bytes.end(), {static_cast<uint8_t>(heightmaps), static_cast<uint8_t>(heightmaps)});
            if (heightmaps)
                for (unsigned value = 0; value < 256; ++value)
                    bytes.push_back(static_cast<uint8_t>(value));
            bytes.insert(bytes.end(), {static_cast<uint8_t>(heightmaps), static_cast<uint8_t>(heightmaps)});
            if (heightmaps)
                for (unsigned value = 0; value < 256; ++value)
                    bytes.push_back(static_cast<uint8_t>(255 - value));
            bytes.push_back(static_cast<uint8_t>(cache));
            if (cache)
                bytes.insert(bytes.end(), std::begin(kHashBytes), std::end(kHashBytes));
            return bytes;
        }
        // cache flag, signed dimension/origin, fixed little-endian entry count,
        // signed byte offsets, result. Uncached all-air still carries an empty
        // ByteArray; cached all-air omits that field entirely in these profiles.
        std::vector<uint8_t> bytes{static_cast<uint8_t>(cache), 2, 3, 7, 6, 1, 0, 0, 0,
                                   0xff, 2, 0xfd, static_cast<uint8_t>(air ? 6 : 1)};
        if (!air)
            bytes.insert(bytes.end(), {2, 0xaa, 0xbb});
        else if (!cache)
            bytes.push_back(0);
        bytes.push_back(static_cast<uint8_t>(heightmaps));
        if (heightmaps)
            for (unsigned value = 0; value < 256; ++value)
                bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(heightmaps));
        if (heightmaps)
            for (unsigned value = 0; value < 256; ++value)
                bytes.push_back(static_cast<uint8_t>(255 - value));
        if (cache)
            bytes.insert(bytes.end(), std::begin(kHashBytes), std::end(kHashBytes));
        return bytes;
    }

    void Replies() {
        for (bool cache : {false, true}) {
            for (bool air : {false, true}) {
                for (bool heightmaps : {false, true}) {
                    const auto bytes = ReplyBytes(cache, air, heightmaps);
                    const auto sent = Reply(cache, air, heightmaps);
                    Encodes(sent, bytes, "reply selects the caching variant and literal payload/heightmap layout");
                    auto received = Reply(true, false, true);
                    Check(Decode(bytes, received) && received.entries.size() == 1,
                          "literal cached or uncached reply is fully decoded");
                    if (received.entries.size() != 1)
                        continue;
                    const auto& entry = received.entries[0];
                    Check(received.cacheEnabled == cache && received.dimension == 1 &&
                              received.origin.x == -2 && received.origin.y == -4 && received.origin.z == 3 &&
                              entry.dx == -1 && entry.dy == 2 && entry.dz == -3 && entry.result == sent.entries[0].result,
                          "reply header, signed coordinates and result are retained");
                    Check(entry.blobId == (cache ? std::optional<uint64_t>{kHash} : std::nullopt),
                          "blob ID is present only on the cached legacy variant");
                    const bool payloadPresent = profile == 2168 ? !air : !(cache && air);
                    Check(entry.payload.has_value() == payloadPresent &&
                              (air || entry.payload == sent.entries[0].payload),
                          "legacy payload absence is preserved separately from an empty uncached payload");
                    Check(entry.heightmap == sent.entries[0].heightmap &&
                              entry.renderHeightmap == sent.entries[0].renderHeightmap,
                          "both 256-byte maps preserve signed values, row order and optional absence");
                }
            }
            auto invalid = Reply(cache, false, true);
            (*invalid.entries[0].heightmap)[7].pop_back();
            std::vector<uint8_t> bytes;
            Check(!net::EncodeChecked(invalid, bytes),
                  "a non-16-by-16 heightmap cannot be silently truncated or padded into the legacy layout");
            auto truncated = ReplyBytes(cache, false, true);
            truncated.resize(120);
            proto::SubchunkPacket received;
            Check(!Decode(truncated, received), "truncated literal heightmap fails decoding");
            auto empty = Reply(cache, false, false);
            empty.entries[0].payload.reset();
            auto expected = ReplyBytes(cache, false, false);
            if (profile == 2168) {
                expected = ReplyBytes(cache, true, false);
                expected[18] = 1; // Success with absent inline payload
            } else {
                expected.erase(expected.begin() + 14, expected.begin() + 16);
                expected[13] = 0;
            }
            Encodes(empty, expected, "absent inline payload respects the target profile's required-or-optional representation");
        }
    }

    void Requests() {
        // Dimension/origin are ZigZag32; request count is fixed uint32;
        // individual offsets are three raw signed bytes, not named x/y/z fields.
        std::vector<uint8_t> bytes{2, 3, 7, 6, 2, 0, 0, 0, 0xff, 2, 0xfd, 0x7f, 0x80, 1};
        if (profile >= 1001) {
            // 1001 already moved requests before the origin, made their count
            // varuint, and changed the origin to fixed little-endian int32.
            bytes = {2, 2, 0xff, 2, 0xfd, 0x7f, 0x80, 1,
                     0xfe, 0xff, 0xff, 0xff, 0xfc, 0xff, 0xff, 0xff, 3, 0, 0, 0};
        }
        proto::SubchunkRequestPacket packet;
        packet.dimension = 1;
        packet.origin = {-2, -4, 3};
        packet.requests = {{-1, 2, -3}, {127, -128, 1}};
        Encodes(packet, bytes, "request offsets retain dx/dy/dz despite the current x/y/z member names");
        proto::SubchunkRequestPacket received;
        Check(Decode(bytes, received) && received.requests.size() == 2,
              "literal old request packet is fully decoded");
        if (received.requests.size() == 2)
            Check(received.requests[0].x == -1 && received.requests[0].y == 2 && received.requests[0].z == -3 &&
                      received.requests[1].x == 127 && received.requests[1].y == -128 && received.requests[1].z == 1,
                  "both request offsets survive incoming conversion");
    }

    void TransitionalMapPresence() {
        auto present = Reply(false, false, true);
        present.entries[0].heightmapType = proto::HeightMapDataType::NoData;
        present.entries[0].renderHeightmapType = proto::HeightMapDataType::TooLow;
        auto bytes = ReplyBytes(false, false, true);
        bytes[23] = 0;  // type and option presence are independent in 2168
        bytes[281] = 3;
        Encodes(present, bytes, "2168 preserves present map bytes independently of the type enum");
        proto::SubchunkPacket received;
        Check(Decode(bytes, received) && received.entries.size() == 1 &&
                  received.entries[0].heightmap == present.entries[0].heightmap &&
                  received.entries[0].renderHeightmap == present.entries[0].renderHeightmap,
              "2168 receives map options even with NoData or TooLow type values");
        (*present.entries[0].renderHeightmap)[3].clear();
        std::vector<uint8_t> invalid;
        Check(!net::EncodeChecked(present, invalid),
              "2168 rejects malformed present render-map data even when its type is not HasData");

        auto absent = Reply(false, false, false);
        absent.entries[0].heightmapType = absent.entries[0].renderHeightmapType = proto::HeightMapDataType::HasData;
        bytes = ReplyBytes(false, false, false);
        bytes[23] = bytes[25] = 1;
        Encodes(absent, bytes, "2168 retains absent map options independently of HasData type values");
        Check(Decode(bytes, received) && received.entries.size() == 1 &&
                  !received.entries[0].heightmap && !received.entries[0].renderHeightmap,
              "2168 does not invent absent map contents from a HasData enum");
    }
}

int main() {
    using namespace perlin;
    for (int version : {944, 975, 1001}) {
        profile = version;
        const net::context::ScopedSettings context({version, -1});
        Columns();
        Replies();
        Requests();
    }
    // The newer optional/count representation remains byte-for-byte unchanged.
    for (int version : {2168, 2193, 2225}) {
        profile = version;
        const net::context::ScopedSettings context({version, -1});
        auto packet = Column();
        packet.subChunkCount = 2;
        packet.payload = {0xaa, 0xbb};
        std::vector<uint8_t> bytes{3, 6, 2, 2, 0, 0, 0, 2, 0xaa, 0xbb};
        if (version == 2225)
            bytes.push_back(0); // clientBiomeUpdate added in current protocol
        Encodes(packet, bytes, "newer ordinary-column layout is unchanged");
        proto::LevelChunkPacket received;
        Check(Decode(bytes, received) && !received.highestSubchunkCount && received.subChunkCount == 2,
              "newer ordinary columns retain absent request mode");
        if (version == 2168) {
            Replies();
            Requests();
            TransitionalMapPresence();
        }
    }
    std::printf("chunk_wire_selftest: %s (%d checks, %d failures)\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
