#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace perlin::util {
    struct SocketEvent {
        enum class Kind { Open, Data, Close };
        Kind kind = Kind::Open;
        uint32_t id = 0;
        bool binary = true;
        std::vector<uint8_t> data;
        std::string text;
        std::string error;
        uint32_t code = 0;
    };

    struct SocketOpenOptions {
        uint32_t id = 0;
        bool webSocket = false;
        std::string url;
        int timeoutMs = 10000;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string protocols;
    };

    enum class SocketResult { Ok, BadArguments, Unavailable, NotFound };

    class SocketSet {
    public:
        static constexpr size_t kMaxOpen = 8;
        static constexpr size_t kMaxMessage = 4u << 20;
        static constexpr size_t kMaxQueuedBytes = 16u << 20;

        SocketSet();
        ~SocketSet();
        SocketSet(const SocketSet&) = delete;
        SocketSet& operator=(const SocketSet&) = delete;

        SocketResult Open(SocketOpenOptions options);
        SocketResult Send(uint32_t id, bool binary, std::vector<uint8_t> data);
        SocketResult Close(uint32_t id, uint32_t code, std::string reason);
        void CloseAll();
        void Poll(std::vector<SocketEvent>& out);

        static bool TlsAvailable();

    private:
        struct Socket;
        void Reap();
        void Push(SocketEvent&& event);
        void Run(std::shared_ptr<Socket> socket);

        std::mutex mutex;
        std::vector<std::shared_ptr<Socket>> sockets;
        std::mutex eventMutex;
        std::deque<SocketEvent> events;
    };
}
