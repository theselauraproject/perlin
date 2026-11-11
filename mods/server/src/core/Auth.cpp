#include "core/Auth.hpp"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <openssl/core_names.h>
#include <openssl/md5.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace perlin::server::auth {
    namespace {
        struct PkeyFree {
            void operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); }
        };
        using Pkey = std::unique_ptr<EVP_PKEY, PkeyFree>;

        Pkey LoadKey(const std::string& b64) {
            const std::string der = json::Base64UrlDecode(b64);
            const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
            return Pkey(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size())));
        }

        std::string B64UrlEncode(const std::string& s) {
            static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            std::string out;
            for (size_t i = 0; i < s.size(); i += 3) {
                uint32_t v = static_cast<unsigned char>(s[i]) << 16;
                if (i + 1 < s.size())
                    v |= static_cast<unsigned char>(s[i + 1]) << 8;
                if (i + 2 < s.size())
                    v |= static_cast<unsigned char>(s[i + 2]);
                out += t[(v >> 18) & 63];
                out += t[(v >> 12) & 63];
                if (i + 1 < s.size())
                    out += t[(v >> 6) & 63];
                if (i + 2 < s.size())
                    out += t[v & 63];
            }
            return out;
        }

        int64_t Now() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        struct Jwt {
            std::string signingInput, signature, x5u;
            json::Value payload;
        };

        bool Split(const std::string& token, Jwt& out) {
            const size_t a = token.find('.');
            const size_t b = a == std::string::npos ? a : token.find('.', a + 1);
            if (b == std::string::npos)
                return false;
            json::Value header;
            if (!json::Parse(json::Base64UrlDecode(token.substr(0, a)), header))
                return false;
            if (header["alg"].String() != "ES384")
                return false;
            out.x5u = header["x5u"].String();
            if (!json::Parse(json::Base64UrlDecode(token.substr(a + 1, b - a - 1)), out.payload))
                return false;
            out.signingInput = token.substr(0, b);
            out.signature = json::Base64UrlDecode(token.substr(b + 1));
            return !out.x5u.empty();
        }

        bool InTime(const json::Value& payload) {
            const int64_t now = Now();
            constexpr int64_t kSlack = 300;
            if (payload["nbf"].kind == json::Value::Kind::Number && now + kSlack < payload["nbf"].Int())
                return false;
            if (payload["exp"].kind == json::Value::Kind::Number && now - kSlack > payload["exp"].Int())
                return false;
            return true;
        }

        struct TokenKey {
            std::string kty, alg;
            Pkey key;
        };

        struct TokenStore {
            std::mutex mutex;
            std::string issuer;
            std::map<std::string, std::shared_ptr<TokenKey>> keys;
        };

        TokenStore& Tokens() {
            static TokenStore store;
            return store;
        }

        Pkey RsaKey(const std::string& nB64, const std::string& eB64) {
            const std::string n = json::Base64UrlDecode(nB64), e = json::Base64UrlDecode(eB64);
            if (n.empty() || e.empty())
                return nullptr;
            BIGNUM* bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(n.data()), static_cast<int>(n.size()), nullptr);
            BIGNUM* be = BN_bin2bn(reinterpret_cast<const unsigned char*>(e.data()), static_cast<int>(e.size()), nullptr);
            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            EVP_PKEY* out = nullptr;
            if (bn && be && bld && OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, bn) == 1 &&
                OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, be) == 1) {
                OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
                EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
                if (params && ctx && EVP_PKEY_fromdata_init(ctx) == 1)
                    EVP_PKEY_fromdata(ctx, &out, EVP_PKEY_PUBLIC_KEY, params);
                EVP_PKEY_CTX_free(ctx);
                OSSL_PARAM_free(params);
            }
            OSSL_PARAM_BLD_free(bld);
            BN_free(bn);
            BN_free(be);
            return Pkey(out);
        }

        Pkey EcKey(const std::string& xB64, const std::string& yB64) {
            const std::string x = json::Base64UrlDecode(xB64), y = json::Base64UrlDecode(yB64);
            if (x.size() != 48 || y.size() != 48)
                return nullptr;
            std::string point;
            point.push_back(4);
            point += x;
            point += y;
            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            EVP_PKEY* out = nullptr;
            if (bld && OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "secp384r1", 0) == 1 &&
                OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()) == 1) {
                OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
                EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
                if (params && ctx && EVP_PKEY_fromdata_init(ctx) == 1)
                    EVP_PKEY_fromdata(ctx, &out, EVP_PKEY_PUBLIC_KEY, params);
                EVP_PKEY_CTX_free(ctx);
                OSSL_PARAM_free(params);
            }
            OSSL_PARAM_BLD_free(bld);
            return Pkey(out);
        }

        bool VerifyWith(EVP_PKEY* key, const std::string& alg, const std::string& input, const std::string& raw) {
            if (!key)
                return false;
            std::string der;
            if (alg == "ES384") {
                if (raw.size() != 96)
                    return false;
                ECDSA_SIG* sig = ECDSA_SIG_new();
                BIGNUM* r = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 48, nullptr);
                BIGNUM* sv = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 48, 48, nullptr);
                if (!sig || !r || !sv || ECDSA_SIG_set0(sig, r, sv) != 1) {
                    ECDSA_SIG_free(sig);
                    BN_free(r);
                    BN_free(sv);
                    return false;
                }
                unsigned char* d = nullptr;
                const int n = i2d_ECDSA_SIG(sig, &d);
                ECDSA_SIG_free(sig);
                if (n <= 0)
                    return false;
                der.assign(reinterpret_cast<const char*>(d), static_cast<size_t>(n));
                OPENSSL_free(d);
            } else if (alg == "RS256") {
                der = raw;
            } else {
                return false;
            }
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            const EVP_MD* md = alg == "ES384" ? EVP_sha384() : EVP_sha256();
            const bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, key) == 1 &&
                            EVP_DigestVerifyUpdate(ctx, input.data(), input.size()) == 1 &&
                            EVP_DigestVerifyFinal(ctx, reinterpret_cast<const unsigned char*>(der.data()), der.size()) == 1;
            EVP_MD_CTX_free(ctx);
            return ok;
        }

        std::string UuidFromXuid(const std::string& xuid) {
            const std::string name = "pocket-auth-1-xuid:" + xuid;
            unsigned char h[16];
            unsigned int len = 0;
            EVP_Digest(name.data(), name.size(), h, &len, EVP_md5(), nullptr);
            h[6] = static_cast<unsigned char>((h[6] & 0x0f) | 0x30);
            h[8] = static_cast<unsigned char>((h[8] & 0x3f) | 0x80);
            char out[37];
            std::snprintf(out, sizeof out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", h[0], h[1],
                          h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11], h[12], h[13], h[14], h[15]);
            return out;
        }

        struct TokenLogin {
            bool parsed = false, verified = false;
            std::string cpk, xuid, name, uuid, error;
        };

        TokenLogin ParseToken(const std::string& token) {
            TokenLogin out;
            const size_t a = token.find('.');
            const size_t b = a == std::string::npos ? a : token.find('.', a + 1);
            if (b == std::string::npos) {
                out.error = "malformed multiplayer token";
                return out;
            }
            json::Value header, payload;
            if (!json::Parse(json::Base64UrlDecode(token.substr(0, a)), header) ||
                !json::Parse(json::Base64UrlDecode(token.substr(a + 1, b - a - 1)), payload)) {
                out.error = "malformed multiplayer token";
                return out;
            }
            out.cpk = payload["cpk"].String();
            if (out.cpk.empty()) {
                out.error = "the multiplayer token names no client key";
                return out;
            }
            out.parsed = true;
            out.xuid = payload["xid"].String();
            out.name = payload["xname"].String();
            out.uuid = payload["leguuid"].String();
            if (out.uuid.empty() && !out.xuid.empty())
                out.uuid = UuidFromXuid(out.xuid);
            const std::string alg = header["alg"].String(), kid = header["kid"].String();
            const std::string input = token.substr(0, b), signature = json::Base64UrlDecode(token.substr(b + 1));
            std::shared_ptr<TokenKey> key;
            std::string issuer;
            {
                TokenStore& store = Tokens();
                std::lock_guard lock(store.mutex);
                issuer = store.issuer;
                if (const auto it = store.keys.find(kid); it != store.keys.end())
                    key = it->second;
                else if (kid.empty() && store.keys.size() == 1)
                    key = store.keys.begin()->second;
            }
            bool audience = false;
            const json::Value& aud = payload["aud"];
            if (aud.IsString())
                audience = aud.text == "api://auth-minecraft-services/multiplayer";
            for (const json::Value& v : aud.items)
                audience |= v.String() == "api://auth-minecraft-services/multiplayer";
            std::string iss = payload["iss"].String();
            while (!iss.empty() && iss.back() == '/')
                iss.pop_back();
            std::string want = issuer;
            while (!want.empty() && want.back() == '/')
                want.pop_back();
            out.verified = key && VerifyWith(key->key.get(), alg, input, signature) && InTime(payload) && audience &&
                           !want.empty() && iss == want && !out.xuid.empty();
            if (!out.verified)
                out.error = key ? "the multiplayer token is not signed by the authorization service" : "the authorization service's keys are not loaded";
            return out;
        }
    }

    const std::vector<std::string>& DefaultRootKeys() {
        static const std::vector<std::string> keys = {
            "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAECRXueJeTDqNRRgJi/vlRufByu/2G0i2Ebt6YMar5QX/"
            "R0DIIyrJMcUpruK4QveTfJSTp3Shlq4Gk34cD/4GUWwkv0DVuzeuB+tXija7HBxii03NHDbPAD0AKnLr2wdAp",
            "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE8ELkixyLcwlZryUQcu1TvPOmI2B7vX83ndnWRUaXm74wFfa5f/"
            "lwQNTfrLVHa2PmenpGI6JhIMUJaWZrjmMj90NoKNFSNBuKdm8rYiXsfaz3K36x/1U26HpG0ZxK/V1V"};
        return keys;
    }

    bool VerifyEs384(const std::string& signingInput, const std::string& raw,
                     const std::string& publicKeyB64) {
        if (raw.size() != 96)
            return false;
        const Pkey key = LoadKey(publicKeyB64);
        if (!key)
            return false;
        ECDSA_SIG* sig = ECDSA_SIG_new();
        BIGNUM* r = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()), 48, nullptr);
        BIGNUM* s = BN_bin2bn(reinterpret_cast<const unsigned char*>(raw.data()) + 48, 48, nullptr);
        if (!sig || !r || !s || ECDSA_SIG_set0(sig, r, s) != 1) {
            ECDSA_SIG_free(sig);
            BN_free(r);
            BN_free(s);
            return false;
        }
        unsigned char* der = nullptr;
        const int derLen = i2d_ECDSA_SIG(sig, &der);
        ECDSA_SIG_free(sig);
        if (derLen <= 0)
            return false;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha384(), nullptr, key.get()) == 1 &&
                  EVP_DigestVerifyUpdate(ctx, signingInput.data(), signingInput.size()) == 1 &&
                  EVP_DigestVerifyFinal(ctx, der, static_cast<size_t>(derLen)) == 1;
        EVP_MD_CTX_free(ctx);
        OPENSSL_free(der);
        return ok;
    }

    Login VerifyLogin(const std::string& identityJson, const std::string& clientToken,
                      const std::vector<std::string>& rootKeys) {
        Login out;
        json::Value identity;
        if (!json::Parse(identityJson, identity)) {
            out.error = "the identity is not json";
            return out;
        }
        std::vector<std::string> chain;
        const json::Value* source = &identity;
        json::Value inner;
        if (identity["Certificate"].IsString()) {
            if (!json::Parse(identity["Certificate"].text, inner)) {
                out.error = "the certificate is not json";
                return out;
            }
            source = &inner;
        }
        for (const json::Value& t : (*source)["chain"].items)
            if (!t.String().empty())
                chain.push_back(t.String());
        const std::string tokenText = identity["Token"].String();
        if (!tokenText.empty()) {
            const TokenLogin token = ParseToken(tokenText);
            if (!token.parsed) {
                out.error = token.error;
                return out;
            }
            out.clientKey = token.cpk;
            out.name = token.name;
            out.uuid = token.uuid;
            if (token.verified) {
                out.xboxVerified = true;
                out.xuid = token.xuid;
            }
            out.chainValid = true;
            Jwt client;
            if (Split(clientToken, client) && client.x5u == out.clientKey &&
                VerifyEs384(client.signingInput, client.signature, out.clientKey) && InTime(client.payload)) {
                out.clientData = client.payload;
                out.clientDataValid = true;
            } else {
                out.chainValid = false;
                out.xboxVerified = false;
                out.xuid.clear();
                out.error = "the client data is not signed by the key in the multiplayer token";
                return out;
            }
            if (!token.verified && !token.error.empty())
                out.error = token.error;
            return out;
        }
        if (chain.empty() || chain.size() > 8) {
            out.error = "no certificate chain";
            return out;
        }

        struct Identity {
            std::string name, xuid, uuid;
            bool found = false;
        };
        Identity claimed, verified;
        std::string expectedKey;
        bool rooted = false;
        for (size_t i = 0; i < chain.size(); i++) {
            Jwt jwt;
            if (!Split(chain[i], jwt)) {
                out.error = "malformed certificate link " + std::to_string(i);
                return out;
            }
            if (i > 0 && jwt.x5u != expectedKey) {
                out.error = "certificate link " + std::to_string(i) +
                            " is not signed by the key the previous link named";
                return out;
            }
            if (!VerifyEs384(jwt.signingInput, jwt.signature, jwt.x5u) || !InTime(jwt.payload)) {
                out.error =
                    "certificate link " + std::to_string(i) + " has a bad signature or is out of date";
                return out;
            }
            for (const std::string& root : rootKeys)
                if (jwt.x5u == root)
                    rooted = true;
            const json::Value& extra = jwt.payload["extraData"];
            if (extra.IsObject()) {
                Identity& dst = rooted ? verified : claimed;
                dst.name = extra["displayName"].String();
                dst.xuid = extra["XUID"].String();
                dst.uuid = extra["identity"].String();
                dst.found = true;
            }
            expectedKey = jwt.payload["identityPublicKey"].String();
            if (expectedKey.empty()) {
                out.error = "certificate link " + std::to_string(i) + " names no key";
                return out;
            }
        }
        out.chainValid = true;
        out.clientKey = expectedKey;
        if (rooted && verified.found && !verified.xuid.empty()) {
            out.xboxVerified = true;
            out.name = verified.name;
            out.xuid = verified.xuid;
            out.uuid = verified.uuid;
        } else {
            out.name = claimed.name.empty() ? verified.name : claimed.name;
            out.uuid = claimed.uuid;
        }

        Jwt client;
        if (Split(clientToken, client) && client.x5u == out.clientKey &&
            VerifyEs384(client.signingInput, client.signature, out.clientKey) && InTime(client.payload)) {
            out.clientData = client.payload;
            out.clientDataValid = true;
        } else {
            out.chainValid = false;
            out.xboxVerified = false;
            out.xuid.clear();
            out.error = "the client data is not signed by the key in the certificate";
        }
        return out;
    }

    bool VerifyOfferBinding(const std::string& offerSdp, const std::string& clientKey, std::string& error) {
        std::string payload = "{\"fingerprint\":[";
        bool first = true;
        std::istringstream in(offerSdp);
        std::string line;
        std::string assertionAttr;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.starts_with("a=fingerprint:")) {
                const std::string rest = line.substr(14);
                const size_t sp = rest.find(' ');
                if (sp == std::string::npos)
                    continue;
                payload += std::string(first ? "" : ",") + "{\"algorithm\":\"" +
                           json::Escape(rest.substr(0, sp)) + "\",\"digest\":\"" +
                           json::Escape(rest.substr(sp + 1)) + "\"}";
                first = false;
            } else if (line.starts_with("a=identity:") && assertionAttr.empty()) {
                assertionAttr = line.substr(11);
            }
        }
        payload += "]}";
        if (first) {
            error = "the offer has no dtls fingerprint";
            return false;
        }
        if (assertionAttr.empty()) {
            error = "the offer carries no identity assertion";
            return false;
        }
        json::Value envelope, assertion;
        if (!json::Parse(json::Base64UrlDecode(assertionAttr), envelope) ||
            !json::Parse(envelope["assertion"].String(), assertion)) {
            error = "the identity assertion is malformed";
            return false;
        }
        const std::string fps = assertion["fingerprints"].String();
        const size_t gap = fps.find("..");
        if (gap == std::string::npos) {
            error = "the fingerprint signature is malformed";
            return false;
        }
        const std::string signingInput = fps.substr(0, gap) + "." + B64UrlEncode(payload);
        if (!VerifyEs384(signingInput, json::Base64UrlDecode(fps.substr(gap + 2)), clientKey)) {
            error = "the offer's fingerprints are not signed by the player's key";
            return false;
        }
        return true;
    }
}

