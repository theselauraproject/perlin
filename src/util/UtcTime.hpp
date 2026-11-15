#pragma once
#include <cstdint>

namespace perlin::utc {
    constexpr int64_t DaysFromCivil(int64_t year, int month, int day) noexcept {
        year -= month <= 2;
        const int64_t era = (year >= 0 ? year : year - 399) / 400;
        const int64_t yoe = year - era * 400;
        const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    constexpr int64_t UnixSeconds(int year, int month, int day, int hour, int minute, int second) noexcept {
        if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
            minute > 59 || second < 0 || second > 60)
            return 0;
        return DaysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
    }
}
