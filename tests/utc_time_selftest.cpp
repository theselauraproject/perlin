#include "util/UtcTime.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace perlin;

static_assert(utc::UnixSeconds(1970, 1, 1, 0, 0, 0) == 0);
static_assert(utc::UnixSeconds(2000, 3, 1, 0, 0, 0) == 951868800);
static_assert(utc::UnixSeconds(2024, 2, 29, 12, 34, 56) == 1709210096);
static_assert(utc::UnixSeconds(1969, 12, 31, 23, 59, 59) == -1);
static_assert(utc::UnixSeconds(2038, 1, 19, 3, 14, 8) == 2147483648LL);
static_assert(utc::UnixSeconds(2026, 13, 1, 0, 0, 0) == 0);
static_assert(utc::UnixSeconds(2026, 1, 1, 24, 0, 0) == 0);

int main() {
    int failures = 0;
    for (int year = 1971; year <= 2100; year += 3)
        for (int month = 1; month <= 12; month++) {
            const auto day = std::chrono::sys_days{std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} / 17};
            const int64_t want = std::chrono::duration_cast<std::chrono::seconds>(day.time_since_epoch()).count() + 5 * 3600 + 6 * 60 + 7;
            if (utc::UnixSeconds(year, month, 17, 5, 6, 7) != want) {
                std::printf("FAIL %d-%02d\n", year, month);
                failures++;
            }
        }
    std::printf(failures ? "%d failure(s)\n" : "utc_time_selftest ok\n", failures);
    return failures ? 1 : 0;
}
