#include "network/SecretVault.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace perlin::auth::vault {
#ifdef __APPLE__
    namespace {
        class KeychainVault final : public SecretVault {
        public:
            const char* Name() const override { return "Apple Keychain"; }
            bool Available() override { return true; }

            Result Load(const std::string& id, std::string& secret) override {
                CFMutableDictionaryRef query = Query(id);
                CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
                CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
                CFTypeRef found = nullptr;
                const OSStatus status = SecItemCopyMatching(query, &found);
                CFRelease(query);
                if (status == errSecItemNotFound)
                    return Result::Missing;
                if (status != errSecSuccess || !found)
                    return Result::Unavailable;
                CFDataRef data = static_cast<CFDataRef>(found);
                secret.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                              static_cast<size_t>(CFDataGetLength(data)));
                CFRelease(found);
                return Result::Ok;
            }

            bool Store(const std::string& id, const std::string& secret) override {
                Erase(id);
                CFMutableDictionaryRef item = Query(id);
                CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.data()),
                                              static_cast<CFIndex>(secret.size()));
                CFDictionarySetValue(item, kSecValueData, data);
                CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
                const OSStatus status = SecItemAdd(item, nullptr);
                CFRelease(data);
                CFRelease(item);
                return status == errSecSuccess;
            }

            void Erase(const std::string& id) override {
                CFMutableDictionaryRef query = Query(id);
                SecItemDelete(query);
                CFRelease(query);
            }

        private:
            static CFMutableDictionaryRef Query(const std::string& id) {
                CFMutableDictionaryRef query = CFDictionaryCreateMutable(
                    kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
                CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault, "Perlin Xbox login",
                                                                kCFStringEncodingUTF8);
                CFStringRef account = CFStringCreateWithCString(
                    kCFAllocatorDefault, VaultAccountName(id).c_str(), kCFStringEncodingUTF8);
                CFDictionarySetValue(query, kSecAttrService, service);
                CFDictionarySetValue(query, kSecAttrAccount, account);
                CFRelease(service);
                CFRelease(account);
                return query;
            }
        };
    }

    std::shared_ptr<SecretVault> MakeKeychain() {
        return std::make_shared<KeychainVault>();
    }
#else
    std::shared_ptr<SecretVault> MakeKeychain() {
        return nullptr;
    }
#endif
}
