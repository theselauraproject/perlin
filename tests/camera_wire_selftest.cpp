#include "client/systems/multiplayer/CameraEasing.hpp"
#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <algorithm>
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

    template<class Packet> bool Decode(std::span<const uint8_t> bytes, Packet& packet) {
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

    void LegacyCameraBridgeCases() {
        // These fixtures describe the existing legacy schema without changing
        // its wire contract: flat fade times/color and a one-byte FOV enum.
        const std::vector<uint8_t> fadeBytes{
            0, 0, 1,
            0, 0, 0, 0x40, 0, 0, 0x40, 0x40, 0, 0, 0x80, 0x40,
            0, 0, 0x80, 0x3e, 0, 0, 0, 0x3f, 0, 0, 0x40, 0x3f,
            0, 0, 0, 0, 0, 0};
        proto::CameraInstructionPacket decoded;
        Check(Decode(fadeBytes, decoded) && decoded.fade && decoded.fade->timeData &&
                  decoded.fade->timeData->fadeInDuration == 2.0f &&
                  decoded.fade->timeData->waitDuration == 3.0f &&
                  decoded.fade->timeData->fadeOutDuration == 4.0f && decoded.fade->colorRgb &&
                  decoded.fade->colorRgb->x == 0.25f && decoded.fade->colorRgb->y == 0.5f &&
                  decoded.fade->colorRgb->z == 0.75f,
              "legacy flat fade retains timing and color through the current packet interface");
        proto::CameraInstructionPacket outgoing;
        outgoing.fade.emplace().timeData = proto::CameraInstructionPacketFadeTimeData{2, 3, 4};
        outgoing.fade->colorRgb = proto::Vec3F{0.25f, 0.5f, 0.75f};
        std::vector<uint8_t> encoded;
        Check(net::EncodeChecked(outgoing, encoded) && encoded == fadeBytes,
              "legacy flat fade encoding preserves the independently constructed nondefault times");
        const std::vector<uint8_t> defaultFadeBytes{
            0, 0, 1,
            0, 0, 0x80, 0x3f, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0};
        outgoing.fade->timeData.reset();
        outgoing.fade->colorRgb.reset();
        Check(net::EncodeChecked(outgoing, encoded) && encoded == defaultFadeBytes,
              "legacy encoding materializes default fade values when its wire cannot represent absence");
        outgoing.fade->timeData = proto::CameraInstructionPacketFadeTimeData{0, 0, 0};
        auto zeroFadeBytes = defaultFadeBytes;
        std::fill(zeroFadeBytes.begin() + 3, zeroFadeBytes.begin() + 15, uint8_t{0});
        Check(net::EncodeChecked(outgoing, encoded) && encoded == zeroFadeBytes,
              "explicit zero fade times remain zero and are not mistaken for absent defaults");
        Check(!Decode(std::span(fadeBytes.data(), size_t{20}), decoded),
              "truncated flat fade cannot become a successful current instruction");

        const std::array<std::string_view, 33> names{
            "linear", "spring", "in_quad", "out_quad", "in_out_quad", "in_cubic", "out_cubic",
            "in_out_cubic", "in_quart", "out_quart", "in_out_quart", "in_quint", "out_quint",
            "in_out_quint", "in_sine", "out_sine", "in_out_sine", "in_expo", "out_expo",
            "in_out_expo", "in_circ", "out_circ", "in_out_circ", "in_bounce", "out_bounce",
            "in_out_bounce", "in_back", "out_back", "in_out_back", "in_elastic", "out_elastic",
            "in_out_elastic", "inverse_lerp"};
        for (size_t i = 0; i < names.size(); ++i) {
            const bool clear = (i & 1u) != 0;
            const std::vector<uint8_t> bytes{
                0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40,
                static_cast<uint8_t>(i), static_cast<uint8_t>(clear), 0, 0, 0};
            Check(Decode(bytes, decoded) && decoded.fov && decoded.fov->fieldOfView == 90 &&
                      decoded.fov->easeTime == 2 && decoded.fov->easeType == names[i] &&
                      decoded.fov->clear == clear,
                  "every legacy FOV enum retains its corresponding current easing label");
            outgoing = {};
            auto& fov = outgoing.fov.emplace();
            fov.fieldOfView = 90;
            fov.easeTime = 2;
            fov.easeType = names[i];
            fov.clear = clear;
            Check(net::EncodeChecked(outgoing, encoded) && encoded == bytes,
                  "every current easing label emits its corresponding literal legacy FOV enum");
        }
        const std::vector<uint8_t> invalid{
            0, 0, 0, 0, 0, 1, 0, 0, 0xb4, 0x42, 0, 0, 0, 0x40, 255, 0, 0, 0, 0};
        Check(!Decode(invalid, decoded), "an unrepresentable legacy FOV enum is not silently converted to linear");
        outgoing.fov->easeType = "unregistered";
        Check(!net::EncodeChecked(outgoing, encoded),
              "an unrepresentable easing label cannot silently emit the legacy linear enum");
    }

    void PresetWireCases(int protocol) {
        // One preset named "a", parent minecraft:free. Position contains x=8
        // and z=-4 only; rotation contains yaw=45 only. These literal presence
        // bytes must survive independently, including an explicit false flag.
        std::vector<uint8_t> bytes{
            1, 1, 'a', 14, 'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':', 'f', 'r', 'e', 'e',
            1, 0, 0, 0, 0x41, 0, 1, 0, 0, 0x80, 0xc0,
            0, 1, 0, 0, 0x34, 0x42,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 0, 0, 0x70, 0xc1, 1, 0, 0, 0xf0, 0x41,
            1, 1, 1, 0, 0, 1, 2};
        if (protocol != 944)
            bytes.insert(bytes.end(), {1, 1, 0, 0, 0x20, 0x41, 0, 0, 0xa0, 0x41});
        proto::CameraPresetsPacket decoded;
        const bool ok = Decode(bytes, decoded);
        Check(ok && decoded.presets.size() == 1, "literal camera preset decodes fully");
        if (ok && decoded.presets.size() == 1) {
            const auto& p = decoded.presets[0];
            Check(p.name == "a" && p.parent == "minecraft:free" &&
                      p.position.x == 8.0f && !p.position.y && p.position.z == -4.0f &&
                      !p.rotation.x && p.rotation.y == 45.0f,
                  "preset wire preserves every position/rotation component's presence");
            Check(p.yawLimitMin == -15.0f && p.yawLimitMax == 30.0f &&
                      p.audioListener == 1 && p.playerEffects.has_value() && !*p.playerEffects &&
                      p.controlScheme == proto::CameraPresetsControlScheme::CameraRelativeStrafe,
                  "preset wire preserves yaw limits, listener, control scheme and explicit false effects");
            Check(protocol == 944 ? !p.applyInheritedStartingRotation && !p.startingRotation
                                  : p.applyInheritedStartingRotation && p.startingRotation &&
                                        p.startingRotation->x == 10.0f && p.startingRotation->z == 20.0f,
                  "starting rotation fields follow their actual profile availability");
        }
        proto::CameraPresetsPacket outgoing;
        auto& p = outgoing.presets.emplace_back();
        p.name = "a";
        p.parent = "minecraft:free";
        p.position.x = 8.0f;
        p.position.z = -4.0f;
        p.rotation.y = 45.0f;
        p.yawLimitMin = -15.0f;
        p.yawLimitMax = 30.0f;
        p.audioListener = 1;
        p.playerEffects = false;
        p.controlScheme = proto::CameraPresetsControlScheme::CameraRelativeStrafe;
        if (protocol != 944) {
            p.applyInheritedStartingRotation = true;
            p.startingRotation = proto::Vec2F{10.0f, 20.0f};
        }
        std::vector<uint8_t> encoded;
        Check(net::EncodeChecked(outgoing, encoded) && encoded == bytes,
              "separately constructed preset matches its independent literal bytes");
        bytes.pop_back();
        Check(!Decode(bytes, decoded), "truncated final preset field is rejected");
    }

    void SetWireCases() {
        // Preset3, InOutSine ease2sec, pos(1,2,3), rot(-10,20), no facing,
        // view offset(.25,-.5), entity offset(4,5,6), explicit default=false.
        std::vector<uint8_t> bytes{
            1, 3, 0, 0, 0, 1, 16, 0, 0, 0, 0x40,
            1, 0, 0, 0x80, 0x3f, 0, 0, 0, 0x40, 0, 0, 0x40, 0x40,
            1, 0, 0, 0x20, 0xc1, 0, 0, 0xa0, 0x41,
            0, 1, 0, 0, 0x80, 0x3e, 0, 0, 0, 0xbf,
            1, 0, 0, 0x80, 0x40, 0, 0, 0xa0, 0x40, 0, 0, 0xc0, 0x40,
            1, 0};
        bytes.push_back(1); // removeIgnoreStartingValues
        bytes.insert(bytes.end(), 8, 0);
        proto::CameraInstructionPacket decoded;
        const bool ok = Decode(bytes, decoded);
        Check(ok && decoded.instructionSet, "literal set instruction decodes fully");
        if (ok && decoded.instructionSet) {
            const auto& p = *decoded.instructionSet;
            Check(p.runtimeId == 3 && p.easeData && p.easeData->type == proto::EaseType::InOutSine &&
                      p.easeData->duration == 2.0f && p.position && p.position->z == 3.0f &&
                      p.rotation && p.rotation->x == -10.0f && p.rotation->z == 20.0f && !p.facing,
                  "set wire preserves preset, easing, position and pitch/yaw");
            Check(p.offset && p.offset->x == 0.25f && p.offset->z == -0.5f &&
                      p.entityOffset && p.entityOffset->y == 5.0f && p.default_.has_value() && !*p.default_ &&
                      p.removeIgnoreStartingValues,
                  "set wire preserves offsets, false default and starting-value reset");
        }
        proto::CameraInstructionPacket outgoing;
        auto& p = outgoing.instructionSet.emplace();
        p.runtimeId = 3;
        p.easeData = proto::CameraInstructionPacketInstructionSetEaseData{proto::EaseType::InOutSine, 2.0f};
        p.position = proto::Vec3F{1, 2, 3};
        p.rotation = proto::Vec2F{-10, 20};
        p.offset = proto::Vec2F{0.25f, -0.5f};
        p.entityOffset = proto::Vec3F{4, 5, 6};
        p.default_ = false;
        p.removeIgnoreStartingValues = true;
        std::vector<uint8_t> encoded;
        Check(net::EncodeChecked(outgoing, encoded) && encoded == bytes,
              "separately constructed set instruction matches independent literal bytes");
        bytes.resize(18);
        Check(!Decode(bytes, decoded), "truncated set position cannot produce a complete instruction");
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
        PresetWireCases(protocol);
        SetWireCases();
    }
    {
        const net::context::ScopedSettings profile({944, -1});
        PresetWireCases(944);
        SetWireCases();
    }
    for (int protocol : {944, 975, 1001, 2168}) {
        const net::context::ScopedSettings profile({protocol, -1});
        LegacyCameraBridgeCases();
    }
    EasingNames();
    Check(net::context::Protocol() == originalProtocol, "each profile scope restores the caller context");
    std::printf("camera_wire_selftest: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
