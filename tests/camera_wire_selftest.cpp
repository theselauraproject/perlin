#include "client/systems/multiplayer/CameraEasing.hpp"
#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {
    using namespace perlin;

    int checks = 0, failures = 0;

    void Check(bool ok, const char* message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL camera_wire: %s\n", message);
        }
    }

    bool Decode(std::span<const uint8_t> bytes, proto::CameraInstructionPacket& packet) {
        net::ReadStream stream(bytes.data(), bytes.size());
        return proto::Read(stream, packet) && stream.remaining() == 0;
    }

    struct FadeCase {
        bool time, color;
        std::vector<uint8_t> bytes;
    };

    // Packet 300 bodies, excluding packet ID. The native 2193 reflection writer
    // emits a bool for each nested option before its payload, as does 2225.
    const std::array fadeCases{
        FadeCase{true, true,
                 {0, 0, 1, 1,
                  0, 0, 0, 0x40, 0, 0, 0x40, 0x40, 0, 0, 0x80, 0x40,
                  1, 0, 0, 0x80, 0x3e, 0, 0, 0, 0x3f, 0, 0, 0x40, 0x3f,
                  0, 0, 0, 0, 0, 0}},
        FadeCase{true, false,
                 {0, 0, 1, 1,
                  0, 0, 0, 0x40, 0, 0, 0x40, 0x40, 0, 0, 0x80, 0x40,
                  0, 0, 0, 0, 0, 0, 0}},
        FadeCase{false, true,
                 {0, 0, 1, 0, 1,
                  0, 0, 0x80, 0x3e, 0, 0, 0, 0x3f, 0, 0, 0x40, 0x3f,
                  0, 0, 0, 0, 0, 0}},
        FadeCase{false, false, {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}},
    };

    void FadeWireCases() {
        for (const auto& test : fadeCases) {
            proto::CameraInstructionPacket decoded;
            const bool ok = Decode(test.bytes, decoded);
            Check(ok && decoded.fade && decoded.fade->timeData.has_value() == test.time &&
                      decoded.fade->colorRgb.has_value() == test.color,
                  "literal fade retains independent time/color presence");
            if (test.time) {
                Check(ok && decoded.fade && decoded.fade->timeData &&
                          decoded.fade->timeData->fadeInDuration == 2.0f &&
                          decoded.fade->timeData->waitDuration == 3.0f &&
                          decoded.fade->timeData->fadeOutDuration == 4.0f,
                      "literal fade retains all nondefault durations");
            }
            if (test.color) {
                Check(ok && decoded.fade && decoded.fade->colorRgb &&
                          decoded.fade->colorRgb->x == 0.25f &&
                          decoded.fade->colorRgb->y == 0.5f &&
                          decoded.fade->colorRgb->z == 0.75f,
                      "literal fade retains all color components");
            }

            // Construct a separate packet so the outgoing check is independent
            // of any mistake in the decoder.
            proto::CameraInstructionPacket outgoing;
            outgoing.fade.emplace();
            if (test.time) {
                auto& time = outgoing.fade->timeData.emplace();
                time.fadeInDuration = 2.0f;
                time.waitDuration = 3.0f;
                time.fadeOutDuration = 4.0f;
            }
            if (test.color)
                outgoing.fade->colorRgb = proto::Vec3F{0.25f, 0.5f, 0.75f};
            Check(net::Encode(outgoing) == test.bytes,
                  "fade encoding matches the independent literal payload");
        }

        const std::array<uint8_t, 9> absent{};
        proto::CameraInstructionPacket decoded;
        Check(Decode(absent, decoded) && !decoded.fade,
              "an absent fade remains distinct from a present empty fade");
        Check(net::Encode(proto::CameraInstructionPacket{}) ==
                  std::vector<uint8_t>(absent.begin(), absent.end()),
              "a default camera instruction emits nine absent options");

        const auto& complete = fadeCases.front().bytes;
        for (size_t length : {size_t{4}, size_t{10}, size_t{16}, size_t{17}, size_t{23}, size_t{28}}) {
            proto::CameraInstructionPacket truncated;
            Check(!Decode(std::span(complete.data(), length), truncated),
                  "truncated optional fade payload is rejected");
        }
    }

    struct FovCase {
        const char* ease;
        bool clear;
        std::vector<uint8_t> bytes;
    };

    // FOV 90, duration 2, length-prefixed easing name, clear flag, then three
    // absent options. These literal bytes are shared by the corrected profiles.
    const std::array fovCases{
        FovCase{"linear", false,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                 6, 'l', 'i', 'n', 'e', 'a', 'r', 0, 0, 0, 0}},
        FovCase{"in_quad", false,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                 7, 'i', 'n', '_', 'q', 'u', 'a', 'd', 0, 0, 0, 0}},
        FovCase{"in_out_elastic", true,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                 14, 'i', 'n', '_', 'o', 'u', 't', '_', 'e', 'l', 'a', 's', 't', 'i', 'c',
                 1, 0, 0, 0}},
        // These three fixtures test only lossless string encoding. Native enum
        // lookup accepts case-insensitive registered names and reports an error
        // for unknown/empty names; the generic string codec does not validate it.
        FovCase{"IN_QUAD", false,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                 7, 'I', 'N', '_', 'Q', 'U', 'A', 'D', 0, 0, 0, 0}},
        FovCase{"unknown", false,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                 7, 'u', 'n', 'k', 'n', 'o', 'w', 'n', 0, 0, 0, 0}},
        FovCase{"", false,
                {0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40, 0, 0, 0, 0, 0}},
    };

    void FovWireCases() {
        for (const auto& test : fovCases) {
            proto::CameraInstructionPacket decoded;
            const bool ok = Decode(test.bytes, decoded);
            Check(ok && decoded.fov && decoded.fov->fieldOfView == 90.0f &&
                      decoded.fov->easeTime == 2.0f && decoded.fov->easeType == test.ease &&
                      decoded.fov->clear == test.clear,
                  "literal FOV retains its string, duration, value and clear flag");
            proto::CameraInstructionPacket outgoing;
            auto& fov = outgoing.fov.emplace();
            fov.fieldOfView = 90.0f;
            fov.easeTime = 2.0f;
            fov.easeType = test.ease;
            fov.clear = test.clear;
            Check(net::Encode(outgoing) == test.bytes,
                  "FOV encoding matches the independent literal string payload");
        }

        auto truncated = fovCases[1].bytes;
        truncated[14] = 40;
        proto::CameraInstructionPacket decoded;
        Check(!Decode(truncated, decoded), "truncated FOV string is rejected");
        truncated = fovCases[1].bytes;
        truncated.resize(22);
        Check(!Decode(truncated, decoded), "missing FOV clear flag is rejected");
    }

    void EasingNames() {
        const std::array<std::string_view, 32> names{
            "linear", "spring", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
            "in_out_cubic", "in_quart", "out_quart", "in_out_quart", "in_quint", "out_quint",
            "in_out_quint", "in_sine", "out_sine", "in_out_sine", "in_expo", "out_expo",
            "in_out_expo", "in_circ", "out_circ", "in_out_circ", "in_bounce", "out_bounce",
            "in_out_bounce", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
            "in_out_elastic"};
        for (size_t i = 0; i < names.size(); ++i) {
            Check(CameraEaseIndex(names[i], 2193) == static_cast<int>(i) &&
                      CameraEaseIndex(names[i], 2225) == static_cast<int>(i),
                  "all native registered labels retain their easing index");
            std::string upper(names[i]);
            for (char& value : upper)
                if (value >= 'a' && value <= 'z')
                    value = static_cast<char>(value - ('a' - 'A'));
            Check(CameraEaseIndex(upper, 2193) == static_cast<int>(i),
                  "2193 accepts ASCII case-insensitive registered labels");
            Check(CameraEaseIndex(upper, 2225) == 0 && CameraEaseIndex(upper, 2168) == 0,
                  "case handling is not generalized to other protocol profiles");
        }
        Check(CameraEaseIndex("iN_qUaD", 2193) == 2,
              "mixed ASCII case maps to the native nonlinear curve");
        Check(CameraEaseIndex("unknown", 2193) == 0 && CameraEaseIndex("", 2193) == 0,
              "existing unknown/empty consumer fallback remains explicit");
        Check(CameraEaseIndex("inverse_lerp", 2193) == 32 &&
                  CameraEaseIndex("INVERSE_LERP", 2193) == 0,
              "case folding does not extend the native label set to the extra Perlin curve");
    }
}

int main() {
    using namespace perlin;
    const auto originalProtocol = net::context::Protocol();
    for (int protocol : {2193, 2225}) {
        const net::context::ScopedSettings profile({protocol, -1});
        FadeWireCases();
        FovWireCases();
    }
    EasingNames();
    Check(net::context::Protocol() == originalProtocol, "each profile scope restores the caller context");
    std::printf("camera_wire_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
