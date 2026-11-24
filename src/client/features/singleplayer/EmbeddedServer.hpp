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

        struct SetupStatus {
            bool starting = false;
            std::string text;
            float progress = -1.0f;
        };

        bool Start(const std::string& dataDir, const std::string& configJson, std::string& error);
        // Starts Perlite on its own thread; Connect() can be called right away and its transport waits
        // for the server before the first packet.
        void StartAsync(const std::string& dataDir, const std::string& configJson);
        void Stop();
        bool Running() const;
        bool Starting() const;
        SetupStatus Status() const;

        void Tick();
        void Command(const std::string& line);

        std::unique_ptr<Transport> Connect();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
