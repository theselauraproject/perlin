#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(PERLIN_USE_STD_META) && defined(__cpp_impl_reflection)
#include <meta>
#define PERLIN_HAS_STD_META 1
#else
#define PERLIN_HAS_STD_META 0
#endif

namespace perlin::meta {
    inline constexpr bool uses_std_meta = PERLIN_HAS_STD_META != 0;

    template <size_t N> struct fixed_string {
        char chars[N + 1]{};

        consteval fixed_string() = default;
        consteval fixed_string(const char (&text)[N + 1]) {
            for (size_t i = 0; i < N; i++)
                chars[i] = text[i];
        }
        constexpr operator std::string_view() const { return {chars, N}; }
        constexpr std::string_view view() const { return {chars, N}; }
        static constexpr size_t size() { return N; }
        constexpr const char* c_str() const { return chars; }
        template <size_t M> constexpr bool operator==(const fixed_string<M>& other) const {
            return view() == other.view();
        }
    };
    template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N - 1>;

    namespace detail {
        template <class T> constexpr std::string_view raw_type_name() {
#if defined(__clang__) || defined(__GNUC__)
            return __PRETTY_FUNCTION__;
#else
            return {};
#endif
        }

        template <auto V> constexpr std::string_view raw_value_name() {
#if defined(__clang__) || defined(__GNUC__)
            return __PRETTY_FUNCTION__;
#else
            return {};
#endif
        }

        constexpr std::string_view value_of(std::string_view sig) {
            const size_t eq = sig.find("V = ");
            if (eq == std::string_view::npos)
                return {};
            sig.remove_prefix(eq + 4);
            const size_t end = sig.find_first_of(";]");
            return end == std::string_view::npos ? sig : sig.substr(0, end);
        }

        constexpr std::string_view unqualified(std::string_view name) {
            const size_t colon = name.rfind("::");
            return colon == std::string_view::npos ? name : name.substr(colon + 2);
        }

