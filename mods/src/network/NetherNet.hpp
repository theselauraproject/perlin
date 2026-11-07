#pragma once
#include "network/ClientIdentity.hpp"
#include "network/Transport.hpp"
#include <atomic>
#include <concurrentqueue.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin {

    namespace friendworld {
        struct Lease;
    }

    class NetherNet : public Transport {
    public:
        static constexpr size_t kFragmentSize = 262143;

        using PacketQueue = moodycamel::ConcurrentQueue<std::vector<uint8_t>>;

        NetherNet();
        ~NetherNet() override;

        NetherNet(const NetherNet&) = delete;
        NetherNet& operator=(const NetherNet&) = delete;

        static void SetSecureSignaling(bool secure);
        static void SetIceServers(const std::string& servers);
        static bool SecureSignaling();

        static bool Probe(const std::string& host, uint16_t port, NetherNetServerInfo& info,
                          std::string& error, int timeoutSecs = 15);

        bool Connect(const std::string& host, uint16_t port, const ClientIdentity& identity);
        bool ConnectFriend(friendworld::Lease& lease, const ClientIdentity& identity);
        void Disconnect() override;

        bool SendReliable(const std::vector<uint8_t>& data) override;
        bool SendUnreliable(const std::vector<uint8_t>& data) override;
        bool SendPacket(const std::vector<uint8_t>& data) { return SendReliable(data); }

        bool TryReceive(std::vector<uint8_t>& out) override;

#ifdef __EMSCRIPTEN__
        void OnWebMessage(bool reliable, const uint8_t* data, size_t size);
        void OnWebClosed();
#endif

        bool IsConnected() const override { return connected; }
        std::string LastError() const override { return lastError; }
        const NetherNetServerInfo& Info() const override { return serverInfo; }

    private:
        void OnReliableMessage(const uint8_t* data, size_t size);
        void OnUnreliableMessage(const uint8_t* data, size_t size);

        std::shared_ptr<int> life = std::make_shared<int>(0);
        std::atomic<bool> connected{false};
        std::string lastError;
        NetherNetServerInfo serverInfo;

        struct Impl;
        std::unique_ptr<Impl> impl;

        PacketQueue inbound;
        std::vector<uint8_t> reassembly;
        std::mutex reassemblyMutex;
    };

}
