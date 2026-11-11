#pragma once
// calls only a server answers; on the client they return Status::UnknownOp
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "perlin/rpc.hpp"

namespace perlin::server {
    using PlayerId = uint64_t;

    template <class... Args> Result<void> broadcast(std::format_string<Args...> fmt, Args&&... args) {
        return call<abi::Op::Broadcast>(abi::TextMessage{std::format(fmt, std::forward<Args>(args)...)});
    }

    template <class... Args>
    Result<void> message(PlayerId player, std::format_string<Args...> fmt, Args&&... args) {
        return call<abi::Op::PlayerMessage>(
            abi::PlayerText{player, std::format(fmt, std::forward<Args>(args)...)});
    }

    template <class... Args>
    Result<void> kick(PlayerId player, std::format_string<Args...> fmt, Args&&... args) {
        return call<abi::Op::Kick>(abi::PlayerText{player, std::format(fmt, std::forward<Args>(args)...)});
    }

    inline Result<std::vector<abi::OnlinePlayer>> players() {
        auto list = call<abi::Op::OnlinePlayers>();
        if (!list)
            return unexpected(list.error());
        return std::move(list->players);
    }

    inline Result<void> teleport(PlayerId player, double x, double y, double z) {
        return call<abi::Op::Teleport>(abi::ServerTeleport{player, x, y, z});
    }

    inline Result<void> set_block(int32_t x, int32_t y, int32_t z, std::string block) {
        return call<abi::Op::SetBlock>(abi::BlockWrite{x, y, z, std::move(block)});
    }

    inline Result<std::string> block(int32_t x, int32_t y, int32_t z) {
        auto name = call<abi::Op::BlockName>(abi::BlockCoord{x, y, z});
        if (!name)
            return unexpected(name.error());
        return std::move(name->text);
    }

    inline Result<abi::ServerStatus> info() {
        return call<abi::Op::ServerInfo>();
    }

    // the count that did not fit
    inline Result<uint32_t> give(PlayerId player, std::string item, uint32_t count = 1) {
        auto r = call<abi::Op::GiveItem>(abi::GiveItemRequest{player, std::move(item), count});
        if (!r)
            return unexpected(r.error());
        return r->leftover;
    }
}
