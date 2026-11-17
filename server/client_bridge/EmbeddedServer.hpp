#pragma once

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

        bool Start(const std::string& dataDir, const std::string& configJson, std::string& error);
        void Stop();
        bool Running() const;

        void Tick();
        void Command(const std::string& line);

        std::unique_ptr<Transport> Connect();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
