#include "client/application/LaunchOverrides.hpp"

namespace perlin {

    LaunchEffects ApplyLaunchOverrides(Options& options, const LaunchArgs& args) {
        LaunchEffects effects;
        if (!args.server.empty())
            options.server = args.server;
        if (!args.playerName.empty())
            options.playerName = args.playerName;
        if (args.logout) {
            effects.signOut = true;
            options.onlineLogin = false;
        }
        if (args.login)
            options.onlineLogin = true;
        return effects;
    }

    ServerText NormalizeServerText(std::string text, bool secure) {
        if (text.starts_with("https://")) {
            text.erase(0, 8);
            secure = true;
        } else if (text.starts_with("http://")) {
            text.erase(0, 7);
        }
        while (!text.empty() && text.back() == '/')
            text.pop_back();
        return {std::move(text), secure};
    }

    uint16_t DefaultPortFor(const std::string& text, bool secure, bool explicitTarget, uint16_t port) {
        if (!explicitTarget && secure && text.rfind(':') == std::string::npos)
            return 443;
        return port;
    }

}
