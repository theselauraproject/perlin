#pragma once
#include <string>

namespace perlin {
    class MojangFonts {
    public:
        static std::string UnihexPath(const std::string& dataDir);
        static bool Available(const std::string& dataDir);
        static bool Ensure(const std::string& dataDir);
    };
}
