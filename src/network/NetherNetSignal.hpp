#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::nethernet {

    struct Signal {
        std::string command;
        std::string payload;
    };

    std::string StripIdentityLines(const std::string& sdp);
    std::string CollapseDoubledCarriageReturns(std::string sdp);
    std::optional<Signal> ParseSignal(std::string_view text, std::string_view connectionId);

    class CandidateLog {
    public:
        void Add(std::string candidate);
        std::vector<std::string> Snapshot() const;

    private:
        mutable std::mutex m;
        std::vector<std::string> items;
    };

    class SignalInbox {
    public:
        SignalInbox(std::string hostNetworkId, std::string connectionId);

        void Receive(const std::string& from, const std::string& text);
        std::optional<Signal> Next(std::chrono::milliseconds wait);

    private:
        const std::string host;
        const std::string connection;
        std::mutex m;
        std::condition_variable cv;
        std::deque<Signal> items;
    };

}
