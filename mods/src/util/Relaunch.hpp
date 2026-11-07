#pragma once

namespace perlin {
    void RememberCommandLine(int argc, char** argv);
    bool RelaunchSelf(bool requested = false);
}
