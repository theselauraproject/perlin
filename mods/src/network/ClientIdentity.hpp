#pragma once
#include <cstdint>
#include <string>
#include <vector>

using EC_KEY = struct ec_key_st;

namespace perlin {

    std::string B64Standard(const uint8_t* data, size_t len);
    std::string B64Url(const uint8_t* data, size_t len);
    std::string B64UrlDecode(const std::string& s);
    inline std::string B64Standard(const std::string& s) {
        return B64Standard(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    inline std::string B64Url(const std::string& s) {
        return B64Url(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    class ClientIdentity {
    public:
        ClientIdentity();
        // shares an existing key pair (a cached login is bound to it)
        explicit ClientIdentity(EC_KEY* shared);
        ~ClientIdentity();

        ClientIdentity(const ClientIdentity&) = delete;
        ClientIdentity& operator=(const ClientIdentity&) = delete;

        bool Valid() const { return key != nullptr; }

        std::string PublicKeyB64() const;

        std::string BuildMultiplayerToken(const std::string& playerName, const std::string& uuid);
        const std::string& MultiplayerToken() const { return mpToken; }
        void SetMultiplayerToken(std::string token) { mpToken = std::move(token); }

        std::string AssertOffer(const std::string& sdp, const std::string& identityProvider = {}) const;

        bool Sign(const std::string& signingInput, std::vector<uint8_t>& rawSig) const;
        EC_KEY* Key() const { return key; }

    private:
        EC_KEY* key = nullptr;
        std::string mpToken;
    };

    std::string RandomUuid();

}
