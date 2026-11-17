#include "network/NetherNetSignal.hpp"

#include "util/Strings.hpp"

#include <sstream>

namespace perlin::nethernet {

    std::string StripIdentityLines(const std::string& sdp) {
        std::istringstream in(sdp);
        std::string line, out;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.starts_with("a=identity"))
                continue;
            out += line + "\r\n";
        }
        return out;
    }

    std::string CollapseDoubledCarriageReturns(std::string sdp) {
        std::string clean;
        clean.reserve(sdp.size());
        for (size_t i = 0; i < sdp.size(); i++) {
            if (sdp[i] == '\r' && i + 1 < sdp.size() && sdp[i + 1] == '\r')
                continue;
            clean.push_back(sdp[i]);
        }
        return clean;
    }

    std::optional<Signal> ParseSignal(std::string_view text, std::string_view connectionId) {
        const size_t a = text.find(' ');
        const size_t b = a == std::string_view::npos ? a : text.find(' ', a + 1);
        if (b == std::string_view::npos || text.substr(a + 1, b - a - 1) != connectionId)
            return std::nullopt;
        return Signal{std::string(text.substr(0, a)), std::string(text.substr(b + 1))};
    }

    void CandidateLog::Add(std::string candidate) {
        std::lock_guard lock(m);
        items.push_back(std::move(candidate));
    }

    std::vector<std::string> CandidateLog::Snapshot() const {
        std::lock_guard lock(m);
        return items;
    }

    SignalInbox::SignalInbox(std::string hostNetworkId, std::string connectionId)
        : host(str::Lower(std::move(hostNetworkId))), connection(std::move(connectionId)) {}

    void SignalInbox::Receive(const std::string& from, const std::string& text) {
        if (str::Lower(from) != host)
            return;
        auto signal = ParseSignal(text, connection);
        if (!signal)
            return;
        std::lock_guard lock(m);
        items.push_back(std::move(*signal));
        cv.notify_all();
    }

    std::optional<Signal> SignalInbox::Next(std::chrono::milliseconds wait) {
        std::unique_lock lock(m);
        cv.wait_for(lock, wait, [&] { return !items.empty(); });
        if (items.empty())
            return std::nullopt;
        Signal s = std::move(items.front());
        items.pop_front();
        return s;
    }

}
