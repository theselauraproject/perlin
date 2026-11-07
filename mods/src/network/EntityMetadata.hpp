#pragma once
#include "protocol/Types.hpp"
#include <type_traits>

namespace perlin::net {
    inline bool MetadataInteger(const proto::MetadataDictionaryEntry& metadata, int64_t& out) {
        const auto integer = [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_integral_v<T>) {
                out = static_cast<int64_t>(value);
                return true;
            } else {
                return false;
            }
        };
        if (const auto* value = std::get_if<1>(&metadata.value))
            return integer(*value);
        if (const auto* value = std::get_if<2>(&metadata.value))
            return integer(*value);
        if (const auto* value = std::get_if<4>(&metadata.value))
            return std::visit(integer, *value);
        return false;
    }
}