        constexpr bool is_identifier(std::string_view name) {
            if (name.empty())
                return false;
            const char c = name.front();
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
                return false;
            for (const char ch : name)
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                      ch == '_'))
                    return false;
            return true;
        }
    }

    template <class T> constexpr std::string_view type_name() {
        std::string_view sig = detail::raw_type_name<T>();
#if defined(__clang__)
        constexpr std::string_view key = "T = ";
        const size_t at = sig.find(key);
        if (at == std::string_view::npos)
            return sig;
        sig.remove_prefix(at + key.size());
        return sig.substr(0, sig.rfind(']'));
#elif defined(__GNUC__)
        constexpr std::string_view key = "T = ";
        const size_t at = sig.find(key);
        if (at == std::string_view::npos)
            return sig;
        sig.remove_prefix(at + key.size());
        return sig.substr(0, sig.find_first_of(";]"));
#else
        return sig;
#endif
    }

    template <class E> struct enum_range {
        static constexpr int min = -16;
        static constexpr int max = 255;
    };

    template <class E> struct enable_flags : std::false_type {};

    template <class E>
    concept Enumeration = std::is_enum_v<E>;

    template <class E> struct enable_format : std::false_type {};

    template <class E>
    concept FormattableEnum = std::is_enum_v<E> && enable_format<E>::value;

    template <class E>
    concept FlagEnum = Enumeration<E> && enable_flags<E>::value;

    namespace detail {
        template <class E, int64_t I> constexpr std::string_view probe() {
            constexpr std::string_view name = unqualified(value_of(raw_value_name<static_cast<E>(I)>()));
            if constexpr (is_identifier(name))
                return name;
            else
                return {};
        }

        template <class E, int64_t Base, size_t... K> constexpr auto probe_run(std::index_sequence<K...>) {
            return std::array<std::string_view, sizeof...(K)>{probe<E, Base + static_cast<int64_t>(K)>()...};
        }

        template <class E, size_t... K> constexpr auto probe_bits(std::index_sequence<K...>) {
            return std::array<std::string_view, sizeof...(K)>{
                probe<E, static_cast<int64_t>(uint64_t{1} << K)>()...};
        }

        template <class E>
        inline constexpr int probe_min =
            std::is_unsigned_v<std::underlying_type_t<E>> ? (enum_range<E>::min < 0 ? 0 : enum_range<E>::min)
                                                          : enum_range<E>::min;

        template <class E> struct enum_table {
            struct Entry {
                E value;
                std::string_view name;
            };

#if PERLIN_HAS_STD_META
            static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
            static constexpr size_t count = enumerators.size();
            static constexpr auto entries = [] {
                std::array<Entry, count> out{};
                size_t i = 0;
                template for (constexpr auto e : enumerators) out[i++] =
                    Entry{[:e:], std::meta::identifier_of(e)};
                return out;
            }();
#else
            static constexpr auto names = [] {
                if constexpr (FlagEnum<E>) {
                    return probe_bits<E>(std::make_index_sequence<64>{});
                } else {
                    constexpr int lo = probe_min<E>, hi = enum_range<E>::max;
                    static_assert(hi >= lo && hi - lo < 4096, "enum_range is empty or too wide to probe");
                    return probe_run<E, lo>(std::make_index_sequence<static_cast<size_t>(hi - lo + 1)>{});
                }
            }();
            static constexpr size_t count = [] {
                size_t n = 0;
                for (const auto& s : names)
                    n += s.empty() ? 0 : 1;
                return n;
            }();
            static constexpr auto entries = [] {
                std::array<Entry, count> out{};
                size_t at = 0;
                for (size_t i = 0; i < names.size(); i++) {
                    if (names[i].empty())
                        continue;
                    int64_t v;
                    if constexpr (FlagEnum<E>)
                        v = static_cast<int64_t>(uint64_t{1} << i);
                    else
                        v = probe_min<E> + static_cast<int64_t>(i);
                    out[at++] = Entry{static_cast<E>(v), names[i]};
                }
                return out;
            }();
#endif
        };
    }

    template <Enumeration E> inline constexpr size_t enum_count = detail::enum_table<E>::count;

    template <Enumeration E> inline constexpr auto enum_entries = detail::enum_table<E>::entries;

    template <Enumeration E>
    inline constexpr auto enum_values = [] {
        std::array<E, enum_count<E>> out{};
        for (size_t i = 0; i < out.size(); i++)
            out[i] = enum_entries<E>[i].value;
        return out;
    }();

    template <Enumeration E>
    inline constexpr auto enum_names = [] {
        std::array<std::string_view, enum_count<E>> out{};
        for (size_t i = 0; i < out.size(); i++)
            out[i] = enum_entries<E>[i].name;
        return out;
    }();

    template <Enumeration E> constexpr std::string_view enum_name(E value) {
        for (const auto& e : enum_entries<E>)
            if (e.value == value)
                return e.name;
        return {};
    }

    template <Enumeration E> constexpr std::optional<E> enum_cast(std::string_view name) {
        for (const auto& e : enum_entries<E>)
            if (e.name == name)
                return e.value;
        return std::nullopt;
    }

    template <Enumeration E> constexpr std::optional<E> enum_cast_nocase(std::string_view name) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        for (const auto& e : enum_entries<E>) {
            if (e.name.size() != name.size())
                continue;
            bool same = true;
            for (size_t i = 0; i < name.size() && same; i++)
                same = lower(e.name[i]) == lower(name[i]);
            if (same)
                return e.value;
        }
        return std::nullopt;
    }

    template <Enumeration E> constexpr std::optional<size_t> enum_index(E value) {
        for (size_t i = 0; i < enum_count<E>; i++)
            if (enum_entries<E>[i].value == value)
                return i;
        return std::nullopt;
    }

    template <Enumeration E> constexpr bool is_enumerator(E value) {
        return enum_index(value).has_value();
    }

    template <Enumeration E> constexpr auto to_underlying(E value) {
        return static_cast<std::underlying_type_t<E>>(value);
    }

    template <FlagEnum E> class Flags {
    public:
        static constexpr bool wire_raw_tag = true;
        using Bits = std::make_unsigned_t<std::underlying_type_t<E>>;

        constexpr Flags() = default;
        constexpr Flags(E one) : bits_(static_cast<Bits>(one)) {}
        static constexpr Flags from_bits(Bits bits) {
            Flags f;
            f.bits_ = bits;
            return f;
        }

        constexpr Bits bits() const { return bits_; }
        constexpr bool has(E one) const { return (bits_ & static_cast<Bits>(one)) == static_cast<Bits>(one); }
        constexpr bool any(Flags other) const { return (bits_ & other.bits_) != 0; }
        constexpr bool all(Flags other) const { return (bits_ & other.bits_) == other.bits_; }
        constexpr bool empty() const { return bits_ == 0; }
        constexpr explicit operator bool() const { return bits_ != 0; }
        constexpr size_t count() const { return static_cast<size_t>(std::popcount(bits_)); }

        constexpr Flags& set(E one, bool on = true) {
            bits_ = on ? static_cast<Bits>(bits_ | static_cast<Bits>(one))
                       : static_cast<Bits>(bits_ & ~static_cast<Bits>(one));
            return *this;
        }
        constexpr Flags& reset(E one) { return set(one, false); }
        constexpr Flags& toggle(E one) { return set(one, !has(one)); }

        friend constexpr Flags operator|(Flags a, Flags b) {
            return from_bits(static_cast<Bits>(a.bits_ | b.bits_));
        }
        friend constexpr Flags operator&(Flags a, Flags b) {
            return from_bits(static_cast<Bits>(a.bits_ & b.bits_));
        }
        friend constexpr Flags operator^(Flags a, Flags b) {
            return from_bits(static_cast<Bits>(a.bits_ ^ b.bits_));
        }
        friend constexpr Flags operator~(Flags a) { return from_bits(static_cast<Bits>(~a.bits_)); }
        constexpr Flags& operator|=(Flags o) {
            bits_ = static_cast<Bits>(bits_ | o.bits_);
            return *this;
        }
        constexpr Flags& operator&=(Flags o) {
            bits_ = static_cast<Bits>(bits_ & o.bits_);
            return *this;
        }
        friend constexpr bool operator==(Flags, Flags) = default;

        class iterator {
        public:
            using value_type = E;
            using difference_type = std::ptrdiff_t;
            constexpr iterator() = default;
            constexpr explicit iterator(Bits rest) : rest_(rest) {}
            constexpr E operator*() const {
                return static_cast<E>(static_cast<Bits>(1) << std::countr_zero(rest_));
            }
            constexpr iterator& operator++() {
                rest_ = static_cast<Bits>(rest_ & (rest_ - 1));
                return *this;
            }
            constexpr iterator operator++(int) {
                iterator t = *this;
                ++*this;
                return t;
            }
            friend constexpr bool operator==(iterator, iterator) = default;

        private:
            Bits rest_ = 0;
        };
        constexpr iterator begin() const { return iterator(bits_); }
        constexpr iterator end() const { return iterator(0); }

    private:
        Bits bits_ = 0;
    };

    template <FlagEnum E> constexpr Flags<E> operator|(E a, E b) {
        return Flags<E>(a) | Flags<E>(b);
    }
    template <FlagEnum E> constexpr Flags<E> operator&(E a, E b) {
        return Flags<E>(a) & Flags<E>(b);
    }

    template <Enumeration E, class Storage = uint64_t> class IndexSet {
    public:
        static constexpr bool wire_raw_tag = true;

        constexpr IndexSet() = default;
        static constexpr IndexSet from_bits(Storage bits) {
            IndexSet s;
            s.bits_ = bits;
            return s;
        }
        constexpr Storage bits() const { return bits_; }
        constexpr bool has(E one) const { return ((bits_ >> static_cast<unsigned>(one)) & 1u) != 0; }
        constexpr IndexSet& set(E one, bool on = true) {
            const Storage mask = Storage{1} << static_cast<unsigned>(one);
            bits_ = on ? static_cast<Storage>(bits_ | mask) : static_cast<Storage>(bits_ & ~mask);
            return *this;
        }
        constexpr bool empty() const { return bits_ == 0; }
        constexpr size_t count() const { return static_cast<size_t>(std::popcount(bits_)); }
        friend constexpr bool operator==(IndexSet, IndexSet) = default;

        class iterator {
        public:
            using value_type = E;
            using difference_type = std::ptrdiff_t;
            constexpr iterator() = default;
            constexpr explicit iterator(Storage rest) : rest_(rest) {}
            constexpr E operator*() const { return static_cast<E>(std::countr_zero(rest_)); }
            constexpr iterator& operator++() {
                rest_ = static_cast<Storage>(rest_ & (rest_ - 1));
                return *this;
            }
            constexpr iterator operator++(int) {
                iterator t = *this;
                ++*this;
                return t;
            }
            friend constexpr bool operator==(iterator, iterator) = default;

        private:
            Storage rest_ = 0;
        };
        constexpr iterator begin() const { return iterator(bits_); }
        constexpr iterator end() const { return iterator(0); }

    private:
        Storage bits_ = 0;
    };

    template <class T>
    concept Aggregate = std::is_aggregate_v<std::remove_cvref_t<T>> &&
                        !std::is_array_v<std::remove_cvref_t<T>> && !std::is_union_v<std::remove_cvref_t<T>>;

    namespace detail {
        struct anything {
            template <class U> constexpr operator U() const;
        };

        template <class T, size_t... I> constexpr bool constructible_with(std::index_sequence<I...>) {
            return requires { T{((void)I, anything{})...}; };
        }

        template <class T, size_t N>
        constexpr bool accepts_n = constructible_with<T>(std::make_index_sequence<N>{});

        template <class T, size_t Lo, size_t Hi> constexpr size_t count_members() {
            if constexpr (Lo == Hi) {
                return Lo;
            } else {
                constexpr size_t mid = Lo + (Hi - Lo + 1) / 2;
                if constexpr (accepts_n<T, mid>)
                    return count_members<T, mid, Hi>();
                else
                    return count_members<T, Lo, mid - 1>();
            }
        }
    }
}

