#include "network/LoginBuilder.hpp"

#include "network/ClientIdentity.hpp"
#include "network/LoginClaims.hpp"
#include "util/Json.hpp"

#include <ctime>
#include <vector>

namespace perlin {
    namespace {
        template <class Header, class Payload>
        std::string SignJwt(const ClientIdentity& identity, const Header& header, const Payload& payload) {
            const std::string h = B64Url(json::Write(header));
            const std::string p = B64Url(json::Write(payload));
            std::vector<uint8_t> raw;
            if (!identity.Sign(h + "." + p, raw))
                return {};
            return h + "." + p + "." + B64Url(raw.data(), raw.size());
        }
    }

    std::string BuildIdentityJson(ClientIdentity& identity, const std::string* onlineChainJson,
                                  std::string& error, const std::string& displayName) {
        const std::string& mpToken = identity.MultiplayerToken();
        const std::string cpk = identity.PublicKeyB64();
        if (!identity.Valid() || mpToken.empty() || cpk.empty()) {
            error = "no client identity";
            return {};
        }
        if (!onlineChainJson) {
            claims::Chain self;
            self.chain.clear();
            if (!displayName.empty()) {
                const int64_t now = static_cast<int64_t>(std::time(nullptr));
                claims::SelfSignedLink link;
                link.exp = now + 6 * 3600;
                link.nbf = now - 6 * 3600;
                link.identityPublicKey = cpk;
                link.extraData.displayName = displayName;
                link.extraData.identity = RandomUuid();
                const std::string jwt = SignJwt(identity, claims::Header{.x5u = cpk}, link);
                if (jwt.empty()) {
                    error = "could not sign the offline identity";
                    return {};
                }
                self.chain.push_back(jwt);
            }
            return json::Write(claims::Identity{json::Write(self), 2, mpToken});
        }

        claims::Chain chain;
        chain.chain.clear();
        std::vector<std::string> issued;
        if (auto doc = json::Document::Parse(*onlineChainJson))
            for (const json::View link : doc->root()["chain"].items())
                issued.push_back(link.str());
        if (!issued.empty()) {
            std::string x5u;
            const size_t dot = issued.front().find('.');
            if (dot != std::string::npos)
                if (auto header = json::Document::Parse(B64UrlDecode(issued.front().substr(0, dot))))
                    x5u = header->root()["x5u"].str();
            const int64_t now = static_cast<int64_t>(std::time(nullptr));
            claims::ChainLink link;
            link.exp = now + 6 * 3600;
            link.nbf = now - 6 * 3600;
            link.identityPublicKey = x5u;
            const std::string first = SignJwt(identity, claims::Header{.x5u = cpk}, link);
            if (first.empty() || x5u.empty()) {
                error = "the certificate chain from the Minecraft service is malformed";
                return {};
            }
            chain.chain.push_back(first);
            for (std::string& jwt : issued)
                chain.chain.push_back(std::move(jwt));
        }
        return json::Write(claims::Identity{json::Write(chain), 0, mpToken});
    }
}
