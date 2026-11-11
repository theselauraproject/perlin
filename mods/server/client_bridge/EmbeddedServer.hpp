#pragma once
// client side of singleplayer: the server is linked into the client (every platform, the web included) and a world
// is exposed as an ordinary perlin::Transport, so BedrockSession joins it like any NetherNet server, without sockets
#include "network/Transport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin {

    class EmbeddedServer {
    public:
        EmbeddedServer();
        ~EmbeddedServer();
        EmbeddedServer(const EmbeddedServer&) = delete;
        EmbeddedServer& operator=(const EmbeddedServer&) = delete;

        // dataDir: world and config directory; configJson overrides config file keys (server.json names) (may be empty)
        bool Start(const std::string& dataDir, const std::string& configJson, std::string& error);
        void Stop();
        bool Running() const;
        // one tick on the caller's thread when `ownThread` was false
        void Tick();
        void Command(const std::string& line);

        // a transport for BedrockSession; null if the server is not running
        std::unique_ptr<Transport> Connect();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
