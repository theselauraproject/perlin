#include "network/PacketDispatcher.hpp"
#include "util/VarInt.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

template <class T> static bool ReadWire(wire::Reader& in, T& value) {
    if constexpr (std::is_same_v<T, uint32_t>)
        return in.ReadVarUint32(value);
    else
        return in.ReadVarUint64(value);
}

template <class T> static void CheckValid(const std::vector<uint8_t>& bytes, T expected) {
    std::vector<uint8_t> framed = bytes;
    framed.push_back(0x42);
    T value = 13;
    net::ReadStream stream(framed.data(), framed.size());
    stream.varint(value);
    CHECK(stream.ok && value == expected && stream.remaining() == 1 && *stream.position() == 0x42);
    wire::Reader reader(framed.data(), framed.size());
    value = 13;
    CHECK(ReadWire(reader, value) && value == expected && reader.Remaining() == 1 && *reader.Cursor() == 0x42);
}

template <class T> static void CheckInvalid(const std::vector<uint8_t>& bytes) {
    T value = 13;
    net::ReadStream stream(bytes.data(), bytes.size());
    stream.varint(value);
    CHECK(!stream.ok && value == 13);
    wire::Reader reader(bytes.data(), bytes.size());
    CHECK(!ReadWire(reader, value) && value == 13);
}

static void TestVarints() {
    CheckValid<uint32_t>({0x00}, 0);
    CheckValid<uint32_t>({0x7F}, 127);
    CheckValid<uint32_t>({0x80, 0x01}, 128);
    CheckValid<uint32_t>({0xFF, 0xFF, 0xFF, 0xFF, 0x0F}, std::numeric_limits<uint32_t>::max());
    CheckValid<uint64_t>({0x80, 0x80, 0x80, 0x80, 0x10}, uint64_t{1} << 32);
    CheckValid<uint64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01},
                         std::numeric_limits<uint64_t>::max());
    // Non-minimal encodings that still fit the declared width remain accepted.
    CheckValid<uint32_t>({0x80, 0x00}, 0);
    CheckValid<uint64_t>({0x80, 0x00}, 0);
    CheckInvalid<uint32_t>({});
    CheckInvalid<uint32_t>({0x80});
    CheckInvalid<uint32_t>({0x80, 0x80, 0x80, 0x80, 0x10});
    CheckInvalid<uint32_t>({0xFF, 0xFF, 0xFF, 0xFF, 0x7F});
    CheckInvalid<uint32_t>({0x80, 0x80, 0x80, 0x80, 0x80, 0x00});
    CheckInvalid<uint64_t>({0x80});
    CheckInvalid<uint64_t>({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02});
    CheckInvalid<uint64_t>({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F});
    CheckInvalid<uint64_t>({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00});

    for (const auto& bytes : {std::vector<uint8_t>{0x80},
                              std::vector<uint8_t>{0x80, 0x80, 0x80, 0x80, 0x10}}) {
        int32_t value = -17;
        net::ReadStream stream(bytes.data(), bytes.size());
        stream.zigzag(value);
        CHECK(!stream.ok && value == -17);
    }

    const std::vector<uint8_t> signedMax{0xFE, 0xFF, 0xFF, 0xFF, 0x0F};
    const std::vector<uint8_t> signedMin{0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    for (const auto* bytes : {&signedMin, &signedMax}) {
        int32_t value = 0;
        const int32_t expected = bytes == &signedMin ? std::numeric_limits<int32_t>::min()
                                                    : std::numeric_limits<int32_t>::max();
        net::ReadStream stream(bytes->data(), bytes->size());
        stream.zigzag(value);
        CHECK(stream.ok && value == expected && stream.remaining() == 0);
        wire::Reader reader(bytes->data(), bytes->size());
        CHECK(reader.ReadZigzag32(value) && value == expected && reader.Remaining() == 0);
    }
}

