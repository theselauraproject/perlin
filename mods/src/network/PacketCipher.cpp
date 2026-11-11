#include "network/PacketCipher.hpp"

#include "network/ClientIdentity.hpp"
#include "util/Json.hpp"

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <cstring>

namespace perlin {
    struct PacketCipher::Impl {
        EVP_CIPHER_CTX* enc = nullptr;
        EVP_CIPHER_CTX* dec = nullptr;
        uint8_t key[32] = {};
        uint64_t sendCounter = 0;
        uint64_t recvCounter = 0;

        ~Impl() {
            if (enc)
                EVP_CIPHER_CTX_free(enc);
            if (dec)
                EVP_CIPHER_CTX_free(dec);
        }

        static void Checksum(uint64_t counter, const uint8_t* data, size_t n, const uint8_t* key,
                             uint8_t out[8]) {
            uint8_t c[8];
            for (int i = 0; i < 8; i++)
                c[i] = uint8_t(counter >> (8 * i));
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            unsigned char digest[32];
            unsigned int len = 0;
            EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
            EVP_DigestUpdate(ctx, c, 8);
            EVP_DigestUpdate(ctx, data, n);
            EVP_DigestUpdate(ctx, key, 32);
            EVP_DigestFinal_ex(ctx, digest, &len);
            EVP_MD_CTX_free(ctx);
            std::memcpy(out, digest, 8);
        }
    };

    PacketCipher::PacketCipher() : impl(std::make_unique<Impl>()) {}
    PacketCipher::~PacketCipher() = default;

    bool PacketCipher::InitWithKey(const uint8_t key[32]) {
        Impl& s = *impl;
        std::memcpy(s.key, key, 32);
        uint8_t iv[16];
        std::memcpy(iv, key, 12);
        iv[12] = 0;
        iv[13] = 0;
        iv[14] = 0;
        iv[15] = 2;
        for (EVP_CIPHER_CTX** ctx : {&s.enc, &s.dec}) {
            if (*ctx)
                EVP_CIPHER_CTX_free(*ctx);
            *ctx = EVP_CIPHER_CTX_new();
            if (!*ctx || EVP_EncryptInit_ex(*ctx, EVP_aes_256_ctr(), nullptr, key, iv) != 1)
                return false;
        }
        s.sendCounter = s.recvCounter = 0;
        active = true;
        return true;
    }

    bool PacketCipher::InitFromHandshake(const ClientIdentity& identity, const std::string& token,
                                         std::string& error) {
        const size_t a = token.find('.');
        const size_t b = a == std::string::npos ? a : token.find('.', a + 1);
        if (b == std::string::npos) {
            error = "malformed handshake token";
            return false;
        }
        auto header = json::Document::Parse(B64UrlDecode(token.substr(0, a)));
        auto payload = json::Document::Parse(B64UrlDecode(token.substr(a + 1, b - a - 1)));
        if (!header || !payload || !header->root()["x5u"].isString() || !payload->root()["salt"].isString()) {
            error = "handshake token has no key or salt";
            return false;
        }
        const std::string der = B64UrlDecode(header->root()["x5u"].str());
        const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
        EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size()));
        if (!pkey) {
            error = "bad server public key";
            return false;
        }
        EC_KEY* peer = EVP_PKEY_get1_EC_KEY(pkey);
        EVP_PKEY_free(pkey);
        if (!peer || !identity.Key()) {
            error = "no EC key for the handshake";
            if (peer)
                EC_KEY_free(peer);
            return false;
        }
        uint8_t secret[64];
        const int secretLen =
            ECDH_compute_key(secret, sizeof(secret), EC_KEY_get0_public_key(peer), identity.Key(), nullptr);
        EC_KEY_free(peer);
        if (secretLen <= 0) {
            error = "ECDH failed";
            return false;
        }
        const std::string salt = B64UrlDecode(payload->root()["salt"].str());
        std::string input = salt;
        input.append(reinterpret_cast<char*>(secret), size_t(secretLen));
        uint8_t key[32];
        unsigned int len = 0;
        EVP_Digest(input.data(), input.size(), key, &len, EVP_sha256(), nullptr);
        if (!InitWithKey(key)) {
            error = "could not start AES";
            return false;
        }
        return true;
    }

    std::vector<uint8_t> PacketCipher::Encrypt(const std::vector<uint8_t>& plain) {
        Impl& s = *impl;
        std::vector<uint8_t> out(plain.size() + 8);
        std::memcpy(out.data(), plain.data(), plain.size());
        Impl::Checksum(s.sendCounter++, plain.data(), plain.size(), s.key, out.data() + plain.size());
        int outLen = 0;
        EVP_EncryptUpdate(s.enc, out.data(), &outLen, out.data(), static_cast<int>(out.size()));
        return out;
    }

    bool PacketCipher::Decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& plain) {
        Impl& s = *impl;
        if (cipher.size() < 8)
            return false;
        plain.assign(cipher.size(), 0);
        int outLen = 0;
        EVP_EncryptUpdate(s.dec, plain.data(), &outLen, cipher.data(), static_cast<int>(cipher.size()));
        const size_t n = plain.size() - 8;
        uint8_t sum[8];
        Impl::Checksum(s.recvCounter++, plain.data(), n, s.key, sum);
        const bool ok = std::memcmp(sum, plain.data() + n, 8) == 0;
        plain.resize(n);
        return ok;
    }
}
