#pragma once
#include <cstddef>
#include <cstdint>

namespace perlin::packetlog {
    void Record(bool outgoing, uint32_t id, const uint8_t* data, size_t size);
}
