#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    namespace xbox {
        class Account;
    }

    namespace friendworld {

        struct IceServerInfo {
            std::string username;
            std::string password;
            std::vector<std::string> urls;
        };

        class Signaling {
        public:
            virtual ~Signaling() = default;
            virtual bool Send(const std::string& toNetworkId, const std::string& text, bool waitDelivery,
                              std::string& error) = 0;
            virtual void
            SetHandler(std::function<void(const std::string& from, const std::string& text)> handler) = 0;
            virtual bool Credentials(std::vector<IceServerInfo>& servers, std::string& error) = 0;
            virtual bool Alive() const = 0;
            virtual void Close() = 0;
        };

        struct Lease {
            std::shared_ptr<Signaling> signaling;
            std::string hostNetworkId;
            std::string nonce;
            std::string issuer;
            std::string hostName;
            std::string worldName;
            std::string version;
            int protocol = 0;
            std::function<void()> leave;

            Lease() = default;
            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;
            ~Lease() { Release(); }
            void Release() {
                if (signaling)
                    signaling->Close();
                signaling.reset();
                if (leave) {
                    auto fn = std::move(leave);
                    leave = nullptr;
                    fn();
                }
            }
        };

        using ProgressFn = std::function<void(const std::string&)>;

        bool Join(xbox::Account& account, const std::string& friendXuid, const std::string& gameVersion,
                  std::shared_ptr<Lease>& out, const ProgressFn& progress, std::string& error);

        bool JoinRealm(xbox::Account& account, const std::string& address, const std::string& protocol,
                       const std::string& gameVersion, const std::string& realmName,
                       std::shared_ptr<Lease>& out, std::string& error);
    }
}
