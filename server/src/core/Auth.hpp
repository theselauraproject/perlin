#pragma once
#include "core/Json.hpp"

#include <string>
#include <vector>

namespace perlin::server::auth {

    const std::vector<std::string>& DefaultRootKeys();

    struct Login {
        bool chainValid = false;
        bool xboxVerified = false;
        std::string name, xuid, uuid;
        std::string clientKey;
        json::Value clientData;
        bool clientDataValid = false;
        std::string error;
    };

    Login VerifyLogin(const std::string& identity, const std::string& clientToken,
                      const std::vector<std::string>& rootKeys);

    void SetTokenKeys(const std::string& issuer, const json::Value& jwks);
    bool TokenKeysReady();
    void StartTokenKeyRefresh();

    bool VerifyOfferBinding(const std::string& offerSdp, const std::string& clientKey, std::string& error);

    bool VerifyEs384(const std::string& signingInput, const std::string& rawSignature,
                     const std::string& publicKeyB64);
}
