#pragma once
#include <cctype>
#include <charconv>
#include <concepts>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace perlin::str {
    // the C locale's isspace set; the game never switches locale
    constexpr bool IsSpace(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
    }

    constexpr char AsciiLower(char c) noexcept {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    }
    constexpr char AsciiUpper(char c) noexcept {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
    }

    inline std::string Lower(std::string s) {
        for (char& c : s)
            c = AsciiLower(c);
        return s;
    }

    inline std::string Upper(std::string s) {
        for (char& c : s)
            c = AsciiUpper(c);
        return s;
    }

    constexpr std::string_view TrimView(std::string_view s) noexcept {
        while (!s.empty() && IsSpace(s.front()))
            s.remove_prefix(1);
        while (!s.empty() && IsSpace(s.back()))
            s.remove_suffix(1);
        return s;
    }

    inline std::string Trim(std::string_view s) {
        return std::string(TrimView(s));
    }

    constexpr std::string_view TrimRightView(std::string_view s, std::string_view chars) noexcept {
        while (!s.empty() && chars.contains(s.back()))
            s.remove_suffix(1);
        return s;
    }

    constexpr bool ContainsAny(std::string_view s, std::initializer_list<std::string_view> parts) noexcept {
        for (std::string_view p : parts)
            if (s.contains(p))
                return true;
        return false;
    }

    // "minecraft:stone" -> "stone"; anything else unchanged
    constexpr std::string_view StripPrefix(std::string_view s, std::string_view prefix) noexcept {
        return s.starts_with(prefix) ? s.substr(prefix.size()) : s;
    }
    constexpr std::string_view StripNamespace(std::string_view id) noexcept {
        return StripPrefix(id, "minecraft:");
    }

    inline std::vector<std::string> Split(std::string_view s, char sep, bool skipEmpty = false) {
        std::vector<std::string> out;
        for (size_t from = 0;;) {
            const size_t at = s.find(sep, from);
            const std::string_view part =
                s.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from);
            if (!skipEmpty || !part.empty())
                out.emplace_back(part);
            if (at == std::string_view::npos)
                return out;
            from = at + 1;
        }
    }

    template <class F>
        requires std::invocable<F, std::string_view>
    void ForEachSplit(std::string_view s, char sep, F&& fn) {
        for (size_t from = 0;;) {
            const size_t at = s.find(sep, from);
            fn(s.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
            if (at == std::string_view::npos)
                return;
            from = at + 1;
        }
    }

    template <class Range> std::string Join(const Range& parts, std::string_view sep) {
        std::string out;
        bool first = true;
        for (const auto& p : parts) {
            if (!first)
                out += sep;
            out += p;
            first = false;
        }
        return out;
    }

    namespace detail {
        inline void Append(std::string& out, std::string_view v) {
            out += v;
        }
        inline void Append(std::string& out, const char* v) {
            out += v;
        }
        inline void Append(std::string& out, const std::string& v) {
            out += v;
        }
        inline void Append(std::string& out, char v) {
            out += v;
        }
        inline void Append(std::string& out, bool v) {
            out += v ? '1' : '0';
        }
        template <class T>
            requires(std::is_arithmetic_v<T> && !std::same_as<T, bool> && !std::same_as<T, char>)
        void Append(std::string& out, T v) {
            out += std::to_string(v);
        }
    }

    // absl::StrCat: strings verbatim, numbers through std::to_string, bools as 1/0
    template <class... Args> std::string Cat(const Args&... args) {
        std::string out;
        (detail::Append(out, args), ...);
        return out;
    }

    // a cache key from every input that changes a built document, '|' between parts
    template <class... Args> std::string Key(const Args&... args) {
        std::string out;
        auto add = [&out, first = true](const auto& v) mutable {
            if (!first)
                out += '|';
            first = false;
            detail::Append(out, v);
        };
        (add(args), ...);
        return out;
    }

    template <class T>
        requires std::integral<T> // floating from_chars needs macOS 13.3
    bool Parse(std::string_view s, T& out) {
        s = TrimView(s);
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
        return ec == std::errc() && ptr == s.data() + s.size();
    }
}