#ifdef PERLIN_SERVER_HAS_SSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace perlin::server::auth {
    void SetTokenKeys(const std::string& issuer, const json::Value& jwks) {
        std::map<std::string, std::shared_ptr<TokenKey>> keys;
        for (const json::Value& k : jwks["keys"].items) {
            auto key = std::make_shared<TokenKey>();
            key->kty = k["kty"].String();
            key->alg = k["alg"].String();
            if (key->kty == "RSA")
                key->key = RsaKey(k["n"].String(), k["e"].String());
            else if (key->kty == "EC")
                key->key = EcKey(k["x"].String(), k["y"].String());
            if (key->key)
                keys[k["kid"].String()] = std::move(key);
        }
        TokenStore& store = Tokens();
        std::lock_guard lock(store.mutex);
        store.issuer = issuer;
        if (!keys.empty())
            store.keys = std::move(keys);
    }

    bool TokenKeysReady() {
        TokenStore& store = Tokens();
        std::lock_guard lock(store.mutex);
        return !store.keys.empty();
    }

#ifdef PERLIN_SERVER_HAS_SSL
    namespace {
        bool SplitUrl(const std::string& url, std::string& host, std::string& path) {
            if (!url.starts_with("https://"))
                return false;
            const size_t slash = url.find('/', 8);
            host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
            path = slash == std::string::npos ? "/" : url.substr(slash);
            return !host.empty();
        }

        bool HttpsGet(const std::string& url, std::string& body) {
            std::string host, path;
            if (!SplitUrl(url, host, path))
                return false;
            SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
            if (!ctx)
                return false;
            SSL_CTX_set_default_verify_paths(ctx);
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
            BIO* bio = BIO_new_ssl_connect(ctx);
            bool ok = false;
            if (bio) {
                SSL* ssl = nullptr;
                BIO_get_ssl(bio, &ssl);
                const std::string target = host + ":443";
                BIO_set_conn_hostname(bio, target.c_str());
                if (ssl) {
                    SSL_set_tlsext_host_name(ssl, host.c_str());
                    SSL_set1_host(ssl, host.c_str());
                }
                if (BIO_do_connect(bio) == 1 && BIO_do_handshake(bio) == 1) {
                    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                                                "\r\nAccept: application/json\r\nUser-Agent: PerlinServer\r\nConnection: close\r\n\r\n";
                    if (BIO_write(bio, request.data(), static_cast<int>(request.size())) == static_cast<int>(request.size())) {
                        std::string response;
                        char buf[4096];
                        for (;;) {
                            const int n = BIO_read(bio, buf, sizeof buf);
                            if (n <= 0)
                                break;
                            response.append(buf, static_cast<size_t>(n));
                            if (response.size() > (4u << 20))
                                break;
                        }
                        const size_t split = response.find("\r\n\r\n");
                        if (split != std::string::npos && response.starts_with("HTTP/1.1 200")) {
                            const std::string head = response.substr(0, split);
                            body = response.substr(split + 4);
                            std::string lower = head;
                            for (char& c : lower)
                                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            if (lower.find("transfer-encoding: chunked") != std::string::npos) {
                                std::string joined;
                                size_t at = 0;
                                while (at < body.size()) {
                                    const size_t eol = body.find("\r\n", at);
                                    if (eol == std::string::npos)
                                        break;
                                    const size_t size = std::strtoul(body.substr(at, eol - at).c_str(), nullptr, 16);
                                    if (size == 0)
                                        break;
                                    joined += body.substr(eol + 2, size);
                                    at = eol + 2 + size + 2;
                                }
                                body = std::move(joined);
                            }
                            ok = true;
                        }
                    }
                }
                BIO_free_all(bio);
            }
            SSL_CTX_free(ctx);
            return ok;
        }

        bool RefreshTokenKeys() {
            std::string body;
            if (!HttpsGet("https://client.discovery.minecraft-services.net/api/v1.0/discovery/MinecraftPE/builds/1.26.60",
                          body))
                return false;
            json::Value discovery;
            if (!json::Parse(body, discovery))
                return false;
            std::string issuer = discovery["result"]["serviceEnvironments"]["auth"]["prod"]["issuer"].String();
            if (issuer.empty())
                return false;
            while (!issuer.empty() && issuer.back() == '/')
                issuer.pop_back();
            json::Value config;
            if (!HttpsGet(issuer + "/.well-known/openid-configuration", body) || !json::Parse(body, config))
                return false;
            const std::string jwksUri = config["jwks_uri"].String();
            json::Value jwks;
            if (jwksUri.empty() || !HttpsGet(jwksUri, body) || !json::Parse(body, jwks))
                return false;
            const std::string configIssuer = config["issuer"].String();
            SetTokenKeys(configIssuer.empty() ? issuer : configIssuer, jwks);
            return TokenKeysReady();
        }
    }

    void StartTokenKeyRefresh() {
        static std::atomic<bool> started{false};
        if (started.exchange(true))
            return;
        std::thread([] {
            int failures = 0;
            for (;;) {
                const bool ok = RefreshTokenKeys();
                failures = ok ? 0 : failures + 1;
                std::this_thread::sleep_for(ok ? std::chrono::minutes(30) : std::chrono::seconds(std::min(300, 10 << std::min(failures, 5))));
            }
        }).detach();
    }
#else
    void StartTokenKeyRefresh() {}
#endif
}
