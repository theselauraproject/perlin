#pragma once
#include "network/FriendWorld.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin::lan {

    struct Host {
        uint64_t networkId = 0;
        std::string ip;
        uint16_t port = 0;
        std::string name;
        std::string version;
        std::string level;
        int protocol = 0;
        int players = 0, maxPlayers = 0;
        int gameType = 0;
        bool onlineAuth = false;
        bool selfSignedAuth = false;
    };

    class Session : public friendworld::Signaling {
    public:
        static std::shared_ptr<Session> Open(std::string& error);
        ~Session() override;

        void Scan();
        std::vector<Host> Hosts() const;
        bool WaitFor(uint64_t networkId, int timeoutMs);

        bool Send(const std::string& toNetworkId, const std::string& text, bool waitDelivery,
                  std::string& error) override;
        void SetHandler(std::function<void(const std::string&, const std::string&)> handler) override;
        bool Credentials(std::vector<friendworld::IceServerInfo>& servers, std::string& error) override;
        bool Alive() const override;
        void Close() override;

        struct Impl;

    private:
        Session();
        std::unique_ptr<Impl> impl;
    };

}
