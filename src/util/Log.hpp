#pragma once
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

// no <format>: libc++ only formats floating point from macOS 13.3 on, and vformat_to instantiates the float
// formatter for every call whatever the argument types; Log takes "{}" and "{N}" only, checked at compile time
namespace perlin::log_detail {
    template <class>
    inline constexpr bool kUnsupported = false;

    template <class T>
    void Append(std::string& out, const T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            out += v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, char>) {
            out += v;
        } else if constexpr (std::is_integral_v<T>) {
            char buf[48];
            out.append(buf, std::to_chars(buf, buf + sizeof buf, v).ptr);
        } else if constexpr (std::is_floating_point_v<T>) {
            // %g: what the old stream logging printed
            char buf[32];
            const int n = std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
            out.append(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
        } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
            out += std::string_view(v);
        } else if constexpr (std::is_null_pointer_v<T>) {
            out += "0x0";
        } else if constexpr (std::is_pointer_v<T>) {
            char buf[2 + 16] = {'0', 'x'};
            out.append(buf, std::to_chars(buf + 2, buf + sizeof buf, reinterpret_cast<std::uintptr_t>(v), 16).ptr);
        } else {
            static_assert(kUnsupported<T>, "perlin::Log: unsupported argument type");
        }
    }

    struct ArgRef {
        const void* value;
        void (*append)(std::string&, const void*);
    };

    template <class T>
    void AppendErased(std::string& out, const void* value) {
        Append(out, *static_cast<const T*>(value));
    }

    // not constexpr: reaching it during constant evaluation is the compile error for a bad format string
    void InvalidLogFormatString();

    consteval void CheckFormat(std::string_view fmt, std::size_t argCount) {
        bool automatic = false, manual = false;
        std::size_t next = 0;
        for (std::size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '}') {
                if (i + 1 >= fmt.size() || fmt[i + 1] != '}')
                    InvalidLogFormatString();
                ++i;
                continue;
            }
            if (fmt[i] != '{')
                continue;
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
                ++i;
                continue;
            }
            std::size_t index = 0;
            std::size_t j = i + 1;
            for (; j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9'; ++j)
                index = index * 10 + static_cast<std::size_t>(fmt[j] - '0');
            // format specs aren't supported: only "{}" and "{N}"
            if (j >= fmt.size() || fmt[j] != '}')
                InvalidLogFormatString();
            if (j == i + 1) {
                automatic = true;
                index = next++;
            } else {
                manual = true;
            }
            if (automatic && manual)
                InvalidLogFormatString();
            if (index >= argCount)
                InvalidLogFormatString();
            i = j;
        }
    }

    template <class... Args>
    struct FormatString {
        std::string_view str;

        template <class S>
            requires std::convertible_to<const S&, std::string_view>
        consteval FormatString(const S& s) : str(s) {
            CheckFormat(str, sizeof...(Args));
        }
    };

    // the format string was checked at compile time: no bounds or syntax checks here
    inline void Format(std::string& out, std::string_view fmt, const ArgRef* args) {
        std::size_t next = 0;
        for (std::size_t i = 0; i < fmt.size(); ++i) {
            const char c = fmt[i];
            if (c == '}') {
                out += '}';
                ++i;
            } else if (c != '{') {
                out += c;
            } else if (fmt[i + 1] == '{') {
                out += '{';
                ++i;
            } else {
                std::size_t index = 0;
                std::size_t j = i + 1;
                for (; fmt[j] != '}'; ++j)
                    index = index * 10 + static_cast<std::size_t>(fmt[j] - '0');
                if (j == i + 1)
                    index = next++;
                args[index].append(out, args[index].value);
                i = j;
            }
        }
    }
}

namespace perlin {
    // "[tag] message" on stdout; the line is formatted first and written in one call, so lines from worker
    // threads never interleave
    template <class... Args>
    void Log(std::string_view tag, log_detail::FormatString<std::type_identity_t<Args>...> fmt, Args&&... args) {
        const std::array<log_detail::ArgRef, sizeof...(Args)> refs{
            log_detail::ArgRef{&args, &log_detail::AppendErased<std::remove_cvref_t<Args>>}...};
        std::string line;
        line.reserve(96);
        line += '[';
        line += tag;
        line += "] ";
        log_detail::Format(line, fmt.str, refs.data());
        line += '\n';
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        std::cout.flush();
    }
}
