#pragma once
// test-side login forging: real P-384 keys and ES384 signatures, the same shapes the client sends
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace testauth {
    inline std::string B64Url(const std::string& s) {
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

    inline std::string B64Std(const std::string& s) {
        std::string u = B64Url(s);
        for (char& c : u)
            c = c == '-' ? '+' : c == '_' ? '/' : c;
        while (u.size() % 4)
            u += '=';
        return u;
    }

    struct Key {
        EVP_PKEY* pkey = nullptr;
        std::string spki; // base64 der, what x5u and identityPublicKey carry

        Key() {
            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
            EVP_PKEY_keygen_init(ctx);
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp384r1);
            EVP_PKEY_keygen(ctx, &pkey);
            EVP_PKEY_CTX_free(ctx);
            unsigned char* der = nullptr;
            const int n = i2d_PUBKEY(pkey, &der);
            spki = B64Std(std::string(reinterpret_cast<char*>(der), static_cast<size_t>(n)));
            OPENSSL_free(der);
        }
        Key(const Key&) = delete;
        ~Key() { EVP_PKEY_free(pkey); }

        // raw r||s as jws wants it
        std::string Sign(const std::string& input) const {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            EVP_DigestSignInit(ctx, nullptr, EVP_sha384(), nullptr, pkey);
            EVP_DigestSignUpdate(ctx, input.data(), input.size());
            size_t len = 0;
            EVP_DigestSignFinal(ctx, nullptr, &len);
            std::string der(len, '\0');
            EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(der.data()), &len);
            EVP_MD_CTX_free(ctx);
            const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
            ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(len));
            const BIGNUM *r, *s;
            ECDSA_SIG_get0(sig, &r, &s);
            std::string raw(96, '\0');
            BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), 48);
            BN_bn2binpad(s, reinterpret_cast<unsigned char*>(raw.data()) + 48, 48);
            ECDSA_SIG_free(sig);
            return raw;
        }
    };

    inline int64_t Now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // header {alg, x5u: signer}, payload text as given
    inline std::string Jwt(const Key& signer, const std::string& payloadJson) {
        const std::string h = B64Url("{\"alg\":\"ES384\",\"x5u\":\"" + signer.spki + "\"}");
        const std::string p = B64Url(payloadJson);
        return h + "." + p + "." + B64Url(signer.Sign(h + "." + p));
    }

    inline std::string Link(const Key& signer, const Key& subject, const std::string& extraData = "") {
        const int64_t now = Now();
        std::string payload = "{\"identityPublicKey\":\"" + subject.spki +
                              "\",\"nbf\":" + std::to_string(now - 60) +
                              ",\"exp\":" + std::to_string(now + 3600);
        if (!extraData.empty())
            payload += ",\"extraData\":" + extraData;
        return Jwt(signer, payload + "}");
    }

    // what the client sends online: its own link to the service's intermediate, then the service's two links
    struct Account {
        Key root, intermediate, player;
        std::string name, xuid;
        Account(std::string n, std::string x) : name(std::move(n)), xuid(std::move(x)) {}

        std::vector<std::string> Chain(bool withService = true) const {
            const std::string extra = "{\"displayName\":\"" + name + "\",\"XUID\":\"" + xuid +
                                      "\",\"identity\":\"11111111-2222-3333-4444-555555555555\"}";
            if (!withService)
                return {Link(player, player,
                             "{\"displayName\":\"" + name +
                                 "\",\"identity\":\"11111111-2222-3333-4444-555555555555\"}")};
            return {Link(player, root), Link(root, intermediate), Link(intermediate, player, extra)};
        }
        std::string Identity(const std::vector<std::string>& chain, int type = 0) const {
            std::string list;
            for (const std::string& c : chain)
                list += (list.empty() ? "\\\"" : ",\\\"") + c + "\\\"";
            return "{\"Certificate\":\"{\\\"chain\\\":[" + list +
                   "]}\",\"AuthenticationType\":" + std::to_string(type) + ",\"Token\":\"\"}";
        }
        std::string ClientToken() const {
            return Jwt(player, "{\"ThirdPartyName\":\"" + name + "\",\"SkinId\":\"s\"}");
        }
    };

    // the a=identity line the client adds to its offer: fingerprints signed with the player's key
    inline std::string AssertOffer(const std::string& sdp, const Key& player) {
        std::string payload = "{\"fingerprint\":[";
        bool first = true;
        size_t pos = 0;
        const std::string prefix = "a=fingerprint:";
        while ((pos = sdp.find(prefix, pos)) != std::string::npos) {
            size_t eol = sdp.find_first_of("\r\n", pos);
            const std::string rest = sdp.substr(pos + prefix.size(), eol - pos - prefix.size());
            const size_t sp = rest.find(' ');
            payload += std::string(first ? "" : ",") + "{\"algorithm\":\"" + rest.substr(0, sp) +
                       "\",\"digest\":\"" + rest.substr(sp + 1) + "\"}";
            first = false;
            pos = eol;
        }
        payload += "]}";
        const std::string h = B64Url("{\"alg\":\"ES384\"}");
        const std::string fps = h + ".." + B64Url(player.Sign(h + "." + B64Url(payload)));
        const std::string assertion = "{\"fingerprints\":\"" + fps + "\",\"token\":\"\"}";
        std::string escaped;
        for (char c : assertion)
            escaped += c == '"' ? std::string("\\\"") : std::string(1, c);
        const std::string outer =
            "{\"assertion\":\"" + escaped + "\",\"idp\":{\"domain\":\"\",\"protocol\":\"default\"}}";
        const size_t m = sdp.find("m=");
        const size_t bol = sdp.rfind('\n', m) + 1;
        return sdp.substr(0, bol) + "a=identity:" + B64Std(outer) + "\r\n" + sdp.substr(bol);
    }
}
