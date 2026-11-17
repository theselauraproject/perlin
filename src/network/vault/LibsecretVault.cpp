#include "network/SecretVault.hpp"

#if defined(__linux__) && !defined(__ANDROID__)
#include "util/Log.hpp"

#include <dlfcn.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/stat.h>
#endif

namespace perlin::auth::vault {
#if defined(__linux__) && !defined(__ANDROID__)
    namespace {
        struct GError {
            unsigned domain;
            int code;
            char* message;
        };

        struct SchemaAttribute {
            const char* name;
            int type;
        };

        struct Schema {
            const char* name;
            int flags;
            SchemaAttribute attributes[32];
            int reserved;
            void* reserved1;
            void* reserved2;
            void* reserved3;
            void* reserved4;
            void* reserved5;
            void* reserved6;
            void* reserved7;
        };

        using StoreFn = int (*)(const Schema*, const char*, const char*, const char*, void*, GError**, ...);
        using LookupFn = char* (*)(const Schema*, void*, GError**, ...);
        using ClearFn = int (*)(const Schema*, void*, GError**, ...);
        using FreeFn = void (*)(char*);
        using ErrorFreeFn = void (*)(GError*);

        bool SessionBusPresent() {
            if (const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS"); address && *address)
                return true;
            if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
                struct stat st{};
                return stat((std::string(runtime) + "/bus").c_str(), &st) == 0;
            }
            return false;
        }

        class LibsecretVault final : public SecretVault {
        public:
            const char* Name() const override { return "Secret Service (libsecret)"; }

            bool Available() override {
                std::call_once(probed, [this] { Probe(); });
                return usable;
            }

            Result Load(const std::string& id, std::string& secret) override {
                if (!Available())
                    return Result::Unavailable;
                GError* error = nullptr;
                char* value = lookup(&schema, nullptr, &error, "perlin-auth", VaultAccountName(id).c_str(),
                                     static_cast<const char*>(nullptr));
                if (error) {
                    errorFree(error);
                    return Result::Unavailable;
                }
                if (!value)
                    return Result::Missing;
                secret = value;
                passwordFree(value);
                return Result::Ok;
            }

            bool Store(const std::string& id, const std::string& secret) override {
                if (!Available())
                    return false;
                GError* error = nullptr;
                const int ok = store(&schema, "default", "Perlin Xbox login", secret.c_str(), nullptr, &error,
                                     "perlin-auth", VaultAccountName(id).c_str(),
                                     static_cast<const char*>(nullptr));
                if (error)
                    errorFree(error);
                return ok != 0 && !error;
            }

            void Erase(const std::string& id) override {
                if (!Available())
                    return;
                GError* error = nullptr;
                clear(&schema, nullptr, &error, "perlin-auth", VaultAccountName(id).c_str(),
                      static_cast<const char*>(nullptr));
                if (error)
                    errorFree(error);
            }

        private:
            void Probe() {
                if (!SessionBusPresent())
                    return;
                void* secretLib = dlopen("libsecret-1.so.0", RTLD_LAZY | RTLD_LOCAL);
                void* glibLib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
                if (!secretLib || !glibLib)
                    return;
                store = reinterpret_cast<StoreFn>(dlsym(secretLib, "secret_password_store_sync"));
                lookup = reinterpret_cast<LookupFn>(dlsym(secretLib, "secret_password_lookup_sync"));
                clear = reinterpret_cast<ClearFn>(dlsym(secretLib, "secret_password_clear_sync"));
                passwordFree = reinterpret_cast<FreeFn>(dlsym(secretLib, "secret_password_free"));
                errorFree = reinterpret_cast<ErrorFreeFn>(dlsym(glibLib, "g_error_free"));
                if (!store || !lookup || !clear || !passwordFree || !errorFree)
                    return;
                schema.name = "net.perlin.XboxLogin";
                schema.flags = 0;
                schema.attributes[0] = {"perlin-auth", 0};
                schema.attributes[1] = {nullptr, 0};
                GError* error = nullptr;
                char* probe = lookup(&schema, nullptr, &error, "perlin-auth", "availability-probe",
                                     static_cast<const char*>(nullptr));
                if (probe)
                    passwordFree(probe);
                if (error) {
                    errorFree(error);
                    Log("auth", "no Secret Service available; credentials fall back to a private file");
                    return;
                }
                usable = true;
            }

            std::once_flag probed;
            bool usable = false;
            Schema schema{};
            StoreFn store = nullptr;
            LookupFn lookup = nullptr;
            ClearFn clear = nullptr;
            FreeFn passwordFree = nullptr;
            ErrorFreeFn errorFree = nullptr;
        };
    }

    std::shared_ptr<SecretVault> MakeLibsecret() {
        static const std::shared_ptr<SecretVault> instance = std::make_shared<LibsecretVault>();
        return instance;
    }
#else
    std::shared_ptr<SecretVault> MakeLibsecret() {
        return nullptr;
    }
#endif
}
