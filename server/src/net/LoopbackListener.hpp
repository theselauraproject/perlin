#pragma once
#include "net/Connection.hpp"

#include <atomic>
#include <mutex>

namespace perlin::server {

    class LoopbackListener final : public Listener {
    public:
        class Endpoint;

        const char* Name() const override { return "loopback"; }
        bool Start(EventQueue& queue, std::string& error) override;
        void Stop() override;

        std::shared_ptr<Endpoint> Connect();

        struct Shared {
            std::mutex mutex;
            std::deque<std::vector<uint8_t>> inbox;
            std::atomic<bool> alive{true};
        };

        class Endpoint {
        public:
            ~Endpoint() { Close(); }

            bool Send(const uint8_t* data, size_t size);

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
