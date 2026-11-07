#pragma once
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

namespace perlin::game {
    using abi::CameraControlState;
    using abi::CameraInfo;
    using abi::ModelPose;
    using abi::ScreenPoint;
    using abi::WorldBox;
    using abi::WorldLine;

    inline Result<void> camera(const CameraControlState& state) {
        abi::Writer w;
        w.write(state);
        return detail::call_void(Op::CameraControl, w);
    }
    inline Result<CameraInfo> camera_info() {
        return detail::call_as<CameraInfo>(Op::CameraState);
    }

    inline Result<ScreenPoint> to_screen(double x, double y, double z) {
        abi::Writer w;
        w.write(abi::WorldPoint{x, y, z});
        return detail::call_as<ScreenPoint>(Op::WorldToScreen, w);
    }

    inline Result<void> set_velocity(double x, double y, double z) {
        abi::Writer w;
        w.write(abi::MotionRequest{false, x, y, z, true, true, true});
        return detail::call_void(Op::PlayerMotion, w);
    }
    inline Result<void> add_velocity(double x, double y, double z) {
        abi::Writer w;
        w.write(abi::MotionRequest{true, x, y, z, true, true, true});
        return detail::call_void(Op::PlayerMotion, w);
    }
    inline Result<void> motion(const abi::MotionRequest& request) {
        abi::Writer w;
        w.write(request);
        return detail::call_void(Op::PlayerMotion, w);
    }
    inline Result<void> teleport(double x, double y, double z, bool keep_velocity = false) {
        abi::Writer w;
        w.write(abi::TeleportRequest{x, y, z, keep_velocity});
        return detail::call_void(Op::PlayerTeleport, w);
    }
    inline Result<void> set_look(double x, double y, double z) {
        abi::Writer w;
        w.write(abi::LookSet{x, y, z});
        return detail::call_void(Op::PlayerSetLook, w);
    }
    inline Result<void> model_pose(const ModelPose& pose) {
        abi::Writer w;
        w.write(pose);
        return detail::call_void(Op::PlayerModelPose, w);
    }

    inline Result<void> lines(const std::vector<WorldLine>& list, int ticks = 2) {
        abi::Writer w;
        w.write(abi::WorldLines{ticks, list});
        return detail::call_void(Op::WorldLines, w);
    }
    inline Result<void> boxes(const std::vector<WorldBox>& list, int ticks = 2) {
        abi::Writer w;
        w.write(abi::WorldBoxes{ticks, list});
        return detail::call_void(Op::WorldBoxes, w);
    }

    inline Result<uint32_t> find_block(std::string_view name,
                                       std::vector<std::pair<std::string, std::string>> states = {}) {
        abi::Writer w;
        w.write(abi::BlockQuery{std::string(name), std::move(states)});
        auto r = detail::call_as<abi::BlockQueryResult>(Op::ClientBlockGet, w);
        if (!r)
            return unexpected(r.error());
        if (!r->found)
            return unexpected(Error{Status::NotFound});
        return r->runtimeId;
    }
    inline Result<void> set_blocks(const std::vector<abi::ClientBlockEdit>& edits) {
        abi::Writer w;
        w.write(abi::ClientBlockEdits{edits});
        return detail::call_void(Op::ClientBlockSet, w);
    }

    inline Result<void> drop_packets(const std::vector<uint32_t>& ids, int ticks = 20,
                                     bool outgoing = false) {
        abi::Writer w;
        w.write(abi::PacketDropRequest{ticks, ids, outgoing});
        return detail::call_void(Op::PacketDrop, w);
    }
    inline Result<std::vector<abi::PacketCount>> packet_counters() {
        return detail::call_list<abi::PacketCount>(Op::PacketCounters);
    }

    inline Result<void> inject_packet(uint32_t id, std::span<const uint8_t> body) {
        abi::Writer w;
        w.put(id).bytes(body.data(), body.size());
        return detail::call_void(Op::PacketInject, w);
    }
}
