#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin {

    constexpr int kBedrockProtocol = 2225;
    constexpr const char* kBedrockVersion = "1.26.60";
    constexpr int kOldestBedrockProtocol = 944;

    struct NetherNetServerInfo {
        std::string name;
        int protocol = 0;
        std::string version;
        int advertisedProtocol = 0;
        std::string advertisedVersion;
        std::string level;
        int players = 0;
        int maxPlayers = 0;
        int gameType = 0;
    };

    class Transport {
    public:
        virtual ~Transport() = default;

        virtual void Disconnect() = 0;
        virtual bool SendReliable(const std::vector<uint8_t>& data) = 0;
        virtual bool SendUnreliable(const std::vector<uint8_t>& data) = 0;
        virtual bool TryReceive(std::vector<uint8_t>& out) = 0;
        virtual bool IsConnected() const = 0;
        virtual bool HasLivenessTimeout() const { return false; }
        virtual std::string LastError() const = 0;
        virtual const NetherNetServerInfo& Info() const = 0;
    };

}
