#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    class ClientIdentity;

    class PacketCipher {
    public:
        PacketCipher();
        ~PacketCipher();
        PacketCipher(const PacketCipher&) = delete;
        PacketCipher& operator=(const PacketCipher&) = delete;

        bool InitFromHandshake(const ClientIdentity& identity, const std::string& token, std::string& error);
        bool InitWithKey(const uint8_t key[32]);

        bool Active() const { return active; }

        std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plain);
        bool Decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& plain);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool active = false;
    };
}
