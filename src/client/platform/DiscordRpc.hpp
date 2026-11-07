#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace perlin::discord {
    struct Presence {
        std::string details;
        std::string state;
        std::string largeImage;
        std::string largeText;
        std::string smallImage;
        std::string smallText;
        int64_t startUnix = 0;
        std::string partyId;
        int partySize = 0;
        int partyMax = 0;
        std::string joinSecret;
        std::vector<std::pair<std::string, std::string>> buttons;
        bool operator==(const Presence&) const = default;
    };

    struct JoinRequest {
        std::string userId;
        std::string username;
    };

    bool Supported();

    class Client {
    public:
        explicit Client(std::string applicationId);
        ~Client();
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        void Set(std::optional<Presence> presence);
        void SetAutoAccept(bool accept) { autoAccept = accept; }
        std::optional<std::string> TakeJoin();
        std::optional<JoinRequest> TakeAccepted();
        bool Connected() const { return connected; }
        std::string User() const;

    private:
        struct Pipe;
        void Run();
        bool Session(Pipe& pipe);
        void Handle(Pipe& pipe, const std::string& json);
        std::string ActivityJson(const std::optional<Presence>& p) const;
        std::string Nonce();

        std::string appId;
        std::thread worker;
        std::atomic<bool> stop{false};
        std::atomic<bool> connected{false};
        std::atomic<bool> autoAccept{true};
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::optional<Presence> wanted;
        uint64_t wantedGeneration = 0;
        std::deque<std::string> joins;
        std::deque<JoinRequest> accepted;
        std::string user;
        uint64_t nonce = 0;
    };

    bool RegisterLaunch(const std::string& applicationId, const std::string& executable);
}
