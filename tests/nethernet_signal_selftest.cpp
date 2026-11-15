#include "network/NetherNetSignal.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <thread>

using namespace perlin::nethernet;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what, int line) {
        if (!ok) {
            std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
            failures++;
        }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    std::string RefStrip(const std::string& sdp) {
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

    std::string RefCollapse(const std::string& offer) {
        std::string clean;
        for (size_t i = 0; i < offer.size(); i++) {
            if (offer[i] == '\r' && i + 1 < offer.size() && offer[i + 1] == '\r')
                continue;
            clean.push_back(offer[i]);
        }
        return clean;
    }

    void RefHandler(const std::string& hostId, const std::string& idText, const std::string& from,
                    const std::string& text, std::optional<Signal>& out) {
        std::string lower = from;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        if (lower != hostId)
            return;
        const size_t a = text.find(' ');
        const size_t b = a == std::string::npos ? a : text.find(' ', a + 1);
        if (b == std::string::npos || text.substr(a + 1, b - a - 1) != idText)
            return;
        out = Signal{text.substr(0, a), text.substr(b + 1)};
    }
}

int main() {
    std::mt19937 rng(7);
    const char alphabet[] = "ab \r\n=:identity1";
    auto randStr = [&](int n) {
        std::string s;
        for (int i = 0; i < n; i++)
            s.push_back(alphabet[rng() % (sizeof(alphabet) - 1)]);
        return s;
    };
    for (int i = 0; i < 20000; i++) {
        std::string s = randStr(int(rng() % 40));
        if (rng() % 3 == 0)
            s += "\r\na=identity:x\r\n" + randStr(5);
        CHECK(StripIdentityLines(s) == RefStrip(s));
        CHECK(CollapseDoubledCarriageReturns(s) == RefCollapse(s));
    }

    CHECK(StripIdentityLines("v=0\r\na=identity:abc\r\nm=x\r\n") == "v=0\r\nm=x\r\n");
    CHECK(CollapseDoubledCarriageReturns("a\r\r\nb") == "a\r\nb");
    CHECK(CollapseDoubledCarriageReturns("a\r\r\rb") == "a\rb");

    for (int i = 0; i < 20000; i++) {
        const std::string id = "123";
        std::string text;
        switch (rng() % 6) {
        case 0:
            text = "CONNECTRESPONSE 123 body with spaces";
            break;
        case 1:
            text = "CANDIDATEADD 124 x";
            break;
        case 2:
            text = "CANDIDATEADD 123";
            break;
        case 3:
            text = "NOSPACE";
            break;
        case 4:
            text = "CONNECTERROR 123 ";
            break;
        default:
            text = randStr(int(rng() % 12));
            break;
        }
        std::optional<Signal> ref;
        RefHandler("host", id, (rng() & 1) ? "HoSt" : "other", text, ref);
        SignalInbox inbox("HOST", id);
        const bool fromHost = ref.has_value() || (rng() & 1);
        inbox.Receive(fromHost ? "hOsT" : "nope", text);
        auto got = inbox.Next(std::chrono::milliseconds(0));
        std::optional<Signal> expect;
        if (fromHost)
            RefHandler("host", id, "host", text, expect);
        CHECK(got.has_value() == expect.has_value());
        if (got && expect) {
            CHECK(got->command == expect->command);
            CHECK(got->payload == expect->payload);
        }
    }

    CHECK(ParseSignal("CONNECTERROR 9 12", "9")->payload == "12");
    CHECK(!ParseSignal("CONNECTERROR 9", "9"));
    CHECK(ParseSignal("X 9 ", "9")->payload.empty());

    {
        SignalInbox inbox("h", "5");
        std::thread t([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            inbox.Receive("H", "CANDIDATEADD 5 c1");
            inbox.Receive("h", "CANDIDATEADD 5 c2");
        });
        auto a = inbox.Next(std::chrono::milliseconds(2000));
        CHECK(a && a->payload == "c1");
        auto b = inbox.Next(std::chrono::milliseconds(2000));
        CHECK(b && b->payload == "c2");
        t.join();
        CHECK(!inbox.Next(std::chrono::milliseconds(10)));
    }

    if (failures) {
        std::fprintf(stderr, "nethernet_signal_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("nethernet_signal_selftest passed");
    return 0;
}
