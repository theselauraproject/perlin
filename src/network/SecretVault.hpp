#pragma once
#include <memory>
#include <string>

namespace perlin::auth {

    class SecretVault {
    public:
        enum class Result { Ok, Missing, Unavailable };

        virtual ~SecretVault() = default;
        virtual const char* Name() const = 0;
        virtual bool Available() = 0;
        virtual Result Load(const std::string& id, std::string& secret) = 0;
        virtual bool Store(const std::string& id, const std::string& secret) = 0;
        virtual void Erase(const std::string& id) = 0;
    };

    std::shared_ptr<SecretVault> DefaultVault();

    std::string NewDataKey();
    std::string Encrypt(const std::string& key, const std::string& plain);
    bool Decrypt(const std::string& key, const std::string& sealed, std::string& plain);
    bool IsEncrypted(const std::string& bytes);
    std::string VaultAccountName(const std::string& id);

    namespace vault {
        std::shared_ptr<SecretVault> MakeDpapi();
        std::shared_ptr<SecretVault> MakeKeychain();
        std::shared_ptr<SecretVault> MakeKeystore();
        std::shared_ptr<SecretVault> MakeLibsecret();
    }
}
