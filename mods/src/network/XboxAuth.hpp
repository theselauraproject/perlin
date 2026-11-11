#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace perlin::xbox {

    struct Credentials {
        std::string multiplayerToken;
        std::string chainJson;
        std::string gamertag;
        std::string xuid;
        std::string playFabId;
    };

    struct ServiceInfo {
        std::string authorization;
        std::string playerMessagingId;
        std::string issuer;
        std::string signalingUri;
        std::string signalingAfdUri;
    };

    using PromptFn = std::function<void(const std::string& url, const std::string& code)>;

    class Account {
    public:
        explicit Account(std::string cachePath);
        ~Account();
        Account(const Account&) = delete;
        Account& operator=(const Account&) = delete;

        bool HasSavedLogin() const;
        void Cancel();

        bool SignIn(const PromptFn& prompt, std::string& error);
        bool SignInSaved(std::string& error);

        bool FetchGamertag(std::string& gamertag, std::string& error);

        bool AuthorizationHeader(const std::string& relyingParty, std::string& header, std::string& xuid,
                                 std::string& error);

        bool ServiceSession(const std::string& gameVersion, ServiceInfo& out, std::string& error);

        bool LoginMinecraft(const std::string& identityPublicKeyB64, const std::string& gameVersion,
                            Credentials& out, std::string& error);

        static void SignOut(const std::string& cachePath);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    namespace detail {
        std::string SignRequest(void* ecKeyP256, const std::string& method, const std::string& pathAndQuery,
                                const std::string& authorization, const std::string& body, int64_t unixNanos,
                                size_t maxBodyBytes = 0);
        std::string RequestDigest(const std::string& method, const std::string& pathAndQuery,
                                  const std::string& authorization, const std::string& body,
                                  int64_t unixNanos, size_t maxBodyBytes = 0);
        void SetTestEndpoint(const std::string& host, uint16_t port);
        int64_t ParseIsoTime(const std::string& iso);
        int64_t ParseHttpDate(const std::string& date);
    }

}
