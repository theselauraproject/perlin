#pragma once
#include <array>
#include <chrono>
#include <cmath>
#include <compare>
#include <charconv>
#include <cstdint>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

#include "perlin/abi.hpp"
#include "perlin/meta.hpp"

namespace perlin {
    using abi::AimKind;
    using abi::Area;
    using abi::ChatType;
    using abi::Difficulty;
    using abi::Dimension;
    using abi::DisconnectReason;
    using abi::EffectId;
    using abi::EntityFlag;
    using abi::EntityFlags;
    using abi::Face;
    using abi::GameMode;
    using abi::Permission;
    using abi::PermissionLevel;
    using abi::Permissions;
    using abi::Screen;

    using meta::enum_cast;
    using meta::enum_name;

    using Ticks = std::chrono::duration<int64_t, std::ratio<1, 20>>;
    inline namespace literals {
        constexpr Ticks operator""_ticks(unsigned long long n) {
            return Ticks(static_cast<int64_t>(n));
        }
    }

    struct Vec3 {
        double x = 0, y = 0, z = 0;

        constexpr Vec3() = default;
        constexpr Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}

        constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3 operator-() const { return {-x, -y, -z}; }
        constexpr Vec3& operator+=(const Vec3& o) {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }
        constexpr Vec3& operator-=(const Vec3& o) {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }
        constexpr bool operator==(const Vec3&) const = default;

        constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
        constexpr Vec3 cross(const Vec3& o) const {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        constexpr double length_squared() const { return dot(*this); }
        double length() const { return std::sqrt(length_squared()); }
        double horizontal_length() const { return std::sqrt(x * x + z * z); }
        double distance(const Vec3& o) const { return (*this - o).length(); }
        Vec3 normalized() const {
            const double l = length();
            return l > 0 ? *this / l : Vec3{};
        }
    };
    constexpr Vec3 operator*(double s, const Vec3& v) {
        return v * s;
    }

    struct BlockPos {
        int x = 0, y = 0, z = 0;

        constexpr BlockPos() = default;
        constexpr BlockPos(int ax, int ay, int az) : x(ax), y(ay), z(az) {}
        explicit BlockPos(const Vec3& v)
            : x(static_cast<int>(std::floor(v.x))), y(static_cast<int>(std::floor(v.y))),
              z(static_cast<int>(std::floor(v.z))) {}