#if !PERLIN_HAS_STD_META
#include "perlin/detail/field_binding.hpp"
#endif

namespace perlin::meta {
    template <Aggregate T>
    inline constexpr size_t field_count =
#if PERLIN_HAS_STD_META
        std::meta::nonstatic_data_members_of(^^std::remove_cvref_t<T>, std::meta::access_context::unchecked())
            .size();
#else
        detail::count_members<std::remove_cvref_t<T>, 0, 64>();
#endif

    template <Aggregate T> constexpr auto fields(T& value) {
#if PERLIN_HAS_STD_META
        constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
            ^^std::remove_cvref_t<T>, std::meta::access_context::unchecked()));
        return [&]<size_t... I>(std::index_sequence<I...>) {
            return std::tie(value.[:members[I]:]...);
        }(std::make_index_sequence<members.size()>{});
#else
        return detail::bind_fields<field_count<T>>(value);
#endif
    }

    template <Aggregate T, class Fn> constexpr void for_each_field(T& value, Fn&& fn) {
        std::apply([&](auto&... member) { (fn(member), ...); }, fields(value));
    }

#if PERLIN_HAS_STD_META
    template <Aggregate T>
    inline constexpr auto field_names = [] {
        constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(
            ^^std::remove_cvref_t<T>, std::meta::access_context::unchecked()));
        std::array<std::string_view, members.size()> out{};
        for (size_t i = 0; i < members.size(); i++)
            out[i] = std::meta::identifier_of(members[i]);
        return out;
    }();
