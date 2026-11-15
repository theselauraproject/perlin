#include "network/SecretVault.hpp"

#include "network/TokenStore.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstring>

namespace perlin::auth {
    namespace {
        constexpr const char kMagic[] = "PRLNENC2\n";
        constexpr size_t kMagicSize = sizeof(kMagic) - 1;
        constexpr size_t kNonce = 12;
        constexpr size_t kTag = 16;
        constexpr size_t kKey = 32;
    }

    std::string NewDataKey() {
        std::string key(kKey, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(key.data()), static_cast<int>(kKey)) != 1)
            return {};
        return key;
    }

    bool IsEncrypted(const std::string& bytes) {
        return bytes.size() >= kMagicSize && std::memcmp(bytes.data(), kMagic, kMagicSize) == 0;
    }

    std::string Encrypt(const std::string& key, const std::string& plain) {
        if (key.size() != kKey)
            return {};
        std::string out(kMagic, kMagicSize);
        std::string nonce(kNonce, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), static_cast<int>(kNonce)) != 1)
            return {};
        out += nonce;
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        std::string cipher(plain.size(), '\0');
        unsigned char tag[kTag];
        int len = 0;
        int total = 0;
        bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                            reinterpret_cast<const unsigned char*>(key.data()),
                                            reinterpret_cast<const unsigned char*>(nonce.data())) == 1;
        ok = ok && EVP_EncryptUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(kMagic),
                                     static_cast<int>(kMagicSize)) == 1;
        if (ok && !plain.empty())
            ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipher.data()), &len,
                                   reinterpret_cast<const unsigned char*>(plain.data()),
                                   static_cast<int>(plain.size())) == 1;
        total = len;
        ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(cipher.data()) + total, &len) == 1;
        ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTag), tag) == 1;
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
        if (!ok)
            return {};
        out += cipher;
        out.append(reinterpret_cast<char*>(tag), kTag);
        return out;
    }

    bool Decrypt(const std::string& key, const std::string& sealed, std::string& plain) {
        if (key.size() != kKey || !IsEncrypted(sealed) || sealed.size() < kMagicSize + kNonce + kTag)
            return false;
        const size_t cipherSize = sealed.size() - kMagicSize - kNonce - kTag;
        const unsigned char* nonce = reinterpret_cast<const unsigned char*>(sealed.data()) + kMagicSize;
        const unsigned char* cipher = nonce + kNonce;
        unsigned char tag[kTag];
        std::memcpy(tag, cipher + cipherSize, kTag);
        std::string out(cipherSize, '\0');
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        int len = 0;
        bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                            reinterpret_cast<const unsigned char*>(key.data()), nonce) == 1;
        ok = ok && EVP_DecryptUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(kMagic),
                                     static_cast<int>(kMagicSize)) == 1;
        if (ok && cipherSize)
            ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len, cipher,
                                   static_cast<int>(cipherSize)) == 1;
        ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTag), tag) == 1;
        ok = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + len, &len) == 1;
        if (ctx)
            EVP_CIPHER_CTX_free(ctx);
        if (!ok)
            return false;
        plain = std::move(out);
        return true;
    }

    std::string VaultAccountName(const std::string& id) {
        return "auth-" + policy::Fingerprint(id).substr(0, 32);
    }

    std::shared_ptr<SecretVault> DefaultVault() {
#if defined(_WIN32)
        return vault::MakeDpapi();
#elif defined(__APPLE__)
        return vault::MakeKeychain();
#elif defined(__ANDROID__)
        return vault::MakeKeystore();
#elif defined(__EMSCRIPTEN__)
        return nullptr;
#else
        return vault::MakeLibsecret();
#endif
    }
}