        constexpr auto operator<=>(const BlockPos&) const = default;
        constexpr BlockPos operator+(const BlockPos& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr BlockPos operator-(const BlockPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr BlockPos up(int n = 1) const { return {x, y + n, z}; }
        constexpr BlockPos down(int n = 1) const { return {x, y - n, z}; }
        constexpr Vec3 center() const { return {x + 0.5, y + 0.5, z + 0.5}; }

        constexpr BlockPos offset(Face face, int n = 1) const;
        constexpr std::array<BlockPos, 6> neighbors() const;
    };

    constexpr BlockPos direction(Face face) {
        constexpr std::array<BlockPos, 6> table{{
            {0, -1, 0},
            {0, 1, 0},
            {0, 0, -1},
            {0, 0, 1},
            {-1, 0, 0},
            {1, 0, 0},
        }};
        static_assert(table.size() == meta::enum_count<Face>);
        return table[static_cast<size_t>(face)];
    }

    constexpr Face opposite(Face face) {
        constexpr std::array<Face, 6> table{
            {Face::Up, Face::Down, Face::South, Face::North, Face::East, Face::West}};
        return table[static_cast<size_t>(face)];
    }

    constexpr BlockPos BlockPos::offset(Face face, int n) const {
        const BlockPos d = direction(face);
        return {x + d.x * n, y + d.y * n, z + d.z * n};
    }

    constexpr std::array<BlockPos, 6> BlockPos::neighbors() const {
        return {offset(Face::Down),  offset(Face::Up),   offset(Face::North),
                offset(Face::South), offset(Face::West), offset(Face::East)};
    }

    class Identifier {
    public:
        static constexpr size_t kMaxLength = 95;

        constexpr Identifier() = default;

        template <size_t N> consteval Identifier(const char (&literal)[N]) {
            if (!assign(std::string_view(literal, N - 1)))
                invalid_identifier_literal();
        }

        static constexpr std::optional<Identifier> parse(std::string_view text) {
            Identifier id;
            if (!id.assign(text))
                return std::nullopt;
            return id;
        }

        constexpr bool empty() const { return length_ == 0; }
        constexpr std::string_view view() const { return {text_, length_}; }
        constexpr std::string_view name_space() const { return view().substr(0, colon_); }
        constexpr std::string_view path() const { return view().substr(colon_ + 1); }
        std::string str() const { return std::string(view()); }

        constexpr bool matches(std::string_view raw) const {
            const auto other = parse(raw);
            return other && *other == *this;
        }

        friend constexpr bool operator==(const Identifier& a, const Identifier& b) {
            return a.view() == b.view();
        }
        friend constexpr auto operator<=>(const Identifier& a, const Identifier& b) {
            return a.view() <=> b.view();
        }

    private:
        static void invalid_identifier_literal();

        constexpr bool assign(std::string_view in) {
            const size_t colon = in.find(':');
            std::string_view ns = "minecraft", path_part = in;
            if (colon != std::string_view::npos) {
                ns = in.substr(0, colon);
                path_part = in.substr(colon + 1);
            }
            if (ns.empty() || path_part.empty() || ns.size() + 1 + path_part.size() > kMaxLength)
                return false;
            const auto ok_ns = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            };
            const auto ok_path = [&](char c) { return ok_ns(c) || c == '/'; };
            for (char c : ns)
                if (!ok_ns(c))
                    return false;
            for (char c : path_part)
                if (!ok_path(c))
                    return false;
            size_t at = 0;
            for (char c : ns)
                text_[at++] = c;
            text_[at++] = ':';
            colon_ = static_cast<uint8_t>(ns.size());
            for (char c : path_part)
                text_[at++] = c;
            length_ = static_cast<uint8_t>(at);
            return true;
        }

        char text_[kMaxLength + 1]{};
        uint8_t length_ = 0;
        uint8_t colon_ = 0;
    };

    struct Rgba {
        uint8_t r = 255, g = 255, b = 255, a = 255;

        constexpr Rgba() = default;
        constexpr Rgba(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
            : r(red), g(green), b(blue), a(alpha) {}
        static constexpr Rgba hex(uint32_t rgb, uint8_t alpha = 255) {
            return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
                    static_cast<uint8_t>(rgb), alpha};
        }
        constexpr Rgba with_alpha(uint8_t alpha) const { return {r, g, b, alpha}; }
        constexpr uint32_t packed() const {
            return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
                   (static_cast<uint32_t>(b) << 8) | a;
        }
        constexpr bool operator==(const Rgba&) const = default;

        constexpr Rgba mix(const Rgba& to, double t) const {
            const auto lerp = [t](uint8_t from, uint8_t target) {
                return static_cast<uint8_t>(from + (static_cast<double>(target) - from) * t + 0.5);
            };
            return {lerp(r, to.r), lerp(g, to.g), lerp(b, to.b), lerp(a, to.a)};
        }

        static const Rgba white, black, red, green, blue, yellow, gray, dark_gray, gold, aqua, transparent;
    };
    inline constexpr Rgba Rgba::white{255, 255, 255};
    inline constexpr Rgba Rgba::black{0, 0, 0};
    inline constexpr Rgba Rgba::red{255, 85, 85};
    inline constexpr Rgba Rgba::green{85, 255, 85};
    inline constexpr Rgba Rgba::blue{85, 85, 255};
    inline constexpr Rgba Rgba::yellow{255, 255, 85};
    inline constexpr Rgba Rgba::gray{170, 170, 170};
    inline constexpr Rgba Rgba::dark_gray{85, 85, 85};
    inline constexpr Rgba Rgba::gold{255, 170, 0};
    inline constexpr Rgba Rgba::aqua{85, 255, 255};
    inline constexpr Rgba Rgba::transparent{0, 0, 0, 0};

    inline namespace literals {
        constexpr Rgba operator""_rgb(unsigned long long hex) {
            return Rgba::hex(static_cast<uint32_t>(hex));
        }
    }

