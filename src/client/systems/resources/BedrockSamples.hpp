#pragma once
#include <string>

namespace perlin {
    class BedrockSamples {
    public:
        static bool Ensure(const std::string& targetDir);
        static bool Available(const std::string& targetDir);
    };
}
