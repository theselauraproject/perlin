#include "network/SecretVault.hpp"

#ifdef _WIN32
#include "network/AtomicFile.hpp"
#include "util/FileIo.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include <filesystem>
#endif

namespace perlin::auth::vault {
#ifdef _WIN32
    namespace {
        class DpapiVault final : public SecretVault {
        public:
            const char* Name() const override { return "Windows DPAPI"; }
            bool Available() override { return true; }

            Result Load(const std::string& id, std::string& secret) override {
                std::string blob;
                if (!file::Read(id + ".key", blob))
                    return Result::Missing;
                DATA_BLOB in{static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(blob.data())};
                DATA_BLOB out{};
                if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
                    return Result::Unavailable;
                secret.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
                SecureZeroMemory(out.pbData, out.cbData);
                LocalFree(out.pbData);
                return Result::Ok;
            }

            bool Store(const std::string& id, const std::string& secret) override {
                DATA_BLOB in{static_cast<DWORD>(secret.size()),
                             reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
                DATA_BLOB out{};
                if (!CryptProtectData(&in, L"Perlin Xbox login key", nullptr, nullptr, nullptr, 0, &out))
                    return false;
                const std::string blob(reinterpret_cast<char*>(out.pbData), out.cbData);
                LocalFree(out.pbData);
                return WriteFileAtomic(id + ".key", blob);
            }

            void Erase(const std::string& id) override {
                std::error_code ec;
                std::filesystem::remove(id + ".key", ec);
            }
        };
    }

    std::shared_ptr<SecretVault> MakeDpapi() {
        return std::make_shared<DpapiVault>();
    }
#else
    std::shared_ptr<SecretVault> MakeDpapi() {
        return nullptr;
    }
#endif
}