#endif

    template <class... Ts> struct overloaded : Ts... {
        using Ts::operator()...;
    };
    template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

    template <class T, template <class...> class Template> inline constexpr bool is_specialization_of = false;
    template <template <class...> class Template, class... Args>
    inline constexpr bool is_specialization_of<Template<Args...>, Template> = true;

    namespace detail {
        template <class F> struct signature;
        template <class R, class... A> struct signature<R (*)(A...)> {
            using result = R;
            using args = std::tuple<A...>;
        };
        template <class R, class C, class... A> struct signature<R (C::*)(A...)> {
            using result = R;
            using args = std::tuple<A...>;
        };
        template <class R, class C, class... A> struct signature<R (C::*)(A...) const> {
            using result = R;
            using args = std::tuple<A...>;
        };
        template <class R, class C, class... A> struct signature<R (C::*)(A...) noexcept> {
            using result = R;
            using args = std::tuple<A...>;
        };
        template <class R, class C, class... A> struct signature<R (C::*)(A...) const noexcept> {
            using result = R;
            using args = std::tuple<A...>;
        };
        template <class F> struct callable : signature<decltype(&F::operator())> {};
        template <class R, class... A> struct callable<R (*)(A...)> : signature<R (*)(A...)> {};
    }

    template <class F> using callable_args_t = typename detail::callable<std::remove_cvref_t<F>>::args;
    template <class F> using callable_result_t = typename detail::callable<std::remove_cvref_t<F>>::result;
}