static void TestVarint128() {
    std::vector<uint8_t> maximum(19, 0xFF);
    maximum.back() = 0x03;
    std::array<uint64_t, 2> value{};
    net::ReadStream stream(maximum.data(), maximum.size());
    stream.varint128(value);
    CHECK(stream.ok && stream.remaining() == 0);
    CHECK(value[0] == std::numeric_limits<uint64_t>::max() &&
          value[1] == std::numeric_limits<uint64_t>::max());
    std::vector<uint8_t> encoded;
    net::WriteStream writer(encoded);
    writer.varint128(value);
    CHECK(writer.ok && encoded == maximum);

    const std::array<uint64_t, 2> sentinel{17, 29};
    for (const uint8_t last : {0x04, 0x7F, 0x80}) {
        auto invalid = maximum;
        invalid.back() = last;
        invalid.push_back(0x00);
        value = sentinel;
        net::ReadStream bad(invalid.data(), invalid.size());
        bad.varint128(value);
        CHECK(!bad.ok && value == sentinel && bad.remaining() == 1);
    }

    std::vector<uint8_t> bit64(10, 0x80);
    bit64.back() = 0x02;
    net::ReadStream high(bit64.data(), bit64.size());
    high.varint128(value);
    CHECK(high.ok && high.remaining() == 0 && value[0] == 0 && value[1] == 1);
}

namespace {
    struct ValuePacket {
        int32_t value = 0;
        static constexpr uint32_t kId = 1;
        static constexpr const char* kName = "wire_selftest_value";
    };

    bool Read(net::ReadStream& stream, ValuePacket& packet) {
        stream.zigzag(packet.value);
        return stream.ok;
    }
}

static void TestPartialDispatch() {
    net::PacketDispatcher dispatcher;
    std::string order;
    int errors = 0;
    int strictValue = -1;
    dispatcher.OnPartial<ValuePacket>([&](const ValuePacket&) { order += 'a'; });
    dispatcher.On<ValuePacket>([&](const ValuePacket& p) {
        strictValue = p.value;
        order += 'b';
    });
    dispatcher.OnPartial<ValuePacket>([&](const ValuePacket&) { order += 'c'; });
    dispatcher.On<ValuePacket>([&](const ValuePacket&) { order += 'd'; });
    dispatcher.OnFailure([&](uint32_t id, const char* name, size_t size) {
        CHECK(id == ValuePacket::kId && std::string(name) == ValuePacket::kName && size == 1);
        ++errors;
    });

    const std::array<uint8_t, 1> valid{0x02};
    CHECK(dispatcher.Dispatch(ValuePacket::kId, valid.data(), valid.size()) ==
          net::PacketDispatcher::Result::Handled);
    CHECK(order == "abcd" && strictValue == 1 && errors == 0);
    order.clear();
    const std::array<uint8_t, 1> truncated{0x80};
    CHECK(dispatcher.Dispatch(ValuePacket::kId, truncated.data(), truncated.size()) ==
          net::PacketDispatcher::Result::Malformed);
    CHECK(order == "ac" && strictValue == 1 && errors == 1);
}

static void TestCodecContext() {
    net::context::SetProtocol(1001);
    net::context::SetShieldItemID(12);
    {
        const net::context::ScopedSettings first({2193, 17});
        CHECK(net::context::Protocol() == 2193 && net::context::ShieldItemID() == 17);
        net::context::SetProtocol(944);
        net::context::SetShieldItemID(99);
        CHECK(net::context::Protocol() == 2193 && net::context::ShieldItemID() == 17);
        {
            const net::context::ScopedSettings second({2225, 24});
            CHECK(net::context::Protocol() == 2225 && net::context::ShieldItemID() == 24);
            net::context::UpdateBoundShieldItemID(25);
            CHECK(net::context::ShieldItemID() == 25);
        }
        CHECK(net::context::Protocol() == 2193 && net::context::ShieldItemID() == 17);
    }
    CHECK(net::context::Protocol() == 944 && net::context::ShieldItemID() == 99);
    try {
        const net::context::ScopedSettings throwing({2193, 17});
        throw 1;
    } catch (int) {
        CHECK(net::context::Protocol() == 944 && net::context::ShieldItemID() == 99);
    }
    net::context::SetProtocol(0);
    net::context::SetShieldItemID(-1);
}

int main() {
    TestVarints();
    TestVarint128();
    TestPartialDispatch();
    TestCodecContext();
    std::printf(failures ? "%d wire checks failed\n" : "wire_selftest: ok\n", failures);
    return failures ? 1 : 0;
}
