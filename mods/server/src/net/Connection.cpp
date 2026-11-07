#include "net/Connection.hpp"

#include <algorithm>

namespace perlin::server {

    std::vector<std::vector<uint8_t>> SplitFragments(const uint8_t* data, size_t size) {
        std::vector<std::vector<uint8_t>> out;
        if (size == 0)
            return out;
        const size_t count = (size - 1) / kFragmentSize + 1;
        if (count > 256)
            return out;
        for (size_t i = 0; i < count; i++) {
            const size_t off = i * kFragmentSize;
            const size_t len = std::min(kFragmentSize, size - off);
            std::vector<uint8_t> part;
            part.reserve(len + 1);
            part.push_back(static_cast<uint8_t>(count - 1 - i));
            part.insert(part.end(), data + off, data + off + len);
            out.push_back(std::move(part));
        }
        return out;
    }
}
