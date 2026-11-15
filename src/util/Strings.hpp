#pragma once
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace perlin::str {

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

    constexpr std::string_view StripPrefix(std::string_view s, std::string_view prefix) noexcept {
        return s.starts_with(prefix) ? s.substr(prefix.size()) : s;
    }
    constexpr std::string_view StripNamespace(std::string_view id) noexcept {
        return StripPrefix(id, "minecraft:");
    }

    constexpr std::string_view StripAnyNamespace(std::string_view id) noexcept {
        const size_t colon = id.find(':');
        return colon == std::string_view::npos ? id : id.substr(colon + 1);
    }

    enum class SectionEncoding { Utf8, Utf8OrLatin1 };

    inline std::string StripFormatting(std::string_view text, SectionEncoding encoding = SectionEncoding::Utf8) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); i++) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                i += 2;
                continue;
            }
            if (encoding == SectionEncoding::Utf8OrLatin1 && byte == 0xA7) {
                i += 1;
                continue;
            }
            out += text[i];
        }
        return out;
    }

    inline std::string ReplaceAll(std::string_view s, std::string_view from, std::string_view to) {
        std::string out;
        if (from.empty()) {
            out.assign(s);
            return out;
        }
        out.reserve(s.size());
        for (size_t at = 0;;) {
            const size_t next = s.find(from, at);
            if (next == std::string_view::npos) {
                out.append(s.substr(at));
                return out;
            }
            out.append(s.substr(at, next - at));
            out.append(to);
            at = next + from.size();
        }
    }

    inline std::string ReplaceFirst(std::string_view s, std::string_view from, std::string_view to) {
        const size_t at = s.find(from);
        if (at == std::string_view::npos)
            return std::string(s);
        std::string out;
        out.reserve(s.size() - from.size() + to.size());
        out.append(s.substr(0, at)).append(to).append(s.substr(at + from.size()));
        return out;
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

    template <class... Args> std::string Cat(const Args&... args) {
        std::string out;
        (detail::Append(out, args), ...);
        return out;
    }

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
        requires std::integral<T>
    bool Parse(std::string_view s, T& out, int base = 10) {
        s = TrimView(s);
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
        return ec == std::errc() && ptr == s.data() + s.size();
    }

    inline std::optional<double> ParseDouble(std::string_view s, bool wholeString = true) {
        const std::string text(s);
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || (wholeString && end != text.c_str() + text.size()))
            return std::nullopt;
        return v;
    }
}
