#pragma once
#include "protocol/Types.hpp"

#include <cstdint>

namespace perlin {

    constexpr float kPlayerEyeHeight = 1.62001f;

    namespace InputFlag {
        constexpr uint32_t Jumping = static_cast<uint32_t>(proto::InputData::Jumping);
        constexpr uint32_t Up = static_cast<uint32_t>(proto::InputData::Up);
        constexpr uint32_t Down = static_cast<uint32_t>(proto::InputData::Down);
        constexpr uint32_t Left = static_cast<uint32_t>(proto::InputData::Left);
        constexpr uint32_t Right = static_cast<uint32_t>(proto::InputData::Right);
        constexpr uint32_t WantUp = static_cast<uint32_t>(proto::InputData::WantUp);
        constexpr uint32_t WantDown = static_cast<uint32_t>(proto::InputData::WantDown);
        constexpr uint32_t PerformBlockActions = static_cast<uint32_t>(proto::InputData::BlockAction);
        constexpr uint32_t HandledTeleport = static_cast<uint32_t>(proto::InputData::HandledTeleport);
        constexpr uint32_t StartFlying = static_cast<uint32_t>(proto::InputData::StartFlying);
        constexpr uint32_t StopFlying = static_cast<uint32_t>(proto::InputData::StopFlying);
        constexpr uint32_t MissedSwing = static_cast<uint32_t>(proto::InputData::MissedSwing);
        constexpr uint32_t StartUsingItem = static_cast<uint32_t>(proto::InputData::StartUsingItem);
    }

}
