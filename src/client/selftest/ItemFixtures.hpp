#pragma once

#include "network/Stream.hpp"
#include "protocol/Packets.hpp"

#include <string>
#include <utility>

namespace perlin::selftest {
    inline proto::ItemstatesEntry CooldownItem(std::string name, int16_t runtimeId,
                                              std::string category, std::string type = "use") {
        proto::ItemstatesEntry item;
        item.name = std::move(name);
        item.runtimeId = runtimeId;
        item.componentBased = true;
        item.version = proto::ItemstatesEntryVersion::DataDriven;
        net::WriteStream stream(item.nbt.bytes);
        const auto byte = [&](uint8_t value) { stream.u8(value); };
        const auto text = [&](std::string value) {
            uint32_t length = static_cast<uint32_t>(value.size());
            stream.varint(length);
            stream.chars(value, value.size());
        };
        byte(10);
        text("");
        byte(10);
        text("components");
        byte(10);
        text("minecraft:cooldown");
        byte(8);
        text("category");
        text(std::move(category));
        byte(8);
        text("type");
        text(std::move(type));
        byte(0);
        byte(0);
        byte(0);
        return item;
    }
}
