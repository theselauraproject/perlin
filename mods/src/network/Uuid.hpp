#pragma once
#include "network/Stream.hpp"

#include <random>

namespace perlin::net {

    inline Uuid RandomUuidV4() {
        static std::random_device device;
        static std::mt19937_64 rng((static_cast<uint64_t>(device()) << 32) | device());
        uint8_t raw[16];
        for (uint8_t& b : raw)
            b = static_cast<uint8_t>(rng());
        raw[6] = static_cast<uint8_t>((raw[6] & 0x0F) | 0x40);
        raw[8] = static_cast<uint8_t>((raw[8] & 0x3F) | 0x80);
        Uuid uuid;
        for (int i = 0; i < 8; i++)
            uuid.bytes[i] = raw[15 - i];
        for (int i = 0; i < 8; i++)
            uuid.bytes[8 + i] = raw[7 - i];
        return uuid;
    }

}
