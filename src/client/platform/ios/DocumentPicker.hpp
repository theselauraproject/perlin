#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::ios {

    bool PickDocument(const std::string& kind, std::function<void(std::vector<uint8_t>, std::string)> done);
}
