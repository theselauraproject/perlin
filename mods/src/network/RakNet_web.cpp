#include "network/RakNet.hpp"

namespace perlin {
    struct RakNet::Impl {};

    RakNet::RakNet() = default;
    RakNet::~RakNet() = default;

    bool RakNet::Ping(const std::string&, uint16_t, std::string&, std::string& error, int) {
        error = "RakNet needs UDP sockets, which browsers do not provide";
        return false;
    }

    bool RakNet::Discover(std::vector<LanServer>&, std::string& error, int) {
        error = "LAN discovery needs UDP sockets, which browsers do not provide";
        return false;
    }

    bool RakNet::Connect(const std::string&, uint16_t) {
        lastError =
            "RakNet needs UDP sockets, which browsers do not provide - use a NetherNet (WebRTC) server";
        return false;
    }

    void RakNet::Disconnect() {
        connected = false;
    }

    bool RakNet::SendReliable(const std::vector<uint8_t>&) {
        return false;
    }

    bool RakNet::TryReceive(std::vector<uint8_t>&) {
        return false;
    }

    std::string RakNet::LastError() const {
        return lastError;
    }
}
