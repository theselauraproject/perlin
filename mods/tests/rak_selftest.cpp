#include "network/RakNet.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

using namespace perlin;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static std::vector<uint8_t> Pattern(size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; i++)
        b[i] = uint8_t(i * 7 + i / 251);
    return b;
}

static bool Wait(RakNet& r, std::vector<uint8_t>& out, int ms = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (r.TryReceive(out))
            return true;
        if (!r.IsConnected())
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const uint16_t port = static_cast<uint16_t>(argc > 2 ? std::atoi(argv[2]) : 19232);

    std::string ad, err;
    CHECK(RakNet::Ping(host, port, ad, err));
    std::printf("ping: %s %s\n", ad.c_str(), err.c_str());
    CHECK(ad.find("MCPE;Harness;2225") == 0);

    RakNet rak;
    CHECK(rak.Connect(host, port));
    if (!rak.IsConnected()) {
        std::printf("connect failed: %s\n", rak.LastError().c_str());
        return 1;
    }

    std::vector<uint8_t> got;
    CHECK(rak.SendReliable({'h', 'i'}));
    CHECK(Wait(rak, got) && got == std::vector<uint8_t>({'h', 'i'}));

    std::mt19937 rng(7);
    std::vector<uint8_t> big(100000);
    for (auto& b : big)
        b = uint8_t(rng());
    CHECK(rak.SendReliable(big));
    CHECK(Wait(rak, got) && got == big);

    CHECK(rak.SendReliable({'B', 'I', 'G'}));
    CHECK(Wait(rak, got) && got == Pattern(300000));

    std::vector<uint8_t> req{'C', 'O', 'U', 'N', 'T', 0, 0, 1, 0x90};
    CHECK(rak.SendReliable(req));
    int ordered = 0;
    for (int i = 0; i < 400; i++) {
        if (!Wait(rak, got))
            break;
        bool ok = got.size() >= 5 && got[0] == 'N' &&
                  (uint32_t(got[1]) << 24 | uint32_t(got[2]) << 16 | uint32_t(got[3]) << 8 | got[4]) ==
                      uint32_t(i) &&
                  got.size() == 5 + size_t(i % 900);
        if (ok) {
            const auto pat = Pattern(size_t(i % 900));
            ok = std::equal(pat.begin(), pat.end(), got.begin() + 5);
        }
        if (ok)
            ordered++;
    }
    CHECK(ordered == 400);

    for (int i = 0; i < 300; i++)
        rak.SendReliable({'e', uint8_t(i >> 8), uint8_t(i)});
    int echoed = 0;
    for (int i = 0; i < 300; i++) {
        if (!Wait(rak, got))
            break;
        if (got.size() == 3 && got[0] == 'e' && ((got[1] << 8) | got[2]) == i)
            echoed++;
    }
    CHECK(echoed == 300);

    std::this_thread::sleep_for(std::chrono::seconds(3));
    CHECK(rak.IsConnected());
    rak.Disconnect();
    CHECK(!rak.IsConnected());

    if (failures) {
        std::printf("%d check(s) failed (last error: %s)\n", failures, rak.LastError().c_str());
        return 1;
    }
    std::printf("rak_selftest: ok\n");
    return 0;
}
