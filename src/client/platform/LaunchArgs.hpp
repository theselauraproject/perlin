#pragma once
#include <cstdint>
#include <string>

namespace perlin {
    struct LaunchArgs {
        std::string server;
        std::string playerName;
        std::string skin;
        std::string cape;
        bool slimArms = false;
        bool login = false;
        bool offline = false;
        std::string transport;
        bool logout = false;
        bool secure = false;
        bool acceptEula = false;
        std::string stun;
        bool hidden = false;
        int controlPort = 0;
        double benchmarkSeconds = 0;
        uint64_t benchmarkSeed = 0;
        std::string benchmarkOut;
    };
}
