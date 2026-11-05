#include "net/LoopbackListener.hpp"

namespace perlin::server {

    namespace {
        class Peer final : public Connection {
        public:
            explicit Peer(std::shared_ptr<LoopbackListener::Shared> shared) : shared(std::move(shared)) {}
            bool Send(const uint8_t* data, size_t size, bool) override {
                if (!shared->alive)
                    return false;
                std::lock_guard lock(shared->mutex);
                shared->inbox.emplace_back(data, data + size);
                return true;
            }
            void Close() override { shared->alive = false; }
            bool Alive() const override { return shared->alive; }
            std::string Describe() const override { return "loopback"; }
            bool Local() const override { return true; }

        private:
            std::shared_ptr<LoopbackListener::Shared> shared;
        };
    }

    bool LoopbackListener::Start(EventQueue& queue, std::string&) {
        events = &queue;
        return true;
    }

    void LoopbackListener::Stop() {
        std::lock_guard lock(mutex);
        for (auto& weak : endpoints)
            if (auto e = weak.lock())
                e->Close();
        endpoints.clear();
        events = nullptr;
    }

    std::shared_ptr<LoopbackListener::Endpoint> LoopbackListener::Connect() {
        std::lock_guard lock(mutex);
        if (!events)
            return nullptr;
        auto endpoint = std::make_shared<Endpoint>();
        endpoint->queue = events;
        endpoint->peer = std::make_shared<Peer>(endpoint->shared);
        endpoint->peer->id = nextId++;
        endpoints.push_back(endpoint);
        NetEvent open;
        open.type = NetEvent::Type::Open;
        open.connection = endpoint->peer;
        events->Push(std::move(open));
        return endpoint;
    }

    bool LoopbackListener::Endpoint::Send(const uint8_t* data, size_t size) {
        if (!shared->alive || !queue)
            return false;
        NetEvent message;
        message.type = NetEvent::Type::Message;
        message.connection = peer;
        message.data.assign(data, data + size);
        queue->Push(std::move(message));
        return true;
    }

    bool LoopbackListener::Endpoint::Receive(std::vector<uint8_t>& out) {
        std::lock_guard lock(shared->mutex);
        if (shared->inbox.empty())
            return false;
        out = std::move(shared->inbox.front());
        shared->inbox.pop_front();
        return true;
    }

    void LoopbackListener::Endpoint::Close() {
        if (!shared->alive.exchange(false))
            return;
        if (queue) {
            NetEvent close;
            close.type = NetEvent::Type::Close;
            close.connection = peer;
            queue->Push(std::move(close));
        }
    }
}
