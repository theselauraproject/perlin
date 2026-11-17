#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::util::ws {
    struct Url {
        std::string scheme, host, path;
        int port = 0;
    };

    bool ParseUrl(const std::string& url, Url& out);
    std::array<uint8_t, 20> Sha1(const std::string& text);
    std::string Base64(const uint8_t* p, size_t n);
    std::string TrimHeaderValue(const std::string& s);

    std::vector<uint8_t> MakeFrame(uint8_t opcode, const uint8_t* p, size_t n, uint32_t maskKey);

    std::string BuildHandshakeRequest(const Url& url, const std::string& key, const std::string& protocols,
                                      const std::vector<std::pair<std::string, std::string>>& headers);

    enum class HandshakeStatus { Accepted, Rejected, BadAccept };
    struct HandshakeResult {
        HandshakeStatus status = HandshakeStatus::BadAccept;
        std::string statusLine;
        std::string subprotocol;
    };
    HandshakeResult ParseHandshakeResponse(const std::string& head, const std::string& key);

    class FrameDecoder {
    public:
        struct Event {
            enum class Kind { NeedMore, Message, Ping, PeerClose, TooLarge, ProtocolError };
            Kind kind = Kind::NeedMore;
            bool binary = false;
            std::vector<uint8_t> data;
            uint32_t code = 0;
            std::string reason;
        };

        explicit FrameDecoder(size_t maxMessage) : maxMessage(maxMessage) {}
        void Feed(const uint8_t* bytes, size_t count);
        Event Next();

    private:
        size_t maxMessage;
        std::vector<uint8_t> buffer;
        std::vector<uint8_t> message;
        bool messageBinary = false, inMessage = false;
    };
}
