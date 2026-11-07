#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace perlin::server {

    // game packet batches: [compressor byte once settings are agreed] then (varint length, varint header, body)*
    // nethernet has no outer 0xFE byte and no encryption (the data channel is already dtls)
    class BatchCodec {
    public:
        // after NetworkSettings both directions carry the compressor byte
        void EnableCompression(uint16_t threshold) {
            compression = true;
            this->threshold = threshold;
        }
        bool Compressing() const { return compression; }

        static void AppendPacket(std::vector<uint8_t>& content, uint32_t id, std::span<const uint8_t> body);
        std::vector<uint8_t> Seal(const std::vector<uint8_t>& content) const;
        // false on a corrupt or oversized batch
        bool Open(std::span<const uint8_t> wire, std::vector<uint8_t>& content) const;

        using PacketFn = std::function<void(uint32_t id, std::span<const uint8_t> body)>;
        static bool Split(std::span<const uint8_t> content, const PacketFn& fn);

    private:
        bool compression = false;
        uint16_t threshold = 256;
    };
}
