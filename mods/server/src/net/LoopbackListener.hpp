#pragma once
#include "net/Connection.hpp"

#include <atomic>
#include <mutex>

namespace perlin::server {

    // in-process transport for embedded singleplayer: the client side pushes and pulls whole messages,
    // no sockets, no webrtc; the server sees the same Connection it would get from nethernet
    class LoopbackListener final : public Listener {
    public:
        class Endpoint;

        const char* Name() const override { return "loopback"; }
        bool Start(EventQueue& queue, std::string& error) override;
        void Stop() override;

        // client side handle; null when the listener is not running
        std::shared_ptr<Endpoint> Connect();

        // state both sides hold, so neither can outlive the other's pointer
        struct Shared {
            std::mutex mutex;
            std::deque<std::vector<uint8_t>> inbox;
            std::atomic<bool> alive{true};
        };

        class Endpoint {
        public:
            ~Endpoint() { Close(); }
            // client -> server
            bool Send(const uint8_t* data, size_t size);
            // server -> client, false when nothing is waiting
            bool Receive(std::vector<uint8_t>& out);
            void Close();
            bool Alive() const { return shared->alive.load(); }

        private:
            friend class LoopbackListener;
            std::shared_ptr<Shared> shared = std::make_shared<Shared>();
            std::shared_ptr<Connection> peer;
            EventQueue* queue = nullptr;
        };

    private:
        EventQueue* events = nullptr;
        std::atomic<uint64_t> nextId{1};
        std::mutex mutex;
        std::vector<std::weak_ptr<Endpoint>> endpoints;
    };
}
