#include "client/application/LaunchOverrides.hpp"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
    int failures = 0;
    void Check(bool ok, const char* what, int line) {
        if (!ok) {
            std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
            failures++;
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    struct Reference {
        std::string server, playerName;
        bool onlineLogin = false;
        bool signedOut = false;
    };

    void ReferenceApply(Reference& options, const perlin::LaunchArgs& args) {
        if (!args.server.empty())
            options.server = args.server;
        if (!args.playerName.empty())
            options.playerName = args.playerName;
        if (args.logout) {
            options.signedOut = true;
            options.onlineLogin = false;
        }
        if (args.login)
            options.onlineLogin = true;
    }

    void ReferenceConnect(const std::string& optionsServer, bool argsSecure, std::string& serverText,
                          bool& secureSignaling) {
        serverText = optionsServer;
        secureSignaling = argsSecure;
        if (serverText.starts_with("https://")) {
            serverText.erase(0, 8);
            secureSignaling = true;
        } else if (serverText.starts_with("http://")) {
            serverText.erase(0, 7);
        }
        while (!serverText.empty() && serverText.back() == '/')
            serverText.pop_back();
    }

    uint16_t ReferencePort(const std::string& serverText, bool secureSignaling, bool friendLease, bool singleplayer,
                           uint16_t port) {
        if (!friendLease && !singleplayer && secureSignaling && serverText.rfind(':') == std::string::npos)
            port = 443;
        return port;
    }
}

int main() {
    const std::vector<std::string> pool = {"",          "host",         "host:19132",    "https://a.b",
                                           "https://a.b:8443/", "http://x/",  "http://",       "https://",
                                           "///",       "a//",          "[::1]:1",       "https://[::1]/"};
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        perlin::LaunchArgs args;
        args.server = pool[rng() % pool.size()];
        args.playerName = rng() % 3 ? "" : "Steve";
        args.login = rng() & 1;
        args.logout = rng() & 1;
        args.secure = rng() & 1;

        perlin::Options options;
        Reference ref;
        options.server = ref.server = pool[rng() % pool.size()];
        options.playerName = ref.playerName = rng() & 1 ? "Perlin" : "Alex";
        options.onlineLogin = ref.onlineLogin = rng() & 1;

        const perlin::LaunchEffects effects = perlin::ApplyLaunchOverrides(options, args);
        ReferenceApply(ref, args);
        CHECK(options.server == ref.server);
        CHECK(options.playerName == ref.playerName);
        CHECK(options.onlineLogin == ref.onlineLogin);
        CHECK(effects.signOut == ref.signedOut);

        std::string text;
        bool secure = false;
        ReferenceConnect(options.server, args.secure, text, secure);
        const perlin::ServerText got = perlin::NormalizeServerText(options.server, args.secure);
        CHECK(got.text == text);
        CHECK(got.secure == secure);

        const bool lease = rng() & 1, single = rng() & 1;
        const uint16_t port = static_cast<uint16_t>(rng());
        CHECK(perlin::DefaultPortFor(text, secure, lease || single, port) ==
              ReferencePort(text, secure, lease, single, port));
    }
    CHECK(perlin::NormalizeServerText("https://a.b/", false).secure);
    CHECK(perlin::DefaultPortFor("a.b", true, false, 19132) == 443);
    CHECK(perlin::DefaultPortFor("a.b:1", true, false, 19132) == 19132);
    CHECK(perlin::DefaultPortFor("a.b", true, true, 0) == 0);

    if (failures) {
        std::fprintf(stderr, "launch_overrides_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("launch_overrides_selftest passed");
    return 0;
}
