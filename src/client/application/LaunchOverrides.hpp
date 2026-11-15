#pragma once
#include "client/systems/config/Options.hpp"
#include "client/platform/LaunchArgs.hpp"

#include <string>

namespace perlin {

    struct LaunchEffects {
        bool signOut = false;
    };

    LaunchEffects ApplyLaunchOverrides(Options& options, const LaunchArgs& args);

    struct ServerText {
        std::string text;
        bool secure = false;
    };

    ServerText NormalizeServerText(std::string text, bool secure);

    uint16_t DefaultPortFor(const std::string& text, bool secure, bool explicitTarget, uint16_t port);

}