    using Item = abi::Item;
    using Entity = abi::Entity;
    using PlayerState = abi::PlayerState;
    using Inventory = abi::Inventory;
    using WorldState = abi::WorldState;
    using RayHit = abi::RayHit;
    using AimState = abi::AimState;
    using ScreenState = abi::ScreenState;
    using Effect = abi::Effect;
    using PlayerEntry = abi::PlayerEntry;

    struct BlockInfo : abi::BlockInfo {
        BlockInfo() = default;
        BlockInfo(abi::BlockInfo raw) : abi::BlockInfo(std::move(raw)) {}

        Identifier id() const { return Identifier::parse(name).value_or(Identifier{}); }
        bool is(const Identifier& other) const { return loaded && other.matches(name); }
        bool is_air() const { return loaded && (name == "minecraft:air" || name == "air"); }
        std::optional<std::string_view> state(std::string_view key) const {
            for (const auto& [k, v] : states)
                if (k == key)
                    return std::string_view(v);
            return std::nullopt;
        }
        int state_int(std::string_view key, int fallback = 0) const {
            const auto v = state(key);
            if (!v)
                return fallback;
            if (*v == "true")
                return 1;
            if (*v == "false")
                return 0;
            int out = fallback;
            const auto [ptr, ec] = std::from_chars(v->data(), v->data() + v->size(), out);
            return ec == std::errc{} ? out : fallback;
        }
        bool state_bool(std::string_view key, bool fallback = false) const {
            return state_int(key, fallback ? 1 : 0) != 0;
        }
    };

    using BlockProps = abi::BlockProps;
    using BlockEntityInfo = abi::BlockEntityInfo;
    using BreakState = abi::BreakState;
    using Tool = abi::Tool;
    using NameKind = abi::NameKind;
    using Attribute = abi::Attribute;

    inline std::string_view short_name(std::string_view id) {
        const size_t colon = id.find(':');
        return colon == std::string_view::npos ? id : id.substr(colon + 1);
    }
}

template <> struct std::hash<perlin::BlockPos> {
    size_t operator()(const perlin::BlockPos& p) const noexcept {
        uint64_t h = static_cast<uint32_t>(p.x);
        h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(p.y);
        h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(p.z);
        return static_cast<size_t>(h ^ (h >> 29));
    }
};

template <> struct std::hash<perlin::Identifier> {
    size_t operator()(const perlin::Identifier& id) const noexcept {
        return std::hash<std::string_view>{}(id.view());
    }
};

template <> struct std::formatter<perlin::Identifier> : std::formatter<std::string_view> {
    template <class FormatContext> auto format(const perlin::Identifier& id, FormatContext& ctx) const {
        return std::formatter<std::string_view>::format(id.view(), ctx);
    }
};

template <> struct std::formatter<perlin::Vec3> : std::formatter<double> {
    template <class FormatContext> auto format(const perlin::Vec3& v, FormatContext& ctx) const {
        auto out = ctx.out();
        out = std::format_to(out, "(");
        out = std::formatter<double>::format(v.x, ctx);
        out = std::format_to(out, ", ");
        out = std::formatter<double>::format(v.y, ctx);
        out = std::format_to(out, ", ");
        out = std::formatter<double>::format(v.z, ctx);
        return std::format_to(out, ")");
    }
};

template <> struct std::formatter<perlin::BlockPos> : std::formatter<int> {
    template <class FormatContext> auto format(const perlin::BlockPos& p, FormatContext& ctx) const {
        return std::format_to(ctx.out(), "[{}, {}, {}]", p.x, p.y, p.z);
    }
};

template <perlin::meta::FormattableEnum E> struct std::formatter<E> : std::formatter<std::string_view> {
    template <class FormatContext> auto format(E value, FormatContext& ctx) const {
        const std::string_view name = perlin::meta::enum_name(value);
        if (!name.empty())
            return std::formatter<std::string_view>::format(name, ctx);
        return std::format_to(ctx.out(), "{}", static_cast<std::underlying_type_t<E>>(value));
    }
};
