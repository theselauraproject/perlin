#pragma once
#include <cstdint>
#include <string>

namespace perlin {
    struct DeviceProfile {
        std::string id;
        std::string model;
    };

    DeviceProfile LoadDeviceProfile();

    std::string DeriveSelfSignedId(int64_t clientRandomId, const std::string& deviceId);
}
